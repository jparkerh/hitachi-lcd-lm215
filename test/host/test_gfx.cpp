//
// Host-side unit tests for GFX rendering layer.
// Compile: g++ -std=c++11 -Wall -I. -I../../lib/GFX -I../../lib/LM215 test_gfx.cpp -o test_gfx
// Run:     ./test_gfx
//
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// ---------------------------------------------------------------------------
// Mock LM215 — must come before GFX headers so LM215_H guard blocks the real one.
// drawPixel uses the same logic as the real implementation (no hardware registers).
// ---------------------------------------------------------------------------
#define LM215_H
#define LM215_WIDTH     480
#define LM215_HEIGHT    128
#define LM215_ROWS      64
#define LM215_ROW_BYTES 120

static uint8_t mock_row = 0;

class LM215 {
public:
    // Pre-computed register buffer — same layout as hardware implementation
    static uint8_t dma_buffer[LM215_ROWS][LM215_ROW_BYTES];

    static void init()  { memset(dma_buffer, 0, sizeof(dma_buffer)); }
    static void clear() { memset(dma_buffer, 0, sizeof(dma_buffer)); }

    static void drawPixel(uint16_t x, uint8_t y, bool color) {
        if (x >= LM215_WIDTH || y >= LM215_HEIGHT) return;
        uint8_t pulse    = (x < 240) ? (uint8_t)x : (uint8_t)(x - 240);
        uint8_t row      = y % LM215_ROWS;
        uint8_t data_bit = ((x >= 240) ? 2 : 0) | ((y >= 64) ? 1 : 0);
        uint8_t byte_idx = pulse >> 1;
        uint8_t bit_pos  = (pulse & 1) ? (4 + data_bit) : data_bit;
        if (color) dma_buffer[row][byte_idx] |=  (1 << bit_pos);
        else       dma_buffer[row][byte_idx] &= ~(1 << bit_pos);
    }

    static uint8_t getCurrentRow() { return mock_row; }
    static void refresh()          { mock_row = (mock_row + 1) % LM215_ROWS; }
};

uint8_t LM215::dma_buffer[LM215_ROWS][LM215_ROW_BYTES] = {{0}};

// ---------------------------------------------------------------------------
// Include GFX (font5x8 uses mock avr/pgmspace.h from test/host/avr/pgmspace.h)
// ---------------------------------------------------------------------------
#include "../../lib/GFX/font5x8.h"
#include "../../lib/GFX/GFX.h"
#include "../../lib/GFX/GFX.cpp"

// ---------------------------------------------------------------------------
// Helpers — read pixels using (x, display_row) from dma_buffer
// ---------------------------------------------------------------------------
static bool get_pixel(int x, int display_row) {
    if (x >= 480 || display_row >= 128) return false;
    uint8_t pulse    = (x < 240) ? (uint8_t)x : (uint8_t)(x - 240);
    uint8_t row      = display_row % 64;
    uint8_t data_bit = ((x >= 240) ? 2 : 0) | ((display_row >= 64) ? 1 : 0);
    uint8_t byte_idx = pulse >> 1;
    uint8_t bit_pos  = (pulse & 1) ? (4 + data_bit) : data_bit;
    return (LM215::dma_buffer[row][byte_idx] >> bit_pos) & 1;
}

static bool row_is_blank(int display_row) {
    for (int x = 0; x < 480; x++)
        if (get_pixel(x, display_row)) return false;
    return true;
}

static bool rows_equal(int a, int b) {
    for (int x = 0; x < 480; x++)
        if (get_pixel(x, a) != get_pixel(x, b)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static int tests_run = 0, tests_failed = 0;

#define EXPECT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { printf("FAIL [line %d]: %s\n", __LINE__, (msg)); tests_failed++; } \
    else         { printf("PASS: %s\n", (msg)); } \
} while(0)

static void test_gap_rows_are_blank() {
    EXPECT(row_is_blank(40),  "Line 0 gap row 40 is blank");
    EXPECT(row_is_blank(41),  "Line 0 gap row 41 is blank");
    EXPECT(row_is_blank(82),  "Line 1 gap row 82 is blank");
    EXPECT(row_is_blank(83),  "Line 1 gap row 83 is blank");
    EXPECT(row_is_blank(124), "Line 2 gap row 124 is blank");
    EXPECT(row_is_blank(125), "Line 2 gap row 125 is blank");
    EXPECT(row_is_blank(126), "Row 126 (beyond lines) is blank");
    EXPECT(row_is_blank(127), "Row 127 (beyond lines) is blank");
}

static void test_same_font_row_identical() {
    for (int base = 0; base < 40; base += 5) {
        for (int off = 1; off < 5; off++) {
            char msg[80];
            snprintf(msg, sizeof(msg), "Line0 rows %d and %d identical (font_row %d)",
                     base, base + off, base / 5);
            EXPECT(rows_equal(base, base + off), msg);
        }
    }
}

static void test_different_font_rows_differ() {
    for (int fr = 0; fr < 7; fr++) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Line0 font_row %d (row %d) != font_row %d (row %d)",
                 fr, fr*5, fr+1, (fr+1)*5);
        EXPECT(!rows_equal(fr * 5, (fr + 1) * 5), msg);
    }
}

static void test_line_boundary() {
    EXPECT(!row_is_blank(30),  "font_row 6 of line 0 has content");
    EXPECT(!row_is_blank(42),  "First row of line 1 has content");
    EXPECT(!rows_equal(0, 42), "Line 0 and line 1 content differs");
}

static void test_active_rows_not_blank() {
    // Skip font_row 7 rows (35-39, 77-81, 119-123) — blank for these strings
    int samples[] = {0, 5, 10, 20, 30, 42, 50, 60, 84, 90, 100, 110, 114};
    for (int i = 0; i < 13; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Active display row %d has lit pixels", samples[i]);
        EXPECT(!row_is_blank(samples[i]), msg);
    }
}

static void print_font_row_breakdown() {
    printf("\n--- Font row breakdown for line 0 (display rows 0-41) ---\n");
    for (int row = 0; row < 42; row++) {
        int ril      = row % 42;
        int font_row = (ril < 40) ? ril / 5 : -1;
        int lit = 0;
        for (int x = 0; x < 480; x++) if (get_pixel(x, row)) lit++;
        printf("  disp_row=%3d  font_row=%2d  lit_px=%d%s\n",
               row, font_row, lit, font_row < 0 ? "  [gap]" : "");
    }
}

static void print_display() {
    printf("\n=== Display (480x128, sample every 3rd pixel) ===\n");
    for (int row = 0; row < 128; row++) {
        printf("%3d|", row);
        for (int x = 0; x < 480; x += 3)
            printf("%c", get_pixel(x, row) ? '#' : '.');
        printf("|\n");
    }
    printf("=================================================\n\n");
}

int main() {
    LM215::clear();
    GFX::setText(0, "HELLO WORLD!");
    GFX::setText(1, "LINE TWO HERE");
    GFX::setText(2, "BOTTOM LINE 3");

    printf("=== GFX Unit Tests ===\n\n");
    test_gap_rows_are_blank();
    test_same_font_row_identical();
    test_different_font_rows_differ();
    test_line_boundary();
    test_active_rows_not_blank();
    print_font_row_breakdown();
    print_display();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
