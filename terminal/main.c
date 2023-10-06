#include <zr/types.h>

#include <stdlib.h>
#include <string.h>

#include <zr/keyboard.h>
#include <zr/syscalls.h>
#include <zr/video.h>

#include "font.h"

#define OUTPUT_READ_BUFFER_SIZE 1024

#define TEXT_BUFFER_DEFAULT_SIZE 1024
#define SCREEN_BUFFER_DEFAULT_SIZE 16384
#define INPUT_BUFFER_DEFAULT_SIZE 128

typedef enum EventSource : uintptr_t {
    EVENT_KEYBOARD,
    EVENT_RESIZE,
    EVENT_STDOUT,
    EVENT_STDERR,
    EVENT_STDIN,
} EventSource;

// Screen buffer
static u8 *screen;
static size_t screen_capacity;
static ScreenSize screen_size;

typedef enum TextColor : u8 {
    TEXT_COLOR_STDOUT,
    TEXT_COLOR_STDERR,
    TEXT_COLOR_STDIN,
} TextColor;

typedef struct TextCharacter {
    u8 ch;
    TextColor color;
} TextCharacter;

// Circular buffer containing displayed text
// Must be large enough to store enough text to fill the entire screen.
// The capacity must be a power of two.
static TextCharacter *text_buffer;
static size_t text_buffer_capacity;
static size_t text_buffer_offset = 0;
static size_t text_buffer_size = 0;

// Circular buffer containing entered text
// The capacity must be a power of two.
// The pending size variable holds the size of the part of the buffer that is waiting to be sent to stdin.
// The remaining part can still be edited.
static u8 *input_buffer;
static size_t input_buffer_capacity;
static size_t input_buffer_offset = 0;
static size_t input_buffer_size = 0;
static size_t input_buffer_pending_size = 0;

// Used for reading stdout and stderr
u8 output_read_buffer[OUTPUT_READ_BUFFER_SIZE];

// Current cursor position
static u32 cursor_x = 0;
static u32 cursor_y = 0;

static handle_t video_data_channel;

// Tells whether the program in currently awaiting input
bool waiting_for_stdin = false;
// The message message the new input should be sent as reply to when received
handle_t current_stdin_msg;
// The number of bytes requested - this is the maximum number that can be sent
size_t stdin_bytes_requested;

static const u8 background_color[3] = {0x22, 0x22, 0x22};
static const u8 stdout_color[3] = {0xDD, 0xDD, 0xDD};
static const u8 stderr_color[3] = {0xDD, 0x55, 0x55};
static const u8 stdin_color[3] = {0x88, 0xCC, 0xDD};

static const u8 keycode_chars_lower[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\n',
    0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    0, 0, 0, ' ',
};

static const u8 keycode_chars_upper[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '\n',
    0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    0, 0, 0, ' ',
};

// Send up to n bytes from the start of the input buffer as reply to the stdin request
static void send_from_input_buffer(handle_t msg, size_t bytes_requested) {
    size_t bytes_to_send = bytes_requested <= input_buffer_pending_size ? bytes_requested : input_buffer_pending_size;
    if (bytes_to_send <= input_buffer_capacity - input_buffer_offset)
        message_reply(msg, &(SendMessage){1, &(SendMessageData){bytes_to_send, input_buffer + input_buffer_offset}, 0, NULL});
    else
        message_reply(msg, &(SendMessage){2, (SendMessageData[]){
            {input_buffer_capacity - input_buffer_offset, input_buffer + input_buffer_offset},
            {bytes_to_send - (input_buffer_capacity - input_buffer_offset), input_buffer}
        }, 0, NULL});
    input_buffer_offset += bytes_to_send;
    input_buffer_size -= bytes_to_send;
    input_buffer_pending_size -= bytes_to_send;
}

// Add a character to the end of the input buffer
static err_t add_to_input_buffer(u8 c) {
    // If the input buffer is too small, try to extend it
    if (input_buffer_size >= input_buffer_capacity) {
        size_t new_input_buffer_capacity = 2 * input_buffer_capacity;
        u8 *new_input_buffer = realloc(input_buffer, new_input_buffer_capacity);
        if (new_input_buffer == NULL)
            return ERR_NO_MEMORY;
        input_buffer_capacity = new_input_buffer_capacity;
        input_buffer = new_input_buffer;
    }
    // Add to the input buffer
    input_buffer[(input_buffer_offset + input_buffer_size) & (text_buffer_capacity - 1)] = c;
    input_buffer_size++;
    // If a newline character is entered, mark all characters in the buffer as pending
    if (c == '\n') {
        input_buffer_pending_size = input_buffer_size;
        if (waiting_for_stdin) {
            send_from_input_buffer(current_stdin_msg, stdin_bytes_requested);
            waiting_for_stdin = false;
        }
    }
    return 0;
}

// Remove the last non-pending character in the input buffer if there is one
// Implements backspace.
static void remove_last_input_char(void) {
    if (input_buffer_pending_size < input_buffer_size) {
        input_buffer_size--;
        text_buffer_size--;
    }
}

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
        if (text_buffer[(text_buffer_offset - 1) & (text_buffer_capacity - 1)].ch == '\n')
            break;
    }
}

// Add a character to the text buffer
static void print_char(u8 c, TextColor color) {
    // Place character in buffer
    text_buffer[(text_buffer_offset + text_buffer_size) & (text_buffer_capacity - 1)] = (TextCharacter){c, color};
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
        u8 c = text_buffer[(text_buffer_offset + i) & (text_buffer_capacity - 1)].ch;
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
        TextCharacter text_ch = text_buffer[(text_buffer_offset + i) & (text_buffer_capacity - 1)];
        u8 c = text_ch.ch;
        // Handle newline character
        if (c == '\n') {
            x = 0;
            y++;
            continue;
        }
        // Get the font glyph for the character
        const u8 *font_char;
        if (FONT_CHAR_LOWEST <= c && c <= FONT_CHAR_HIGHEST)
            font_char = font_chars[c - FONT_CHAR_LOWEST];
        else
            font_char = font_char_unknown;
        // Draw the character
        for (u32 cy = 0; cy < FONT_HEIGHT; cy++) {
            for (u32 cx = 0; cx < FONT_WIDTH; cx++) {
                if ((font_char[cy] << cx) & 0x80) {
                    u8 *pixel = &screen[((FONT_HEIGHT * y + cy) * screen_size.width + (FONT_WIDTH * x + cx)) * 3];
                    const u8 *color;
                    switch (text_ch.color) {
                    case TEXT_COLOR_STDOUT:
                        color = stdout_color;
                        break;
                    case TEXT_COLOR_STDERR:
                        color = stderr_color;
                        break;
                    case TEXT_COLOR_STDIN:
                        color = stdin_color;
                        break;
                    }
                    pixel[0] = color[0];
                    pixel[1] = color[1];
                    pixel[2] = color[2];
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
            const u8 *color = waiting_for_stdin ? stdin_color : stdout_color;
            pixel[0] = color[0];
            pixel[1] = color[1];
            pixel[2] = color[2];
        }
    }
    // Send screen buffer
    channel_send(video_data_channel, &(SendMessage){2, (SendMessageData[]){{sizeof(ScreenSize), &screen_size}, {screen_bytes, screen}}, 0, NULL}, 0);
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
    err = channel_call_read(video_size_channel, NULL, &(ReceiveMessage){sizeof(ScreenSize), &screen_size, 0, NULL}, NULL);
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
    err = mqueue_add_channel_resource(event_mqueue, &resource_name("text/stdout_r"), (MessageTag){EVENT_STDOUT, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_mqueue, &resource_name("text/stderr_r"), (MessageTag){EVENT_STDERR, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_mqueue, &resource_name("text/stdin_r"), (MessageTag){EVENT_STDIN, 0});
    if (err)
        return;
    text_buffer_capacity = TEXT_BUFFER_DEFAULT_SIZE;
    while (text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT))
        text_buffer_capacity *= 2;
    text_buffer = malloc(text_buffer_capacity * sizeof(TextCharacter));
    if (text_buffer == NULL)
        return;
    screen_capacity = SCREEN_BUFFER_DEFAULT_SIZE;
    while (screen_capacity < screen_size.height * screen_size.width * 3)
        screen_capacity *= 2;
    screen = malloc(screen_capacity);
    if (screen == NULL)
        return;
    input_buffer_capacity = INPUT_BUFFER_DEFAULT_SIZE;
    input_buffer = malloc(input_buffer_capacity);
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
        EventSource event_source = (EventSource)tag.data[0];
        switch (event_source) {
        case EVENT_KEYBOARD: {
            KeyEvent key_event;
            err = message_read(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, NULL, ERR_INVALID_ARG, 0);
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
            if (key_event.pressed && waiting_for_stdin) {
                u8 c = keycode_char(key_event.keycode, (mod_keys_held & (MOD_KEY_LEFT_SHIFT | MOD_KEY_RIGHT_SHIFT)));
                if (c == '\b') {
                    remove_last_input_char();
                    draw_screen();
                } else if (c != 0) {
                    err = add_to_input_buffer(c);
                    if (!err) {
                        print_char(c, TEXT_COLOR_STDIN);
                        draw_screen();
                    }
                }
            }
            break;
        }
        case EVENT_RESIZE: {
            ScreenSize new_screen_size;
            err = message_read(msg, &(ReceiveMessage){sizeof(ScreenSize), &new_screen_size, 0, NULL}, NULL, NULL, ERR_INVALID_ARG, 0);
            if (err)
                continue;
            handle_free(msg);
            // Resize text buffer
            if (text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT)) {
                size_t new_text_buffer_capacity = text_buffer_capacity;
                while (new_text_buffer_capacity < (screen_size.width / FONT_WIDTH + 1) * (screen_size.height / FONT_HEIGHT))
                    new_text_buffer_capacity *= 2;
                TextCharacter *new_text_buffer = realloc(text_buffer, new_text_buffer_capacity * sizeof(TextCharacter));
                if (new_text_buffer == NULL)
                    continue;
                text_buffer = new_text_buffer;
                // Move final part of circular buffer if necessary
                if (text_buffer_offset + text_buffer_size > text_buffer_capacity) {
                    memmove(
                        text_buffer + (text_buffer_offset + new_text_buffer_capacity - text_buffer_capacity) * sizeof(TextCharacter),
                        text_buffer + text_buffer_offset * sizeof(TextCharacter),
                        text_buffer_capacity - text_buffer_offset * sizeof(TextCharacter));
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
        case EVENT_STDOUT:
        case EVENT_STDERR:
            if (waiting_for_stdin)
                message_reply_error(msg, ERR_INVALID_OPERATION);
            MessageLength message_length;
            message_get_length(msg, &message_length);
            if (message_length.handles != 0) {
                message_reply_error(msg, ERR_INVALID_ARG);
                continue;
            }
            for (size_t i = 0; i <= message_length.data / OUTPUT_READ_BUFFER_SIZE; i++) {
                size_t read_size = i < message_length.data / OUTPUT_READ_BUFFER_SIZE ? OUTPUT_READ_BUFFER_SIZE : message_length.data % OUTPUT_READ_BUFFER_SIZE;
                message_read(msg, &(ReceiveMessage){read_size, output_read_buffer, 0, NULL}, &(MessageLength){OUTPUT_READ_BUFFER_SIZE * i, 0}, &(MessageLength){0, 0}, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
                for (size_t j = 0; j < read_size; j++)
                    print_char(output_read_buffer[j], event_source == EVENT_STDERR ? TEXT_COLOR_STDERR : TEXT_COLOR_STDOUT);
            }
            message_reply(msg, NULL);
            draw_screen();
            break;
        case EVENT_STDIN:
            if (waiting_for_stdin)
                message_reply_error(msg, ERR_INVALID_OPERATION);
            err = message_read(msg, &(ReceiveMessage){sizeof(size_t), &stdin_bytes_requested, 0, NULL}, NULL, NULL, ERR_INVALID_ARG, 0);
            if (err)
                continue;
            if (stdin_bytes_requested == 0) {
                message_reply(msg, NULL);
            } else if (input_buffer_pending_size > 0) {
                send_from_input_buffer(msg, stdin_bytes_requested);
            } else {
                waiting_for_stdin = true;
                current_stdin_msg = msg;
            }
            draw_screen();
            break;
        }
    }
}
