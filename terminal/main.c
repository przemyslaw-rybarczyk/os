#include <zr/types.h>

#include <stdlib.h>
#include <string.h>

#include <zr/keyboard.h>
#include <zr/syscalls.h>
#include <zr/video.h>

#include "font.h"

#define TEXT_BUFFER_DEFAULT_SIZE 1024
#define SCREEN_BUFFER_DEFAULT_SIZE 16384

typedef enum EventSource : uintptr_t {
    EVENT_KEYBOARD,
    EVENT_RESIZE,
} EventSource;

// Screen buffer
static u8 *screen;
static size_t screen_capacity;
static ScreenSize screen_size;

// Circular buffer containing displayed text
// Must be large enough to store enough text to fill the entire screen.
// The capacity must be a power of two.
static u8 *text_buffer;
static size_t text_buffer_capacity;
static size_t text_buffer_offset = 0;
static size_t text_buffer_size = 0;

// Current cursor position
static u32 cursor_x = 0;
static u32 cursor_y = 0;

static handle_t video_data_channel;

static u8 background_color[3] = {0x22, 0x22, 0x22};
static u8 foreground_color[3] = {0xDD, 0xDD, 0xDD};

static u8 keycode_chars_lower[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\n',
    0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    0, 0, 0, ' ',
};

static u8 keycode_chars_upper[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
    0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '\n',
    0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    0, 0, 0, ' ',
};

// Convert keycode to character
static u8 keycode_char(Keycode keycode, bool shift) {
    if ((size_t)keycode < sizeof(keycode_chars_lower) / sizeof(keycode_chars_lower[0]))
        return (shift ? keycode_chars_upper : keycode_chars_lower)[(size_t)keycode];
    else
        return 0;
}

// Remove the first line of text from the text buffer
static void remove_first_line(void) {
    for (size_t i = 0; i < screen_size.width / FONT_WIDTH; i++) {
        text_buffer_offset++;
        text_buffer_size--;
        if (text_buffer[(text_buffer_offset - 1) & (text_buffer_capacity - 1)] == '\n')
            break;
    }
}

// Add a character to the text buffer
static void print_char(u8 c) {
    // Place character in buffer
    text_buffer[(text_buffer_offset + text_buffer_size) & (text_buffer_capacity - 1)] = c;
    text_buffer_size++;
    cursor_x++;
    // Move to next line on newline character or wraparound
    if (c == '\n' || cursor_x >= screen_size.width / FONT_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        // If we're already at the bottom of the screen, remove the first line to make room for the next one
        if (cursor_y >= screen_size.height / FONT_HEIGHT) {
            remove_first_line();
            cursor_y--;
        }
    }
}

// Recalculate cursor position and remove early lines after resize
static void reshape_text(void) {
    cursor_x = 0;
    cursor_y = 0;
    // Recalculate cursor position
    for (size_t i = 0; i < text_buffer_size; i++) {
        u8 c = text_buffer[(text_buffer_offset + i) & (text_buffer_capacity - 1)];
        cursor_x++;
        if (c == '\n' || cursor_x >= screen_size.width / FONT_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    // Remove lines that are now past the upper edge of the screen
    if (cursor_y >= screen_size.height / FONT_HEIGHT) {
        for (u32 i = 0; i <= cursor_y - screen_size.height / FONT_HEIGHT; i++)
            remove_first_line();
        cursor_y = screen_size.height / FONT_HEIGHT - 1;
    }
}

static void draw_screen(void) {
    size_t screen_bytes = screen_size.height * screen_size.width * 3;
    // Fill screen with background color
    for (u32 y = 0; y < screen_size.height; y++) {
        for (u32 x = 0; x < screen_size.width; x++) {
            u8 *pixel = &screen[(y * screen_size.width + x) * 3];
            pixel[0] = background_color[0];
            pixel[1] = background_color[1];
            pixel[2] = background_color[2];
        }
    }
    // Draw characters
    u32 x = 0;
    u32 y = 0;
    for (size_t i = 0; i < text_buffer_size; i++) {
        // Get character
        u8 c = text_buffer[(text_buffer_offset + i) & (text_buffer_capacity - 1)];
        // Handle newline character
        if (c == '\n') {
            x = 0;
            y++;
            continue;
        }
        // If the character is valid, draw it
        if (FONT_CHAR_LOWEST <= c && c <= FONT_CHAR_HIGHEST) {
            for (u32 cy = 0; cy < FONT_HEIGHT; cy++) {
                for (u32 cx = 0; cx < FONT_WIDTH; cx++) {
                    if ((font_chars[c - FONT_CHAR_LOWEST][cy] << cx) & 0x80) {
                        u8 *pixel = &screen[((FONT_HEIGHT * y + cy) * screen_size.width + (FONT_WIDTH * x + cx)) * 3];
                        pixel[0] = foreground_color[0];
                        pixel[1] = foreground_color[1];
                        pixel[2] = foreground_color[2];
                    }
                }
            }
        }
        // Move to the right and skip to the next line if necessary
        x++;
        if (x >= screen_size.width / FONT_WIDTH) {
            x = 0;
            y++;
        }
    }
    // Draw cursor
    for (u32 cy = 0; cy < FONT_HEIGHT; cy++) {
        for (u32 cx = 0; cx < FONT_WIDTH; cx++) {
            u8 *pixel = &screen[((FONT_HEIGHT * y + cy) * screen_size.width + (FONT_WIDTH * x + cx)) * 3];
            pixel[0] = foreground_color[0];
            pixel[1] = foreground_color[1];
            pixel[2] = foreground_color[2];
        }
    }
    // Send screen buffer
    channel_send(video_data_channel, &(SendMessage){2, (SendMessageData[]){{sizeof(ScreenSize), &screen_size}, {screen_bytes, screen}}, 0, NULL}, FLAG_NONBLOCK);
}

typedef enum ModKeys : u32 {
    MOD_KEY_LEFT_SHIFT = UINT32_C(1) << 0,
    MOD_KEY_RIGHT_SHIFT = UINT32_C(1) << 1,
} ModKeys;

void main(void) {
    err_t err;
    handle_t video_size_channel;
    err = resource_get(&resource_name("video/size"), RESOURCE_TYPE_CHANNEL_SEND, &video_size_channel);
    if (err)
        return;
    err = resource_get(&resource_name("video/data"), RESOURCE_TYPE_CHANNEL_SEND, &video_data_channel);
    if (err)
        return;
    err = channel_call_bounded(video_size_channel, NULL, &(ReceiveMessage){sizeof(ScreenSize), &screen_size, 0, NULL}, NULL);
    if (err)
        return;
    handle_t event_mqueue;
    err = mqueue_create(&event_mqueue);
    if (err)
        return;
    err = mqueue_add_channel_resource(event_mqueue, &resource_name("keyboard/data"), (MessageTag){EVENT_KEYBOARD, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_mqueue, &resource_name("video/resize"), (MessageTag){EVENT_RESIZE, 0});
    if (err)
        return;
    text_buffer_capacity = TEXT_BUFFER_DEFAULT_SIZE;
    while (text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT))
        text_buffer_capacity *= 2;
    text_buffer = malloc(text_buffer_capacity);
    if (text_buffer == NULL)
        return;
    screen_capacity = SCREEN_BUFFER_DEFAULT_SIZE;
    while (screen_capacity < screen_size.height * screen_size.width * 3)
        screen_capacity *= 2;
    screen = malloc(screen_capacity);
    if (screen == NULL)
        return;
    draw_screen();
    ModKeys mod_keys_held = 0;
    while (1) {
        MessageTag tag;
        handle_t msg;
        err = mqueue_receive(event_mqueue, &tag, &msg, 0);
        if (err)
            continue;
        switch ((EventSource)tag.data[0]) {
        case EVENT_KEYBOARD: {
            KeyEvent key_event;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            handle_free(msg);
            // Handle mod keys
            ModKeys mod_key;
            switch (key_event.keycode) {
            case KEY_LEFT_SHIFT:
                mod_key = MOD_KEY_LEFT_SHIFT;
                break;
            case KEY_RIGHT_SHIFT:
                mod_key = MOD_KEY_RIGHT_SHIFT;
                break;
            default:
                mod_key = 0;
                break;
            }
            if (key_event.pressed)
                mod_keys_held |= mod_key;
            else
                mod_keys_held &= ~mod_key;
            // Print appropriate character
            if (key_event.pressed) {
                u8 c = keycode_char(key_event.keycode, (mod_keys_held & (MOD_KEY_LEFT_SHIFT | MOD_KEY_RIGHT_SHIFT)));
                if (c != 0)
                    print_char(c);
            }
            draw_screen();
            break;
        }
        case EVENT_RESIZE: {
            ScreenSize new_screen_size;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(ScreenSize), &new_screen_size, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            handle_free(msg);
            // Resize text buffer
            if (text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT)) {
                size_t new_text_buffer_capacity = text_buffer_capacity;
                while (new_text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT))
                    new_text_buffer_capacity *= 2;
                u8 *new_text_buffer = realloc(text_buffer, new_text_buffer_capacity);
                if (new_text_buffer == NULL)
                    continue;
                text_buffer = new_text_buffer;
                // Move final part of circular buffer if necessary
                if (text_buffer_offset + text_buffer_size > text_buffer_capacity) {
                    memmove(
                        text_buffer + text_buffer_offset + (new_text_buffer_capacity - text_buffer_capacity),
                        text_buffer + text_buffer_offset,
                        text_buffer_capacity - text_buffer_offset);
                    text_buffer_offset += new_text_buffer_capacity - text_buffer_capacity;
                }
                text_buffer_capacity = new_text_buffer_capacity;
            }
            // Resize screen buffer
            if (new_screen_size.height * new_screen_size.width * 3 > screen_capacity) {
                size_t new_screen_capacity = screen_capacity;
                while (new_screen_size.height * new_screen_size.width * 3 > new_screen_capacity)
                    new_screen_capacity *= 2;
                u8 *new_screen = realloc(screen, new_screen_capacity);
                if (new_screen == NULL)
                    continue;
                screen = new_screen;
                screen_capacity = new_screen_capacity;
            }
            // Update screen size and adjust text
            screen_size = new_screen_size;
            reshape_text();
            draw_screen();
            break;
        }
        }
    }
}
