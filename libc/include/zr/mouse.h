#pragma once

#include <zr/types.h>

typedef enum MouseButton : u8 {
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
} MouseButton;

typedef struct MouseButtonEvent {
    MouseButton button;
    bool pressed;
} MouseButtonEvent;

typedef struct MouseMoveEvent {
    i32 diff_x;
    i32 diff_y;
    i32 abs_x;
    i32 abs_y;
} MouseMoveEvent;

typedef struct MouseScrollEvent {
    i32 diff;
} MouseScrollEvent;
