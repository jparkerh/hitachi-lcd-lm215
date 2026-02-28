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
//   Wire format: [0xAB 0xCF] + 30720 bytes
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
// UART bridge — simplified: full 30720-byte frame only, no delta
//
// Wire format: 0xAB 0xCF [30720 bytes: phase0[7680] phase1[7680] phase2[7680] phase3[7680]]
// ---------------------------------------------------------------------------
static uint32_t rx_count = 0;

enum RxState : uint8_t { WAIT_MAGIC_0, WAIT_MAGIC_1, RECEIVING };
static RxState rx_state = WAIT_MAGIC_0;

static void uartPump() {
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        const int back = 1 - front;

        switch (rx_state) {
            case WAIT_MAGIC_0:
                if (b == 0xAB) rx_state = WAIT_MAGIC_1;
                break;
            case WAIT_MAGIC_1:
                if      (b == 0xCF) { rx_count = 0; rx_state = RECEIVING; }
                else if (b == 0xBB) { reset_usb_boot(0, 0); }  // → BOOTSEL
                else { rx_state = WAIT_MAGIC_0; if (b == 0xAB) rx_state = WAIT_MAGIC_1; }
                break;
            case RECEIVING: {
                // Decode flat offset into [phase][row][col]
                const uint32_t phase = rx_count / LCD_BUF_BYTES;
                const uint32_t rem   = rx_count % LCD_BUF_BYTES;
                buf[back][phase][rem / LCD_ROW_BYTES][rem % LCD_ROW_BYTES] = b;
                if (++rx_count >= LCD_TOTAL_BYTES) {
                    front    = back;
                    rx_state = WAIT_MAGIC_0;
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
