#include <Arduino.h>
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"

// ---------------------------------------------------------------------------
// Pin assignments
// Data (GPIO 0-3): D5, D6, D9, D10 on Feather header → TX, RX, SDA, SCL pads
// Control (GPIO 7-10): broken out as D5, D6, D9, D10
// ---------------------------------------------------------------------------
#define PIN_D1   0   // Serial data: upper-left quadrant
#define PIN_D2   1   // Serial data: lower-left quadrant
#define PIN_D3   2   // Serial data: upper-right quadrant
#define PIN_D4   3   // Serial data: lower-right quadrant
#define PIN_FLM  7   // Frame start
#define PIN_M    8   // AC driving signal
#define PIN_CL1  9   // Data latch (row clock)
#define PIN_CL2  10  // Shift clock (pixel clock)

// Non-contiguous mask: bits 0-3 and 7-10
#define LCD_MASK ((0xFu) | (0xFu << 7))

// ---------------------------------------------------------------------------
// Double buffer
//
// Each row = 60 CL2 pulses; packed 2 pulses per byte (low nibble first):
//   byte bits [3:0] → pulse N   (D1-D4)
//   byte bits [7:4] → pulse N+1 (D1-D4)
// 60 pulses / 2 = 30 bytes per row
// ---------------------------------------------------------------------------
#define LCD_ROWS      64
#define LCD_ROW_BYTES 120  // 240 pulses × 4 bits, packed 2 pulses/byte (nibble each) = 7680 bytes total, matches Mega

static uint8_t buf[2][LCD_ROWS][LCD_ROW_BYTES];
static volatile int front = 0;  // core1 always displays buf[front]

// ---------------------------------------------------------------------------
// Core 1 — LCD refresh loop, inline pattern (no buffer, for diagnostics)
// Mirrors the user's example code exactly, with corrected NOP count.
// RP2040 @ 125MHz = 8ns/cycle; need ≥150ns → 20 NOPs ≈ 160ns
// ---------------------------------------------------------------------------
static void core1_main() {
    bool m_state = false;

    while (true) {
        for (int row = 0; row < LCD_ROWS; row++) {
            uint32_t ctrl = m_state ? (1u << PIN_M) : 0u;
            if (row == 0) ctrl |= (1u << PIN_FLM);

            for (int col = 0; col < 240; col++) {
                // Vertical bars every 20 columns (matches 10-column bands at full 240-pulse width)
                uint32_t data = ((col / 20) % 2) ? 0x0Fu : 0x00u;
                sio_hw->gpio_out = ctrl | data | (1u << PIN_CL2);
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
                sio_hw->gpio_out = ctrl | data;
                __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                               "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
            }

            // CL1 latch — hold 25µs
            sio_hw->gpio_out = ctrl | (1u << PIN_CL1);
            delayMicroseconds(25);
            sio_hw->gpio_out = ctrl;
        }

        m_state = !m_state;
    }
}

// ---------------------------------------------------------------------------
// Core 0 helpers
// ---------------------------------------------------------------------------
static inline int back_buf()    { return 1 - front; }
static inline void swap_buffers() { front = 1 - front; }

// ---------------------------------------------------------------------------

void setup() {
    const uint8_t pins[] = {0, 1, 2, 3, 7, 8, 9, 10};
    for (uint8_t pin : pins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    }

    multicore_launch_core1(core1_main);
}

void loop() {
    // Core 1 owns the LCD; nothing to do on core 0 yet.
    delay(1000);
}
