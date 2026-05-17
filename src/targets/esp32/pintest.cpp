#include <Arduino.h>

// Linear ESP32 header order RX2→D23: GPIO16,17,5,18,19,21,22,23
// Wired to Hitachi pins 8→1 in that order via 74HCT245 buffer.
// buf_pin  hitachi_pin  signal     esp32_gpio
static const struct { uint8_t pin; const char* name; } PINS[] = {
    { 16, "buf:11 → Hitachi:8  D4  (lower-right data)" },
    { 17, "buf:12 → Hitachi:7  D3  (upper-right data)" },
    {  5, "buf:13 → Hitachi:6  CL2 (pixel clock)"      },
    { 18, "buf:14 → Hitachi:5  CL1 (row latch)"        },
    { 19, "buf:15 → Hitachi:4  M   (AC drive)"         },
    { 21, "buf:16 → Hitachi:3  FLM (frame start)"      },
    { 22, "buf:17 → Hitachi:2  D2  (lower-left  data)" },
    { 23, "buf:18 → Hitachi:1  D1  (upper-left  data)" },
};
static const int N = sizeof(PINS) / sizeof(PINS[0]);

void setup() {
    Serial.begin(115200);
    for (int i = 0; i < N; i++)
        pinMode(PINS[i].pin, OUTPUT);
    Serial.println("Pin test — cycling each LCD signal HIGH for 1s");
}

void loop() {
    for (int i = 0; i < N; i++) {
        // all LOW, then raise just this one
        for (int j = 0; j < N; j++) digitalWrite(PINS[j].pin, LOW);
        digitalWrite(PINS[i].pin, HIGH);
        Serial.printf("GPIO %2d  HIGH  →  %s\n", PINS[i].pin, PINS[i].name);
        delay(5000);
    }
    // brief all-LOW gap between cycles
    for (int j = 0; j < N; j++) digitalWrite(PINS[j].pin, LOW);
    Serial.println("--- all LOW ---");
    delay(500);
}
