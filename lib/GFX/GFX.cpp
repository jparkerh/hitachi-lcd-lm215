#include "GFX.h"
#include "font5x8.h"
#include <string.h>
#include <avr/pgmspace.h>

char GFX::text[GFX_LINES][GFX_COLS + 1] = { "                ",
                                               "                ",
                                               "                " };

void GFX::setText(uint8_t line, const char *str) {
    if (line >= GFX_LINES) return;

    uint8_t i = 0;
    while (i < GFX_COLS && str[i]) { text[line][i] = str[i]; i++; }
    while (i < GFX_COLS)            { text[line][i] = ' ';    i++; }
    text[line][GFX_COLS] = '\0';

    // Re-render all 8 font rows for this line into dma_buffer
    for (uint8_t fr = 0; fr < GFX_FONT_ROWS; fr++) {
        prerenderRow(line, fr);
    }

    // Clear the 2px gap rows at the bottom of this line slot
    uint8_t gap_y = line * GFX_LINE_H + GFX_CHAR_H;
    for (uint8_t y = gap_y; y < gap_y + (GFX_LINE_H - GFX_CHAR_H); y++) {
        for (uint16_t x = 0; x < LM215_WIDTH; x++) {
            LM215::drawPixel(x, y, false);
        }
    }
}

void GFX::prerenderRow(uint8_t line, uint8_t font_row) {
    uint8_t y_start = line * GFX_LINE_H + font_row * GFX_SCALE;
    const char *str = text[line];

    for (uint8_t dy = 0; dy < GFX_SCALE; dy++) {
        uint8_t y = y_start + dy;

        for (uint16_t x = 0; x < LM215_WIDTH; x++) {
            bool pixel = false;

            uint8_t ch  = x / GFX_CHAR_W;
            uint8_t pos = x % GFX_CHAR_W;

            if (ch < GFX_COLS && pos < (uint8_t)(GFX_FONT_COLS * GFX_SCALE)) {
                uint8_t col      = pos / GFX_SCALE;
                char    c        = str[ch];
                if (c < 0x20 || c > 0x7F) c = ' ';
                uint8_t font_col = pgm_read_byte(&font5x8[c - 0x20][col]);
                pixel = (font_col >> font_row) & 1;
            }

            LM215::drawPixel(x, y, pixel);
        }
    }
}

void GFX::refresh() {
    LM215::refresh();
}
