#pragma once

#include "channel.h"

#include <zr/keyboard.h>
#include <zr/mouse.h>

extern Channel *keyboard_key_channel;
extern Channel *mouse_button_channel;
extern Channel *mouse_move_channel;
extern Channel *mouse_scroll_channel;

extern volatile atomic_bool send_input_delayed;

typedef struct InputEvent {
    enum {
        INPUT_EVENT_KEY,
        INPUT_EVENT_MOUSE_BUTTON,
        INPUT_EVENT_MOUSE_MOVE,
        INPUT_EVENT_MOUSE_SCROLL,
    } type;
    union {
        KeyEvent key_event;
        MouseButtonEvent mouse_button_event;
        MouseMoveEvent mouse_move_event;
        MouseScrollEvent mouse_scroll_event;
    };
} InputEvent;

void add_input_event(InputEvent event);
void send_input_events(void);
