#include <Arduino.h>
#include "LM215.h"
#include "GFX.h"

void setup() {
    LM215::init();
    GFX::setText(0, "HELLO WORLD!");
    GFX::setText(1, "LINE TWO HERE");
    GFX::setText(2, "BOTTOM LINE 3");
}

void loop() {
    for (uint8_t i = 0; i < LM215_ROWS; i++) {
        LM215::refresh();
    }
}
