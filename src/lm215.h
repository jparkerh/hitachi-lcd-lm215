#pragma once
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// LCD buffer constants
//   4 dither phases × 64 rows × 120 bytes = 30720 bytes per display frame
//   Each row: 240 CL2 pulses, nibble-packed 2/byte → 120 bytes
// ---------------------------------------------------------------------------
#define LCD_ROWS        64
#define LCD_ROW_BYTES   120
#define LCD_PHASES      4
#define LCD_BUF_BYTES   (LCD_ROWS * LCD_ROW_BYTES)            // 7680
#define LCD_TOTAL_BYTES (LCD_PHASES * LCD_BUF_BYTES)          // 30720

// ---------------------------------------------------------------------------
// LM215Display — abstract interface for the LM215 480×128 passive LCD.
//
// Subclasses provide:
//   begin()          — GPIO init + start the refresh loop on a second core/task
//   enterBootloader() — platform reboot/bootloader entry
//
// The shared packet receiver calls writeByte() and commitFrame() directly.
// ---------------------------------------------------------------------------
class LM215Display {
public:
    virtual ~LM215Display() = default;

    // Initialise GPIO and launch the refresh loop.
    virtual void begin() = 0;

    // Reboot / restart the device.
    virtual void enterBootloader() = 0;

    // Release the refresh loop to run at full priority.
    // Call at the end of setup() after all hardware/buffer init is done.
    // Default no-op — task creation and loop launch are decoupled on ESP32.
    virtual void startRefreshLoop() {}

    // Write one payload byte at the given flat offset into the back buffer.
    // Offset maps to [phase][row][col] using the standard LCD_BUF_BYTES layout.
    void writeByte(uint32_t offset, uint8_t b) {
        const int     bk    = 1 - front;
        const uint32_t phase = offset / LCD_BUF_BYTES;
        const uint32_t rem   = offset % LCD_BUF_BYTES;
        buf[bk][phase][rem / LCD_ROW_BYTES][rem % LCD_ROW_BYTES] = b;
    }

    // Atomically swap back buffer to front — called after a verified frame.
    void commitFrame() { front = 1 - front; }

protected:
    uint8_t      buf[2][LCD_PHASES][LCD_ROWS][LCD_ROW_BYTES];
    volatile int front = 0;
};

// Implemented in the platform-specific translation unit.
LM215Display* createDisplay();
