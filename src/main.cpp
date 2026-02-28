#include <Arduino.h>
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

// ---------------------------------------------------------------------------
// Pin assignments
// Data  (GPIO 0-3):  TX, RX, SDA, SCL pads on Feather header
// Control (GPIO 7-10): D5, D6, D9, D10 pads on Feather header
// ---------------------------------------------------------------------------
#define PIN_D1   0   // Serial data: upper-left quadrant
#define PIN_D2   1   // Serial data: lower-left quadrant
#define PIN_D3   2   // Serial data: upper-right quadrant
#define PIN_D4   3   // Serial data: lower-right quadrant
#define PIN_FLM  7   // Frame start
#define PIN_M    8   // AC driving signal
#define PIN_CL1  9   // Data latch (row clock)
#define PIN_CL2  10  // Shift clock (pixel clock)

// ---------------------------------------------------------------------------
// Buffer layout
//   4 dither phases × 64 rows × 120 bytes = 30720 bytes per display frame
//   Each row: 240 CL2 pulses, nibble-packed 2/byte → 120 bytes
//
// Wire format (framed packet):
//   [SOF: AA 55 F0 0F] [TYPE: 1] [LEN: 2 LE] [CRC16: 2 LE] [PAYLOAD: LEN]
//   TYPE 0x01 = full frame (LEN = 30720): phase0[7680] phase1 phase2 phase3
//   TYPE 0x02 = reboot to bootloader (LEN = 0)
//   CRC = CRC-16/CCITT-FALSE over payload bytes only (poly 0x1021, init 0xFFFF)
// ---------------------------------------------------------------------------
#define LCD_ROWS        64
#define LCD_ROW_BYTES   120
#define LCD_PHASES      4
#define LCD_BUF_BYTES   (LCD_ROWS * LCD_ROW_BYTES)            // 7680
#define LCD_TOTAL_BYTES (LCD_PHASES * LCD_BUF_BYTES)          // 30720

static uint8_t buf[2][LCD_PHASES][LCD_ROWS][LCD_ROW_BYTES];  // 2 × 30720 = 61440 bytes
static volatile int front = 0;

// ---------------------------------------------------------------------------
// Core 1 — LCD refresh loop, cycles through 4 dither phases
// RP2040 @ 125MHz = 8ns/cycle; 20 NOPs ≈ 160ns ≥ 150ns CL2 minimum
// ---------------------------------------------------------------------------
static void core1_main() {
    bool    m_state = false;
    uint8_t phase   = 0;

    while (true) {
        const int b = front;

        for (int row = 0; row < LCD_ROWS; row++) {
            uint32_t ctrl = m_state ? (1u << PIN_M) : 0u;
            if (row == 0) ctrl |= (1u << PIN_FLM);

            const uint8_t* rowbuf = buf[b][phase][row];

            for (int col = 0; col < LCD_ROW_BYTES; col++) {
                uint8_t byte = rowbuf[col];

                // Even pulse: low nibble
                uint32_t d = ctrl | (byte & 0x0Fu);
                sio_hw->gpio_out = d | (1u << PIN_CL2);
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
                sio_hw->gpio_out = d;
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");

                // Odd pulse: high nibble
                d = ctrl | (byte >> 4);
                sio_hw->gpio_out = d | (1u << PIN_CL2);
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
                sio_hw->gpio_out = d;
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
            }

            // CL1 latch — soak time is the contrast knob for passive LCD
            sio_hw->gpio_out = ctrl | (1u << PIN_CL1);
            delayMicroseconds(50);
            sio_hw->gpio_out = ctrl;

            if (row == 0) sio_hw->gpio_out &= ~(1u << PIN_FLM);
        }

        m_state = !m_state;
        phase   = (phase + 1) & 3;
    }
}

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no bit reflection.
// Process one byte into a running CRC accumulator.
// ---------------------------------------------------------------------------
static uint16_t crc16_update(uint8_t b, uint16_t crc) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Packet receiver state machine
// ---------------------------------------------------------------------------
#define PACKET_TYPE_FRAME  0x01
#define PACKET_TYPE_BOOT   0x02
#define RX_TIMEOUT_MS      400u   // discard partial packet if stalled this long

enum RxState : uint8_t {
    WAIT_SOF_0, WAIT_SOF_1, WAIT_SOF_2, WAIT_SOF_3,  // scan for AA 55 F0 0F
    WAIT_TYPE,                                          // 1 byte
    WAIT_LEN_0, WAIT_LEN_1,                            // 2 bytes LE
    WAIT_CRC_0, WAIT_CRC_1,                            // 2 bytes LE
    RECEIVING,                                          // LEN payload bytes
};

static RxState  rx_state       = WAIT_SOF_0;
static uint8_t  rx_type        = 0;
static uint16_t rx_len         = 0;
static uint16_t rx_crc_exp     = 0;
static uint16_t rx_crc_run     = 0xFFFF;
static uint32_t rx_count       = 0;
static uint32_t rx_start_ms    = 0;

static inline void rx_reset() { rx_state = WAIT_SOF_0; }

static void uartPump() {
    // Abandon a stalled in-progress receive so a fresh packet can resync
    if (rx_state == RECEIVING && (millis() - rx_start_ms) > RX_TIMEOUT_MS) {
        rx_reset();
    }

    while (Serial.available()) {
        const uint8_t b    = (uint8_t)Serial.read();
        const int     back = 1 - front;

        switch (rx_state) {

            // ── SOF scanning ──────────────────────────────────────────────
            case WAIT_SOF_0:
                if (b == 0xAA) rx_state = WAIT_SOF_1;
                break;
            case WAIT_SOF_1:
                if      (b == 0x55) rx_state = WAIT_SOF_2;
                else if (b == 0xAA) rx_state = WAIT_SOF_1;  // re-anchor
                else                rx_reset();
                break;
            case WAIT_SOF_2:
                if      (b == 0xF0) rx_state = WAIT_SOF_3;
                else if (b == 0xAA) rx_state = WAIT_SOF_1;
                else                rx_reset();
                break;
            case WAIT_SOF_3:
                if      (b == 0x0F) rx_state = WAIT_TYPE;
                else if (b == 0xAA) rx_state = WAIT_SOF_1;
                else                rx_reset();
                break;

            // ── Header ────────────────────────────────────────────────────
            case WAIT_TYPE:
                rx_type  = b;
                rx_state = WAIT_LEN_0;
                break;
            case WAIT_LEN_0:
                rx_len   = b;
                rx_state = WAIT_LEN_1;
                break;
            case WAIT_LEN_1:
                rx_len |= (uint16_t)b << 8;
                if (rx_len > LCD_TOTAL_BYTES) { rx_reset(); break; }
                rx_state = WAIT_CRC_0;
                break;
            case WAIT_CRC_0:
                rx_crc_exp = b;
                rx_state   = WAIT_CRC_1;
                break;
            case WAIT_CRC_1: {
                rx_crc_exp |= (uint16_t)b << 8;
                rx_count    = 0;
                rx_crc_run  = 0xFFFF;
                rx_start_ms = millis();

                if (rx_len == 0) {
                    // Zero-length packet: verify CRC now (empty CRC = 0xFFFF)
                    if (rx_crc_run == rx_crc_exp && rx_type == PACKET_TYPE_BOOT) {
                        reset_usb_boot(0, 0);
                    }
                    rx_reset();
                } else {
                    rx_state = RECEIVING;
                }
                break;
            }

            // ── Payload ───────────────────────────────────────────────────
            case RECEIVING: {
                rx_crc_run = crc16_update(b, rx_crc_run);

                const uint32_t phase = rx_count / LCD_BUF_BYTES;
                const uint32_t rem   = rx_count % LCD_BUF_BYTES;
                buf[back][phase][rem / LCD_ROW_BYTES][rem % LCD_ROW_BYTES] = b;

                if (++rx_count >= rx_len) {
                    if (rx_crc_run == rx_crc_exp && rx_type == PACKET_TYPE_FRAME) {
                        front = back;
                    }
                    rx_reset();
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------

void setup() {
    const uint8_t pins[] = {0, 1, 2, 3, 7, 8, 9, 10};
    for (uint8_t pin : pins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    }

    memset(buf, 0, sizeof(buf));
    Serial.begin(1000000);
    multicore_launch_core1(core1_main);
}

void loop() {
    uartPump();
}
