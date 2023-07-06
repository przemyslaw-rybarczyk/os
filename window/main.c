#include <zr/types.h>

#include <stdlib.h>
#include <string.h>

#include <zr/error.h>
#include <zr/keyboard.h>
#include <zr/mouse.h>
#include <zr/syscalls.h>
#include <zr/video.h>

#include "included_programs.h"

typedef enum EventSource : uintptr_t {
    EVENT_KEYBOARD_DATA,
    EVENT_MOUSE_DATA,
    EVENT_LEFT_VIDEO_SIZE,
    EVENT_RIGHT_VIDEO_SIZE,
    EVENT_LEFT_VIDEO_DATA,
    EVENT_RIGHT_VIDEO_DATA,
} EventSource;

handle_t video_data_channel;

ScreenSize screen_size;
size_t x_split;

i32 cursor_x;
i32 cursor_y;

u8 *left_video_buffer;
u8 *right_video_buffer;
u8 *screen_buffer;

#define CURSOR_SIZE 5

static void draw_screen(void) {
    for (size_t y = 0; y < screen_size.height; y++) {
        memcpy(screen_buffer + 3 * screen_size.width * y, left_video_buffer + 3 * x_split * y, 3 * x_split);
        memcpy(screen_buffer + 3 * (screen_size.width * y + x_split), right_video_buffer + 3 * (screen_size.width - x_split) * y, 3 * (screen_size.width - x_split));
    }
    for (size_t x = 0; x < CURSOR_SIZE; x++) {
        for (size_t y = 0; y < CURSOR_SIZE; y++) {
            if (cursor_x + x < screen_size.width && cursor_y + y < screen_size.height && x + y < CURSOR_SIZE) {
                for (size_t i = 0; i < 3; i++) {
                    screen_buffer[3 * (screen_size.width * (cursor_y + y) + cursor_x + x) + i] = 0;
                }
            }
        }
    }
    channel_send(video_data_channel, &(SendMessage){1, &(SendMessageData){3 * screen_size.width * screen_size.height, screen_buffer}, 0, NULL});
}

void main(void) {
    err_t err;
    handle_t video_size_channel, keyboard_data_channel, mouse_data_channel, process_spawn_channel;
    err = resource_get(&resource_name("video/size"), RESOURCE_TYPE_CHANNEL_SEND, &video_size_channel);
    if (err)
        return;
    err = resource_get(&resource_name("video/data"), RESOURCE_TYPE_CHANNEL_SEND, &video_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("keyboard/data"), RESOURCE_TYPE_CHANNEL_RECEIVE, &keyboard_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("mouse/data"), RESOURCE_TYPE_CHANNEL_RECEIVE, &mouse_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("process/spawn"), RESOURCE_TYPE_CHANNEL_SEND, &process_spawn_channel);
    if (err)
        return;
    handle_t left_video_size_in, left_video_size_out, right_video_size_in, right_video_size_out;
    handle_t left_video_data_in, left_video_data_out, right_video_data_in, right_video_data_out;
    handle_t left_keyboard_data_in, left_keyboard_data_out, right_keyboard_data_in, right_keyboard_data_out;
    handle_t left_mouse_data_in, left_mouse_data_out, right_mouse_data_in, right_mouse_data_out;
    err = channel_create(&left_video_size_in, &left_video_size_out);
    if (err)
        return;
    err = channel_create(&right_video_size_in, &right_video_size_out);
    if (err)
        return;
    err = channel_create(&left_video_data_in, &left_video_data_out);
    if (err)
        return;
    err = channel_create(&right_video_data_in, &right_video_data_out);
    if (err)
        return;
    err = channel_create(&left_keyboard_data_in, &left_keyboard_data_out);
    if (err)
        return;
    err = channel_create(&right_keyboard_data_in, &right_keyboard_data_out);
    if (err)
        return;
    err = channel_create(&left_mouse_data_in, &left_mouse_data_out);
    if (err)
        return;
    err = channel_create(&right_mouse_data_in, &right_mouse_data_out);
    if (err)
        return;
    ResourceName program_resource_names[] = {resource_name("video/size"), resource_name("video/data"), resource_name("keyboard/data"), resource_name("mouse/data")};
    SendAttachedHandle program_resource_handles_1[] = {{ATTACHED_HANDLE_FLAG_MOVE, left_video_size_in}, {ATTACHED_HANDLE_FLAG_MOVE, left_video_data_in}, {ATTACHED_HANDLE_FLAG_MOVE, left_keyboard_data_out}, {ATTACHED_HANDLE_FLAG_MOVE, left_mouse_data_out}};
    SendAttachedHandle program_resource_handles_2[] = {{ATTACHED_HANDLE_FLAG_MOVE, right_video_size_in}, {ATTACHED_HANDLE_FLAG_MOVE, right_video_data_in}, {ATTACHED_HANDLE_FLAG_MOVE, right_keyboard_data_out}, {ATTACHED_HANDLE_FLAG_MOVE, right_mouse_data_out}};
    err = channel_send(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program_resource_names), program_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program_resource_handles_1) / sizeof(program_resource_handles_1[0]), program_resource_handles_1}
    });
    if (err)
        return;
    err = channel_send(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program_resource_names), program_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program_resource_handles_2) / sizeof(program_resource_handles_2[0]), program_resource_handles_2}
    });
    if (err)
        return;
    err = channel_call_bounded(video_size_channel, NULL, &(ReceiveMessage){sizeof(ScreenSize), &screen_size, 0, NULL}, NULL);
    if (err)
        return;
    x_split = screen_size.width / 3;
    cursor_x = screen_size.width / 2;
    cursor_y = screen_size.height / 2;
    handle_t event_queue;
    left_video_buffer = malloc(3 * x_split * screen_size.height);
    if (left_video_buffer == NULL)
        return;
    memset(left_video_buffer, 0, 3 * x_split * screen_size.height);
    right_video_buffer = malloc(3 * (screen_size.width - x_split) * screen_size.height);
    if (right_video_buffer == NULL)
        return;
    memset(right_video_buffer, 0, 3 * (screen_size.width - x_split) * screen_size.height);
    screen_buffer = malloc(3 * screen_size.width * screen_size.height);
    if (screen_buffer == NULL)
        return;
    err = mqueue_create(&event_queue);
    if (err)
        return;
    mqueue_add_channel(event_queue, keyboard_data_channel, (MessageTag){EVENT_KEYBOARD_DATA, 0});
    mqueue_add_channel(event_queue, mouse_data_channel, (MessageTag){EVENT_MOUSE_DATA, 0});
    mqueue_add_channel(event_queue, left_video_size_out, (MessageTag){EVENT_LEFT_VIDEO_SIZE, 0});
    mqueue_add_channel(event_queue, right_video_size_out, (MessageTag){EVENT_RIGHT_VIDEO_SIZE, 0});
    mqueue_add_channel(event_queue, left_video_data_out, (MessageTag){EVENT_LEFT_VIDEO_DATA, 0});
    mqueue_add_channel(event_queue, right_video_data_out, (MessageTag){EVENT_RIGHT_VIDEO_DATA, 0});
    draw_screen();
    while (1) {
        handle_t msg;
        MessageTag tag;
        err = mqueue_receive(event_queue, &tag, &msg);
        if (err)
            continue;
        switch ((EventSource)tag.data[0]) {
        case EVENT_KEYBOARD_DATA: {
            KeyEvent key_event;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_t keyboard_data_in = cursor_x < (i32)x_split ? left_keyboard_data_in : right_keyboard_data_in;
            channel_send(keyboard_data_in, &(SendMessage){1, &(SendMessageData){sizeof(KeyEvent), &key_event}, 0, NULL});
            break;
        }
        case EVENT_MOUSE_DATA: {
            MouseUpdate mouse_update;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(MouseUpdate), &mouse_update, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            cursor_x += mouse_update.diff_x;
            cursor_y += mouse_update.diff_y;
            if (cursor_x < 0)
                cursor_x = 0;
            if (cursor_x >= (i32)screen_size.width)
                cursor_x = screen_size.width - 1;
            if (cursor_y < 0)
                cursor_y = 0;
            if (cursor_y >= (i32)screen_size.height)
                cursor_y = screen_size.height - 1;
            handle_t mouse_data_in = cursor_x < (i32)x_split ? left_mouse_data_in : right_mouse_data_in;
            channel_send(mouse_data_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseUpdate), &mouse_update}, 0, NULL});
            break;
        }
        case EVENT_LEFT_VIDEO_SIZE:
            err = message_read_bounded(msg, &(ReceiveMessage){0, NULL, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &(ScreenSize){x_split, screen_size.height}}, 0, NULL});
            break;
        case EVENT_RIGHT_VIDEO_SIZE:
            err = message_read_bounded(msg, &(ReceiveMessage){0, NULL, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &(ScreenSize){screen_size.width - x_split, screen_size.height}}, 0, NULL});
            break;
        case EVENT_LEFT_VIDEO_DATA:
            err = message_read_bounded(msg, &(ReceiveMessage){3 * x_split * screen_size.height, left_video_buffer, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_free(msg);
            draw_screen();
            break;
        case EVENT_RIGHT_VIDEO_DATA:
            err = message_read_bounded(msg, &(ReceiveMessage){3 * (screen_size.width - x_split) * screen_size.height, right_video_buffer, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_free(msg);
            draw_screen();
            break;
        }
    }
}
