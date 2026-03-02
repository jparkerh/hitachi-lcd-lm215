#include <Arduino.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
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

// CL2 half-cycle delay.
// ESP32 @ 240MHz / APB @ 80MHz: GPIO write ≈ 12.5ns, NOP ≈ 4.17ns.
// Target 3MHz → 333ns period → 167ns half-cycle (≥ 150ns minimum).
// (167 - 12.5) / 4.17 ≈ 37 NOPs → use 40 for margin.
#define NOP10 __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")
#define LCD_DELAY() do { NOP10; NOP10; NOP10; NOP10; } while(0)

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
        gpio_set_drive_capability((gpio_num_t)pin, GPIO_DRIVE_CAP_3);  // 40mA max
    }
    memset(buf, 0, sizeof(buf));

    // The LCD task never yields, so the core 0 idle task never runs.
    // Disable the task watchdog for core 0 to prevent periodic WDT stalls.
    disableCore0WDT();

    // Pin to core 0 — Arduino loop() runs on core 1 by default.
    xTaskCreatePinnedToCore(
        lcdTaskTrampoline, "lcd",
        4096, this,
        configMAX_PRIORITIES - 1,
        nullptr, 0
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

            // CL1 latch — 1μs pulse is well above the minimum setup time
            GPIO.out_w1ts = (1u << PIN_CL1);
            delayMicroseconds(1);
            GPIO.out_w1tc = (1u << PIN_CL1);

            if (row == 0) GPIO.out_w1tc = (1u << PIN_FLM);
        }

        m_state = !m_state;
        phase   = (phase + 1) & 3;
    }
}

LM215Display* createDisplay() { return new LM215ESP32(); }
