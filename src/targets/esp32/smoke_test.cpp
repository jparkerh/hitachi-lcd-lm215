#include <Arduino.h>
#include "soc/gpio_struct.h"  // GPIO.out register

// ---------------------------------------------------------------------------
// Pin assignments — ESP32 DevKit V1 (right-side header)
// ---------------------------------------------------------------------------
#define PIN_D1   16  // Serial data: upper-left quadrant
#define PIN_D2   17  // Serial data: lower-left quadrant
#define PIN_D3   18  // Serial data: upper-right quadrant
#define PIN_D4   19  // Serial data: lower-right quadrant
#define PIN_M     5  // AC driving signal
#define PIN_CL2  21  // Shift clock (pixel clock)
#define PIN_CL1  22  // Data latch (row clock)
#define PIN_FLM  23  // Frame start

// Data bits sit at GPIO 16-19 → bits 16-19 of GPIO.out
#define CL2_MASK  (1u << PIN_CL2)

// ---------------------------------------------------------------------------
// Smoke test pattern: four filled bars, one per quadrant
//
//   D1 (upper-left):  rows 10-29
//   D2 (lower-left):  rows 35-54
//   D3 (upper-right): rows  5-24
//   D4 (lower-right): rows 40-59
//   All bars span columns 21-99 of the 120-shift row.
// ---------------------------------------------------------------------------

void setup() {
    const int pins[] = {PIN_D1, PIN_D2, PIN_D3, PIN_D4,
                        PIN_M, PIN_CL2, PIN_CL1, PIN_FLM};
    for (int p : pins) {
        pinMode(p, OUTPUT);
        digitalWrite(p, LOW);
    }
}

void loop() {
    static bool m_state = false;

    for (int row = 0; row < 64; row++) {
        uint32_t ctrl = m_state ? (1u << PIN_M) : 0u;
        if (row == 0) ctrl |= (1u << PIN_FLM);

        for (int col = 0; col < 120; col++) {
            uint8_t nibble = 0;

            if (col > 20 && col < 100) {
                if (row > 10 && row < 30) nibble |= 0x01;  // D1 upper-left
                if (row > 35 && row < 55) nibble |= 0x02;  // D2 lower-left
                if (row >  5 && row < 25) nibble |= 0x04;  // D3 upper-right
                if (row > 40 && row < 60) nibble |= 0x08;  // D4 lower-right
            }

            uint32_t out_val = ctrl | ((uint32_t)nibble << 16);

            GPIO.out = out_val | CL2_MASK;
            __asm__ __volatile__("nop; nop;");
            GPIO.out = out_val;
            __asm__ __volatile__("nop; nop;");
        }

        // CL1 latch
        digitalWrite(PIN_CL1, HIGH);
        delayMicroseconds(25);
        digitalWrite(PIN_CL1, LOW);
    }

    m_state = !m_state;
}
