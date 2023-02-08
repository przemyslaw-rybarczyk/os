#pragma once

#include "types.h"

typedef struct MouseUpdate {
    i32 diff_x;
    i32 diff_y;
    i32 diff_scroll;
    bool left_button_pressed;
    bool right_button_pressed;
    bool middle_button_pressed;
} MouseUpdate;

MouseUpdate mouse_get_update(void);
