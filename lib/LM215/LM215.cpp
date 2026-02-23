#include "LM215.h"
#include <string.h>

uint8_t LM215::dma_buffer[LM215_ROWS][LM215_ROW_BYTES];
uint8_t LM215::m_state     = 0;
uint8_t LM215::current_row = 0;

void LM215::init() {
    DDRA  = 0xFF;
    PORTA = 0x00;
    clear();
}

void LM215::clear() {
    memset(dma_buffer, 0, sizeof(dma_buffer));
}

void LM215::drawPixel(uint16_t x, uint8_t y, bool color) {
    if (x >= LM215_WIDTH || y >= LM215_HEIGHT) return;
    uint8_t pulse    = (x < 240) ? (uint8_t)x : (uint8_t)(x - 240);
    uint8_t row      = y % LM215_ROWS;
    uint8_t data_bit = ((x >= 240) ? 2 : 0) | ((y >= 64) ? 1 : 0);
    uint8_t byte_idx = pulse >> 1;
    uint8_t bit_pos  = (pulse & 1) ? (4 + data_bit) : data_bit;
    if (color) dma_buffer[row][byte_idx] |=  (1 << bit_pos);
    else       dma_buffer[row][byte_idx] &= ~(1 << bit_pos);
}

uint8_t LM215::getCurrentRow() { return current_row; }

void LM215::refresh() {
    // --- Frame start ---
    if (current_row == 0) {
        m_state ^= 1;
        if (m_state) PORTA |=  0x20; else PORTA &= ~0x20;  // toggle M (PA5)
        PORTA |= 0x10;                                       // FLM high (PA4)
        delayMicroseconds(2);
    }

    const uint8_t *row_buf = dma_buffer[current_row];

    for (uint8_t b = 0; b < LM215_ROW_BYTES; b++) {
        uint8_t byte = row_buf[b];

        // Even pulse: lo nibble
        PORTA = (PORTA & 0xF0) | (byte & 0x0F);  // set D1-D4, preserve M/FLM/CL1/CL2
        PORTA |= 0x80;                             // CL2 high
        __asm__("nop\n\tnop\n\tnop\n\t");
        PORTA &= ~0x80;                            // CL2 low

        // Odd pulse: hi nibble
        PORTA = (PORTA & 0xF0) | (byte >> 4);
        PORTA |= 0x80;                             // CL2 high
        __asm__("nop\n\tnop\n\tnop\n\t");
        PORTA &= ~0x80;                            // CL2 low
    }

    // --- CL1 latch ---
    PORTA |= 0x40;
    delayMicroseconds(5);
    PORTA &= ~0x40;

    // --- Clear FLM after row 0 latch ---
    if (current_row == 0) PORTA &= ~0x10;

    current_row++;
    if (current_row >= LM215_ROWS) current_row = 0;
}
