#include <types.h>

#include <stdlib.h>
#include <string.h>
#include <syscalls.h>

typedef struct ScreenSize {
    size_t width;
    size_t height;
} ScreenSize;

typedef struct KeyEvent {
    u8 keycode;
    bool pressed;
} KeyEvent;

typedef struct MouseUpdate {
    i32 diff_x;
    i32 diff_y;
    i32 diff_scroll;
    bool left_button_pressed;
    bool right_button_pressed;
    bool middle_button_pressed;
} MouseUpdate;

ScreenSize screen_size;

#define COLORS_NUM 12

static u8 colors[COLORS_NUM][3] = {
    {0xFF, 0x00, 0x00},
    {0xFF, 0x80, 0x00},
    {0xFF, 0xFF, 0x00},
    {0x80, 0xFF, 0x00},
    {0x00, 0xFF, 0x00},
    {0x00, 0xFF, 0x80},
    {0x00, 0xFF, 0xFF},
    {0x00, 0x80, 0xFF},
    {0x00, 0x00, 0xFF},
    {0x80, 0x00, 0xFF},
    {0xFF, 0x00, 0xFF},
    {0xFF, 0x00, 0x80},
};

#define CURSOR_SIZE 2

static handle_t video_data_channel;

static void draw_screen(u8 *screen, int color_i, i32 mouse_x, i32 mouse_y) {
    size_t screen_bytes = screen_size.height * screen_size.width * 3;
    for (i32 y = 0; (size_t)y < screen_size.height; y++) {
        for (i32 x = 0; (size_t)x < screen_size.width; x++) {
            u8 *pixel = &screen[(y * screen_size.width + x) * 3];
            u8 *color = colors[color_i];
            pixel[0] = color[0];
            pixel[1] = color[1];
            pixel[2] = color[2];
            if (x <= mouse_x + CURSOR_SIZE && y <= mouse_y + CURSOR_SIZE && x >= mouse_x - CURSOR_SIZE && y >= mouse_y - CURSOR_SIZE) {
                pixel[0] ^= -1;
                pixel[1] ^= -1;
                pixel[2] ^= -1;
            }
        }
    }
    channel_call(video_data_channel, screen_bytes, screen, NULL);
}

void main(void) {
    err_t err;
    handle_t video_size_channel;
    err = channel_get("video/size", &video_size_channel);
    if (err)
        return;
    err = channel_get("video/data", &video_data_channel);
    if (err)
        return;
    err = channel_call_sized(video_size_channel, 0, NULL, &screen_size, sizeof(ScreenSize));
    if (err)
        return;
    handle_t event_mqueue;
    err = mqueue_create(&event_mqueue);
    if (err)
        return;
    err = mqueue_add_channel(event_mqueue, "keyboard/data", (uintptr_t[2]){1, 0});
    if (err)
        return;
    err = mqueue_add_channel(event_mqueue, "mouse/data", (uintptr_t[2]){2, 0});
    if (err)
        return;
    size_t screen_bytes = screen_size.height * screen_size.width * 3;
    u8 *screen = malloc(screen_bytes);
    if (screen == NULL)
        return;
    int color = 0;
    i32 mouse_x = screen_size.width / 2;
    i32 mouse_y = screen_size.height / 2;
    draw_screen(screen, color, mouse_x, mouse_y);
    while (1) {
        uintptr_t tag[2];
        handle_t msg;
        err = mqueue_receive(event_mqueue, tag, &msg);
        if (err)
            continue;
        size_t msg_size;
        message_get_length(msg, &msg_size);
        switch (tag[0]) {
        case 1: {
            KeyEvent key_event;
            err = message_read_sized(msg, &key_event, sizeof(KeyEvent), ERR_INVALID_ARG);
            if (err)
                continue;
            if (key_event.pressed == false)
                color = (color + 1) % COLORS_NUM;
            message_reply(msg, 0, NULL);
            draw_screen(screen, color, mouse_x, mouse_y);
            break;
        }
        case 2: {
            MouseUpdate mouse_update;
            err = message_read_sized(msg, &mouse_update, sizeof(MouseUpdate), ERR_INVALID_ARG);
            if (err)
                continue;
            mouse_x += mouse_update.diff_x;
            mouse_y += mouse_update.diff_y;
            message_reply(msg, 0, NULL);
            draw_screen(screen, color, mouse_x, mouse_y);
            break;
        }
        default:
            message_reply_error(msg, ERR_INVALID_ARG);
            break;
        }
    }
}
