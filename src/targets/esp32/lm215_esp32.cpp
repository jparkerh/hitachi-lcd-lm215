#include <Arduino.h>
#include "soc/gpio_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "../../lm215.h"

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

// ESP32 CPU @ 240MHz → 1 cycle ≈ 4.2ns
// GPIO.out writes go through APB bus @ 80MHz → ~12.5ns per write
// LM215 requires CL2 pulse width ≥ 150ns.
// 40 NOPs × 4.2ns ≈ 167ns — pad each CL2 half-cycle to safely exceed that.
#define LCD_DELAY() __asm__ __volatile__(        \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
    "nop; nop; nop; nop; nop; nop; nop; nop;\n" \
)

#define CL2_MASK  (1u << PIN_CL2)

// ---------------------------------------------------------------------------
// ESP32 implementation
// ---------------------------------------------------------------------------
class LM215ESP32 : public LM215Display {
public:
    void begin() override;
    void enterBootloader() override { esp_restart(); }

    void refreshLoop();
};

static void lcdTaskTrampoline(void* arg) {
    static_cast<LM215ESP32*>(arg)->refreshLoop();
}

void LM215ESP32::begin() {
    const uint8_t pins[] = {PIN_D1, PIN_D2, PIN_D3, PIN_D4,
                            PIN_M, PIN_CL2, PIN_CL1, PIN_FLM};
    for (uint8_t pin : pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    memset(buf, 0, sizeof(buf));
    xTaskCreatePinnedToCore(
        lcdTaskTrampoline, "lcd",
        4096, this,
        configMAX_PRIORITIES - 1,
        nullptr, 1
    );
}

void LM215ESP32::refreshLoop() {
    bool    m_state = false;
    uint8_t phase   = 0;

    while (true) {
        const int b = front;

        for (int row = 0; row < LCD_ROWS; row++) {
            uint32_t ctrl = m_state ? (1u << PIN_M) : 0u;
            if (row == 0) ctrl |= (1u << PIN_FLM);

            const uint8_t* rowbuf = buf[b][phase][row];

            for (int col = 0; col < LCD_ROW_BYTES; col++) {
                const uint8_t byte = rowbuf[col];

                // Even pulse: low nibble
                uint32_t d = ctrl | ((uint32_t)(byte & 0x0Fu) << 16);
                GPIO.out = d | CL2_MASK;
                LCD_DELAY();
                GPIO.out = d;
                LCD_DELAY();

                // Odd pulse: high nibble
                d = ctrl | ((uint32_t)(byte >> 4) << 16);
                GPIO.out = d | CL2_MASK;
                LCD_DELAY();
                GPIO.out = d;
                LCD_DELAY();
            }

            // CL1 latch
            GPIO.out_w1ts = (1u << PIN_CL1);
            delayMicroseconds(50);
            GPIO.out_w1tc = (1u << PIN_CL1);

            if (row == 0) GPIO.out_w1tc = (1u << PIN_FLM);
        }

        m_state = !m_state;
        phase   = (phase + 1) & 3;
    }
}

LM215Display* createDisplay() { return new LM215ESP32(); }
