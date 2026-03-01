#include <Arduino.h>
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "../../lm215.h"

// ---------------------------------------------------------------------------
// Pin assignments — Adafruit Feather RP2040
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
// RP2040 implementation
// ---------------------------------------------------------------------------
class LM215RP2040 : public LM215Display {
public:
    void begin() override;
    void enterBootloader() override { reset_usb_boot(0, 0); }

    // Called on core 1 — owns the LCD refresh loop entirely.
    void refreshLoop();
};

// Static pointer so the core1 trampoline can reach the instance.
static LM215RP2040* s_instance;
static void core1_trampoline() { s_instance->refreshLoop(); }

void LM215RP2040::begin() {
    const uint8_t pins[] = {PIN_D1, PIN_D2, PIN_D3, PIN_D4,
                            PIN_FLM, PIN_M, PIN_CL1, PIN_CL2};
    for (uint8_t pin : pins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    }
    memset(buf, 0, sizeof(buf));
    s_instance = this;
    multicore_launch_core1(core1_trampoline);
}

void LM215RP2040::refreshLoop() {
    // RP2040 @ 125MHz = 8ns/cycle; 20 NOPs ≈ 160ns ≥ 150ns CL2 minimum
    #define NOP20 \
        __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n" \
                       "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

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
                NOP20;
                sio_hw->gpio_out = d;
                NOP20;

                // Odd pulse: high nibble
                d = ctrl | (byte >> 4);
                sio_hw->gpio_out = d | (1u << PIN_CL2);
                NOP20;
                sio_hw->gpio_out = d;
                NOP20;
            }

            // CL1 latch
            sio_hw->gpio_out |= (1u << PIN_CL1);
            delayMicroseconds(50);
            sio_hw->gpio_out &= ~(1u << PIN_CL1);

            if (row == 0) sio_hw->gpio_out &= ~(1u << PIN_FLM);
        }

        m_state = !m_state;
        phase   = (phase + 1) & 3;
    }

    #undef NOP20
}

LM215Display* createDisplay() { return new LM215RP2040(); }
