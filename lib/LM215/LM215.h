#ifndef LM215_H
#define LM215_H

#include <Arduino.h>
#include <stdint.h>

#define LM215_WIDTH     480
#define LM215_HEIGHT    128
#define LM215_ROWS      64
#define LM215_ROW_BYTES  120  // 240 CL2 pulses / 2 nibbles per byte
#define LM215_CL1_US    15  // CL1 latch soak time in µs — critical for pixel darkening

// All LCD signals on PORTA (Mega pins 22-29):
//   PA0=D1  PA1=D2  PA2=D3  PA3=D4
//   PA4=FLM PA5=M   PA6=CL1 PA7=CL2

class LM215 {
public:
    static void init();
    static void clear();

    // Set a single pixel. x: 0-479, y: 0-127.
    static void drawPixel(uint16_t x, uint8_t y, bool color);

    static uint8_t getCurrentRow();
    static void refresh();  // drives one row; call 64x per frame

    // dma_buffer[row][byte]: two nibbles per byte, each nibble = {D4,D3,D2,D1}
    // even pulse k uses lo nibble of byte k/2; odd pulse uses hi nibble
    static uint8_t dma_buffer[LM215_ROWS][LM215_ROW_BYTES];

private:
    static uint8_t m_state;
    static uint8_t current_row;
};

#endif // LM215_H
