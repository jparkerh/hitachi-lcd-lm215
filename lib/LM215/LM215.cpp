#include "LM215.h"
#include <string.h>

uint8_t LM215::left_buf[LM215_ROW_BYTES];
uint8_t LM215::right_buf[LM215_ROW_BYTES];
uint8_t LM215::m_state     = 0;
uint8_t LM215::current_row = 0;

void LM215::init() {
    DDRA  = 0xFF;
    PORTA = 0x00;
    clear();
}

void LM215::clear() {
    memset(left_buf,  0, sizeof(left_buf));
    memset(right_buf, 0, sizeof(right_buf));
}

void LM215::setHalfRowData(uint8_t half, const uint8_t *data) {
    if (half == 0) memcpy(left_buf,  data, LM215_ROW_BYTES);
    else           memcpy(right_buf, data, LM215_ROW_BYTES);
}

uint8_t LM215::getCurrentRow() { return current_row; }

void LM215::refresh() {
    uint8_t m_bit = m_state ? 0x20 : 0x00;  // M = PA5

    // --- Frame start ---
    if (current_row == 0) {
        m_state ^= 1;
        m_bit = m_state ? 0x20 : 0x00;
        PORTA = m_bit | 0x10;               // FLM (PA4) high, 2µs setup
        delayMicroseconds(2);
    }

    uint8_t base = m_bit | (current_row == 0 ? 0x10 : 0x00);  // FLM only on row 0

    // --- Shift 480 pixels: 60 bytes × 8 bits ---
    // D1(PA0) + D2(PA1) driven identically from left_buf MSB  → mask 0x03
    // D3(PA2) + D4(PA3) driven identically from right_buf MSB → mask 0x0C
    // CL2 = PA7 = 0x80
    for (uint8_t i = 0; i < LM215_ROW_BYTES; i++) {
        uint8_t lb = left_buf[i];
        uint8_t rb = right_buf[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t pa = base
                       | ((lb & 0x80) ? 0x03 : 0x00)   // D1 + D2
                       | ((rb & 0x80) ? 0x0C : 0x00);  // D3 + D4
            PORTA = pa;                     // data setup
            __asm__("nop\n\tnop\n\tnop\n\t");
            PORTA = pa | 0x80;              // CL2 high (~187ns)
            __asm__("nop\n\tnop\n\tnop\n\t");
            PORTA = pa;                     // CL2 low (~187ns)
            __asm__("nop\n\tnop\n\tnop\n\t");
            lb <<= 1;
            rb <<= 1;
        }
    }

    // --- CL1 latch (PA6 = 0x40), FLM still high for row 0 ---
    PORTA = base | 0x40;
    delayMicroseconds(2);
    PORTA = m_bit;                          // CL1 low, FLM cleared

    current_row++;
    if (current_row >= LM215_ROWS) current_row = 0;
}
