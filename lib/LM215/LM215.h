#ifndef LM215_H
#define LM215_H

#include <Arduino.h>
#include <stdint.h>

#define LM215_WIDTH     480
#define LM215_ROWS      64
#define LM215_ROW_BYTES 60   // 480 pixels / 8 bits

// All LCD signals on PORTA (Mega pins 22-29):
//   PA0=D1  PA1=D2  PA2=D3  PA3=D4
//   PA4=FLM PA5=M   PA6=CL1 PA7=CL2

class LM215 {
public:
    static void init();
    static void clear();

    // half 0 = left half (D1/D2 = PA0/PA1), half 1 = right half (D3/D4 = PA2/PA3)
    // 60 bytes, MSB = first pixel
    static void setHalfRowData(uint8_t half, const uint8_t *data);

    // Stub — not implemented in this architecture; GFX must use setHalfRowData instead.
    static void drawPixel(uint16_t x, uint8_t y, bool color) { (void)x; (void)y; (void)color; }

    static uint8_t getCurrentRow();
    static void refresh();   // drives one row; call 64x per frame

private:
    static uint8_t left_buf[LM215_ROW_BYTES];
    static uint8_t right_buf[LM215_ROW_BYTES];
    static uint8_t m_state;
    static uint8_t current_row;
};

#endif // LM215_H
