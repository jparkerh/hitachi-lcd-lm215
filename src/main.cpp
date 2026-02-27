#include <Arduino.h>
#include "LM215.h"
#include "GFX.h"

// ---------------------------------------------------------------------------
// UART → dma_buffer bridge
//
// Full frame:  0xAB 0xCD [7680 bytes, row-major]
// Delta frame: 0xAB 0xCE [uint16 count LE] [count × (row uint8, col uint8, val uint8)]
// ---------------------------------------------------------------------------

static uint16_t rx_count  = 0;
static uint8_t  delta_row = 0;
static uint8_t  delta_col = 0;

enum RxState : uint8_t {
    WAIT_MAGIC_0,
    WAIT_MAGIC_1,
    RECEIVING_FULL,
    DELTA_COUNT_LO,
    DELTA_COUNT_HI,
    DELTA_ROW,
    DELTA_COL,
    DELTA_VAL,
};
static RxState rx_state = WAIT_MAGIC_0;

static void uartPump() {
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        switch (rx_state) {
            case WAIT_MAGIC_0:
                if (b == 0xAB) rx_state = WAIT_MAGIC_1;
                break;
            case WAIT_MAGIC_1:
                if      (b == 0xCD) { rx_count = 0; rx_state = RECEIVING_FULL; }
                else if (b == 0xCE) { rx_state = DELTA_COUNT_LO; }
                else { rx_state = WAIT_MAGIC_0; if (b == 0xAB) rx_state = WAIT_MAGIC_1; }
                break;

            // --- Full frame ---
            case RECEIVING_FULL:
                LM215::dma_buffer[rx_count / LM215_ROW_BYTES][rx_count % LM215_ROW_BYTES] = b;
                if (++rx_count >= (uint16_t)LM215_ROWS * LM215_ROW_BYTES)
                    rx_state = WAIT_MAGIC_0;
                break;

            // --- Delta frame ---
            case DELTA_COUNT_LO:
                rx_count = b; rx_state = DELTA_COUNT_HI;
                break;
            case DELTA_COUNT_HI:
                rx_count |= ((uint16_t)b << 8);
                rx_state = (rx_count > 0) ? DELTA_ROW : WAIT_MAGIC_0;
                break;
            case DELTA_ROW:
                delta_row = b; rx_state = DELTA_COL;
                break;
            case DELTA_COL:
                delta_col = b; rx_state = DELTA_VAL;
                break;
            case DELTA_VAL:
                if (delta_row < LM215_ROWS && delta_col < LM215_ROW_BYTES)
                    LM215::dma_buffer[delta_row][delta_col] = b;
                rx_state = (--rx_count > 0) ? DELTA_ROW : WAIT_MAGIC_0;
                break;
        }
    }
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    LM215::init();
    GFX::setText(0, F("WAITING..."));
}

void loop() {
    for (uint8_t i = 0; i < LM215_ROWS; i++) {
        LM215::refresh();
    }
    uartPump(); // pump after full frame to avoid mid-frame tearing
}
