#pragma once

#include "keycodes.h"

typedef struct KeyEvent {
    Keycode keycode;
    bool pressed;
} KeyEvent;

KeyEvent keyboard_read(void);
