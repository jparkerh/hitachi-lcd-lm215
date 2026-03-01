#include <Arduino.h>
#include "lm215.h"

// ---------------------------------------------------------------------------
// Wire protocol — framed packet format:
//   [SOF: AA 55 F0 0F] [TYPE: 1] [LEN: 2 LE] [CRC16: 2 LE] [PAYLOAD: LEN]
//   TYPE 0x01 = full frame (LEN = 30720)
//   TYPE 0x02 = reboot to bootloader (LEN = 0)
//   CRC = CRC-16/CCITT-FALSE over payload (poly 0x1021, init 0xFFFF)
// ---------------------------------------------------------------------------
#define PACKET_TYPE_FRAME  0x01
#define PACKET_TYPE_BOOT   0x02
#define RX_TIMEOUT_MS      400u

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no bit reflection.
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
enum RxState : uint8_t {
    WAIT_SOF_0, WAIT_SOF_1, WAIT_SOF_2, WAIT_SOF_3,
    WAIT_TYPE,
    WAIT_LEN_0, WAIT_LEN_1,
    WAIT_CRC_0, WAIT_CRC_1,
    RECEIVING,
};

static RxState  rx_state    = WAIT_SOF_0;
static uint8_t  rx_type     = 0;
static uint16_t rx_len      = 0;
static uint16_t rx_crc_exp  = 0;
static uint16_t rx_crc_run  = 0xFFFF;
static uint32_t rx_count    = 0;
static uint32_t rx_start_ms = 0;

static LM215Display* display = nullptr;

static inline void rx_reset() { rx_state = WAIT_SOF_0; }

static void uartPump() {
    if (rx_state == RECEIVING && (millis() - rx_start_ms) > RX_TIMEOUT_MS) {
        rx_reset();
    }

    while (Serial.available()) {
        const uint8_t b = (uint8_t)Serial.read();

        switch (rx_state) {

            case WAIT_SOF_0:
                if (b == 0xAA) rx_state = WAIT_SOF_1;
                break;
            case WAIT_SOF_1:
                if      (b == 0x55) rx_state = WAIT_SOF_2;
                else if (b == 0xAA) rx_state = WAIT_SOF_1;
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
                    if (rx_crc_run == rx_crc_exp && rx_type == PACKET_TYPE_BOOT) {
                        display->enterBootloader();
                    }
                    rx_reset();
                } else {
                    rx_state = RECEIVING;
                }
                break;
            }

            case RECEIVING: {
                rx_crc_run = crc16_update(b, rx_crc_run);
                display->writeByte(rx_count, b);

                if (++rx_count >= rx_len) {
                    if (rx_crc_run == rx_crc_exp && rx_type == PACKET_TYPE_FRAME) {
                        display->commitFrame();
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
    display = createDisplay();
    display->begin();
    Serial.begin(1000000);
}

void loop() {
    uartPump();
}
