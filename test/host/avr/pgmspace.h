#pragma once
// Mock avr/pgmspace.h for host builds
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
