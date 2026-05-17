#include <Arduino.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "../../lm215.h"

// ---------------------------------------------------------------------------
// Pin assignments — ESP32 DevKit V1 right-side header, linear order RX2→D23
// Physical sequence (skipping TX0/RX0/GND): 16,17,5,18,19,21,22,23
// Wired in order to Hitachi pins 8→1 via 74HCT245 buffer.
// ---------------------------------------------------------------------------
#define PIN_D4   16  // Hitachi 8 — lower-right data
#define PIN_D3   17  // Hitachi 7 — upper-right data
#define PIN_CL2   5  // Hitachi 6 — pixel clock
#define PIN_CL1  18  // Hitachi 5 — row latch
#define PIN_M    19  // Hitachi 4 — AC driving signal
#define PIN_FLM  21  // Hitachi 3 — frame start
#define PIN_D2   22  // Hitachi 2 — lower-left data
#define PIN_D1   23  // Hitachi 1 — upper-left data

// CL2 timing.
// ESP32 @ 240MHz: NOP ≈ 4.17ns.  APB GPIO write ≈ 12.5ns.
// LCD_DELAY serves double duty: data setup time (before CL2↑) and CL2 high
// time.  Each is 40 NOPs ≈ 167ns.  Effective CL2 period per nibble:
//   data_write(12) + setup(167) + CL2↑(12) + high(167) + CL2↓(12) ≈ 370ns
// → ~2.7 MHz.  At 240 pulses/row × 64 rows × 4 phases ≈ 22ms/cycle → 45 Hz.
#define NOP10 __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")
#define LCD_DELAY() do { NOP10; NOP10; NOP10; NOP10; } while(0)

#define CL2_MASK  (1u << PIN_CL2)  // GPIO5

// Data pins are non-contiguous: D1=GPIO23, D2=GPIO22, D3=GPIO17, D4=GPIO16.
// Expand a 4-bit nibble {D4,D3,D2,D1} into the GPIO.out bit positions.
static inline uint32_t nib(uint8_t n) {
    return ((uint32_t)(n & 0x1u) << 23)   // D1 bit0 → GPIO23
         | ((uint32_t)(n & 0x2u) << 21)   // D2 bit1 → GPIO22
         | ((uint32_t)(n & 0x4u) << 15)   // D3 bit2 → GPIO17
         | ((uint32_t)(n & 0x8u) << 13);  // D4 bit3 → GPIO16
}

// ---------------------------------------------------------------------------
// ESP32 implementation
// ---------------------------------------------------------------------------
class LM215ESP32 : public LM215Display {
public:
    void begin() override;
    void startRefreshLoop() override;
    void enterBootloader() override { esp_restart(); }

    void refreshLoop();

private:
    volatile bool go_ = false;
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

    // Create at low priority so setup() on the same core (Core 1) is not
    // immediately preempted. The task boosts itself to max priority inside
    // refreshLoop() once startRefreshLoop() signals it is safe to do so.
    xTaskCreatePinnedToCore(
        lcdTaskTrampoline, "lcd",
        4096, this,
        1,          // low — will self-boost after go_ is set
        nullptr, 1  // Core 1: WiFi owns Core 0
    );
}

void LM215ESP32::startRefreshLoop() {
    // Signal the LCD task to leave its wait loop and enter the hot loop.
    // Called at the end of setup() after all buffers and Serial are ready.
    go_ = true;
}

void LM215ESP32::refreshLoop() {
    // Wait until setup() has finished initialising everything, then self-boost
    // to max priority and disable Core 1 WDT before entering the hot loop.
    while (!go_) vTaskDelay(1);

    disableCore1WDT();
    vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);

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

                // Even pulse: low nibble.
                // Data is written while CL2 is LOW so it is stable before the
                // rising edge.  CL2 is toggled separately with w1ts/w1tc so
                // the data pins are never disturbed during the clock pulse.
                GPIO.out = ctrl | nib(byte & 0x0Fu);
                LCD_DELAY();                // data setup time
                GPIO.out_w1ts = CL2_MASK;  // CL2 HIGH — data already stable
                LCD_DELAY();               // CL2 high time
                GPIO.out_w1tc = CL2_MASK;  // CL2 LOW

                // Odd pulse: high nibble.
                GPIO.out = ctrl | nib(byte >> 4);
                LCD_DELAY();                // data setup time
                GPIO.out_w1ts = CL2_MASK;  // CL2 HIGH
                LCD_DELAY();               // CL2 high time
                GPIO.out_w1tc = CL2_MASK;  // CL2 LOW
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
