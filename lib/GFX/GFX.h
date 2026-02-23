#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "LM215.h"

#define GFX_LINES      3
#define GFX_COLS       16
#define GFX_LINE_H     42   // px per line slot (40px char + 2px gap)
#define GFX_CHAR_H     40   // 8 font rows × 5 scale
#define GFX_CHAR_W     30   // 5 font cols × 5 scale + 5px gap
#define GFX_FONT_ROWS  8
#define GFX_FONT_COLS  5
#define GFX_SCALE      5

class GFX {
public:
    static void setText(uint8_t line, const char *str);
    static void refresh();

private:
    static char text[GFX_LINES][GFX_COLS + 1];

    // Render one font row for a text line directly into LM215::dma_buffer.
    static void prerenderRow(uint8_t line, uint8_t font_row);
};

#endif // GFX_H
