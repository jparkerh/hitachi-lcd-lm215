#include <Arduino.h>

void setup() {
  DDRA = 0xFF; // Set Mega Pins 22-29 (PORTA) as outputs
}

void loop() {
  // 1. Frame Start (FLM)
  // Hitachi Pin 3 (PA4) goes High
  PORTA = 0b00010000; 
  
  for (int row = 0; row < 64; row++) {
    // Determine if this row should be Black or White
    // Every 4 rows, we switch the state.
    bool barState = (row / 4) % 2;

    // 2. Shift in 240 bits of data for this row
    for (int col = 0; col < 240; col++) {
      if (barState) {
        // CL2 (Pin 6/PA7) High + D1-D4 (Pins 1,2,7,8/PA0-PA3) High
        PORTA |= 0b10001111; 
        __asm__("nop\n\tnop\n\t");
        PORTA &= ~0b10000000; // CL2 Low
      } else {
        // CL2 (Pin 6/PA7) High + D1-D4 Low
        PORTA = (PORTA & 0xF0) | 0b10000000;
        __asm__("nop\n\tnop\n\t");
        PORTA &= ~0b10000000; // CL2 Low
      }
    }

    // 3. Latch the row (CL1)
    PORTA |= 0b01000000;
    delayMicroseconds(5); // Latch "soak" time for brightness
    PORTA &= ~0b01000000;

    // 4. Clear FLM after the first row latch
    if (row == 0) PORTA &= ~0b00010000;
  }

  // 5. Toggle AC Drive (M) every frame
  static bool m_signal = false;
  m_signal = !m_signal;
  if (m_signal) PORTA |= 0b00100000;
  else PORTA &= ~0b00100000;
}