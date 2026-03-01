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

#define CL2_MASK  (1u << PIN_CL2)

// ---------------------------------------------------------------------------
// Timing
//
// ESP32 CPU @ 240MHz → 1 cycle ≈ 4.2ns
// GPIO.out writes go through the APB bus @ 80MHz → ~12.5ns per write
//
// LM215 requires CL2 pulse width ≥ 150ns.
// 40 NOPs × 4.2ns ≈ 167ns — pad each half-cycle of CL2 to safely exceed that.
// ---------------------------------------------------------------------------
#define LCD_DELAY() __asm__ __volatile__(        \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
)

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

        // 240 CL2 pulses per row — one pixel per D-pin per pulse
        for (int col = 0; col < 240; col++) {
            uint8_t pixel = 0;

            if (col > 40 && col < 200) {
                if (row > 10 && row < 30) pixel |= 0x01;  // D1 upper-left
                if (row > 35 && row < 55) pixel |= 0x02;  // D2 lower-left
                if (row >  5 && row < 25) pixel |= 0x04;  // D3 upper-right
                if (row > 40 && row < 60) pixel |= 0x08;  // D4 lower-right
            }

            uint32_t out_val = ctrl | ((uint32_t)pixel << 16);

            GPIO.out = out_val | CL2_MASK;
            LCD_DELAY();
            GPIO.out = out_val;
            LCD_DELAY();
        }

        // CL1 latch
        digitalWrite(PIN_CL1, HIGH);
        delayMicroseconds(50);
        digitalWrite(PIN_CL1, LOW);
    }

    m_state = !m_state;
}
