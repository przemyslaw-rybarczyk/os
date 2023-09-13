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
    EVENT_VIDEO_SIZE,
    EVENT_VIDEO_DATA,
} EventSource;

static handle_t video_data_channel;
static handle_t process_spawn_channel;

static handle_t event_queue;

static ScreenSize screen_size;

// The screen is organized as a tree of containers. There are two types: split containers, which are split
// either vertically or horizontally into smaller containers, and windows.
// The axis along which a container is split is referred to as its "length".

typedef enum SplitDirection {
    SPLIT_HORIZONTAL,
    SPLIT_VERTICAL,
} SplitDirection;

typedef struct ScreenPos {
    i32 x;
    i32 y;
} ScreenPos;

typedef enum ContainerType {
    CONTAINER_WINDOW,
    CONTAINER_SPLIT,
} ContainerType;

typedef struct Container {
    ContainerType type;
    // Pointer to the parent
    struct SplitContainer *parent;
    // Next sibling down or to the right, NULL if there is none
    struct Container *next_sibling;
    // The fraction of the parent container's length after which this container starts
    // For example, if the parent is split horizontally and the offset_in_parent value is 0.5,
    // the left edge of the container is located at half the parent's width.
    double offset_in_parent;
} Container;

typedef struct WindowContainer {
    Container header;
    // The buffer for the window contents
    u8 *video_buffer;
    // Input channels
    handle_t keyboard_data_in;
    handle_t mouse_data_in;
} WindowContainer;

typedef struct SplitContainer {
    Container header;
    Container *first_child;
} SplitContainer;

// Root of the container tree, covering the whole window
static Container *root_container = NULL;

// Split direction of the root container
// The split direction of other containers is not represented directly and is instead inferred from the fact
// that it is always opposite of its parent's direction.
static SplitDirection root_split_direction = SPLIT_HORIZONTAL;

static ScreenPos cursor;

static u8 *screen_buffer;

#define CURSOR_SIZE 5

// Create a new window with an attached process
static WindowContainer *create_window(void) {
    err_t err;
    // Allocate channels for the new process
    handle_t video_size_in, video_size_out;
    handle_t video_data_in, video_data_out;
    handle_t keyboard_data_in, keyboard_data_out;
    handle_t mouse_data_in, mouse_data_out;
    err = channel_create(&video_size_in, &video_size_out);
    if (err)
        goto fail_video_size_channel_create;
    err = channel_create(&video_data_in, &video_data_out);
    if (err)
        goto fail_video_data_channel_create;
    err = channel_create(&keyboard_data_in, &keyboard_data_out);
    if (err)
        goto fail_keyboard_channel_create;
    err = channel_create(&mouse_data_in, &mouse_data_out);
    if (err)
        goto fail_mouse_channel_create;
    // Allocate window
    WindowContainer *window = malloc(sizeof(WindowContainer));
    if (window == NULL)
        goto fail_window_alloc;
    window->header.type = CONTAINER_WINDOW;
    window->keyboard_data_in = keyboard_data_in;
    window->mouse_data_in = mouse_data_in;
    window->video_buffer = malloc(3 * screen_size.width * screen_size.height);
    if (window->video_buffer == NULL)
        goto fail_video_buffer_alloc;
    memset(window->video_buffer, 0, 3 * screen_size.width * screen_size.height);
    // Spawn process
    ResourceName program_resource_names[] = {resource_name("video/size"), resource_name("video/data"), resource_name("keyboard/data"), resource_name("mouse/data")};
    SendAttachedHandle program_resource_handles[] = {{ATTACHED_HANDLE_FLAG_MOVE, video_size_in}, {ATTACHED_HANDLE_FLAG_MOVE, video_data_in}, {ATTACHED_HANDLE_FLAG_MOVE, keyboard_data_out}, {ATTACHED_HANDLE_FLAG_MOVE, mouse_data_out}};
    err = channel_send(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program_resource_names), program_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program_resource_handles) / sizeof(program_resource_handles[0]), program_resource_handles}
    }, 0);
    if (err)
        goto fail_process_spawn;
    // Attach channels to event queue
    mqueue_add_channel(event_queue, video_size_out, (MessageTag){EVENT_VIDEO_SIZE, (uintptr_t)window});
    mqueue_add_channel(event_queue, video_data_out, (MessageTag){EVENT_VIDEO_DATA, (uintptr_t)window});
    return window;
fail_process_spawn:
    free(window->video_buffer);
fail_video_buffer_alloc:
    free(window);
fail_window_alloc:
    handle_free(mouse_data_in);
    handle_free(mouse_data_out);
fail_mouse_channel_create:
    handle_free(keyboard_data_in);
    handle_free(keyboard_data_out);
fail_keyboard_channel_create:
    handle_free(video_data_in);
    handle_free(video_data_out);
fail_video_data_channel_create:
    handle_free(video_size_in);
    handle_free(video_size_out);
fail_video_size_channel_create:
    return NULL;
}

// Get the offset of the origin of a container within its parent's length
static u32 get_child_offset(const Container *child, u32 parent_length) {
    return (u32)(child->offset_in_parent * parent_length + 0.5);
}

// Get the length of a container in pixels given the length of its parent
static u32 get_child_length(const Container *child, u32 parent_length) {
    if (child->next_sibling != NULL)
        return (u32)(child->next_sibling->offset_in_parent * parent_length + 0.5) - (u32)(child->offset_in_parent * parent_length + 0.5);
    else
        return parent_length - (u32)(child->offset_in_parent * parent_length + 0.5);
}

// Get the size of a container
static void get_container_size(const Container *container, ScreenSize *container_size, SplitDirection *split_direction) {
    // If the parent is NULL, this is the root container covering the whole screen
    if (container->parent == NULL) {
        *container_size = screen_size;
        *split_direction = root_split_direction;
        return;
    }
    // Otherwise, return the appropriate portion of the parent's size
    ScreenSize parent_size;
    SplitDirection parent_split_direction;
    get_container_size(&container->parent->header, &parent_size, &parent_split_direction);
    switch (parent_split_direction) {
    case SPLIT_HORIZONTAL:
        container_size->width = get_child_length(container, parent_size.width);
        container_size->height = parent_size.height;
        *split_direction = SPLIT_VERTICAL;
        break;
    case SPLIT_VERTICAL:
        container_size->width = parent_size.width;
        container_size->height = get_child_length(container, parent_size.height);
        *split_direction = SPLIT_HORIZONTAL;
        break;
    }
}

// Get the size of a window excluding borders
static void get_window_size(const WindowContainer *window, ScreenSize *window_size) {
    SplitDirection split_direction;
    return get_container_size(&window->header, window_size, &split_direction);
}

// Get the window that the cursor is currently in
// Sets window_origin to the origin of the window if it's not null.
static WindowContainer *get_current_window(ScreenPos *window_origin) {
    const Container *container = root_container;
    // The position and properties of the current container
    u32 origin_x = 0;
    u32 origin_y = 0;
    u32 width = screen_size.width;
    u32 height = screen_size.height;
    SplitDirection split_direction = root_split_direction;
    // Go down the container tree based on the cursor position until finding a window
    while (1) {
        switch (container->type) {
        case CONTAINER_WINDOW:
            if (window_origin != NULL) {
                window_origin->x = origin_x;
                window_origin->y = origin_y;
            }
            return (WindowContainer *)container;
        case CONTAINER_SPLIT:
            switch (split_direction) {
            case SPLIT_HORIZONTAL:
                for (const Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                    if (child->next_sibling == NULL || cursor.x < (i32)(origin_x + get_child_offset(child->next_sibling, width))) {
                        origin_x += get_child_offset(child, width);
                        width = get_child_length(child, width);
                        split_direction = SPLIT_VERTICAL;
                        container = child;
                        break;
                    }
                }
                break;
            case SPLIT_VERTICAL:
                for (const Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                    if (child->next_sibling == NULL || cursor.y < (i32)(origin_y + get_child_offset(child->next_sibling, height))) {
                        origin_y += get_child_offset(child, height);
                        height = get_child_length(child, height);
                        split_direction = SPLIT_HORIZONTAL;
                        container = child;
                        break;
                    }
                }
                break;
            }
        }
    }
}

// Draw a container onto the screen buffer in the given position
static void draw_container(const Container *container, u32 origin_x, u32 origin_y, u32 width, u32 height, SplitDirection split_direction) {
    switch (container->type) {
    case CONTAINER_WINDOW:
        for (size_t y = 0; y < height; y++)
            memcpy(
                screen_buffer + 3 * (screen_size.width * (origin_y + y) + origin_x),
                ((WindowContainer *)container)->video_buffer + 3 * width * y,
                3 * width
            );
        break;
    case CONTAINER_SPLIT:
        switch (split_direction) {
        case SPLIT_HORIZONTAL:
            for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
                draw_container(child, origin_x + get_child_offset(child, width), origin_y, get_child_length(child, width), height, SPLIT_VERTICAL);
            break;
        case SPLIT_VERTICAL:
            for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
                draw_container(child, origin_x, origin_y + get_child_offset(child, height), width, get_child_length(child, height), SPLIT_HORIZONTAL);
            break;
        }
        break;
    }
}

// Draw the screen
static void draw_screen(void) {
    // Draw the root container to the screen buffer
    draw_container(root_container, 0, 0, screen_size.width, screen_size.height, root_split_direction);
    // Draw the cursor
    for (size_t x = 0; x < CURSOR_SIZE; x++) {
        for (size_t y = 0; y < CURSOR_SIZE; y++) {
            if (cursor.x + x < screen_size.width && cursor.y + y < screen_size.height && x + y < CURSOR_SIZE) {
                for (size_t i = 0; i < 3; i++) {
                    screen_buffer[3 * (screen_size.width * (cursor.y + y) + cursor.x + x) + i] = 0;
                }
            }
        }
    }
    // Send the screen buffer
    channel_send(video_data_channel, &(SendMessage){1, &(SendMessageData){3 * screen_size.width * screen_size.height, screen_buffer}, 0, NULL}, 0);
}

void main(void) {
    err_t err;
    handle_t video_size_channel, keyboard_data_channel, mouse_data_channel;
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
    err = mqueue_create(&event_queue);
    if (err)
        return;
    mqueue_add_channel(event_queue, keyboard_data_channel, (MessageTag){EVENT_KEYBOARD_DATA, 0});
    mqueue_add_channel(event_queue, mouse_data_channel, (MessageTag){EVENT_MOUSE_DATA, 0});
    err = channel_call_bounded(video_size_channel, NULL, &(ReceiveMessage){sizeof(ScreenSize), &screen_size, 0, NULL}, NULL);
    if (err)
        return;
    cursor.x = screen_size.width / 2;
    cursor.y = screen_size.height / 2;
    SplitContainer *split_1 = malloc(sizeof(SplitContainer));
    if (split_1 == NULL)
        return;
    split_1->header.type = CONTAINER_SPLIT;
    SplitContainer *split_2 = malloc(sizeof(SplitContainer));
    if (split_2 == NULL)
        return;
    split_2->header.type = CONTAINER_SPLIT;
    WindowContainer *window_1 = create_window();
    if (window_1 == NULL)
        return;
    WindowContainer *window_2 = create_window();
    if (window_2 == NULL)
        return;
    WindowContainer *window_3 = create_window();
    if (window_3 == NULL)
        return;
    split_1->header.parent = NULL;
    split_1->header.next_sibling = NULL;
    split_1->header.offset_in_parent = 0.0;
    split_1->first_child = (Container *)window_1;
    window_1->header.parent = split_1;
    window_1->header.next_sibling = (Container *)split_2;
    window_1->header.offset_in_parent = 0.0;
    split_2->header.parent = split_1;
    split_2->header.next_sibling = NULL;
    split_2->header.offset_in_parent = 0.3;
    split_2->first_child = (Container *)window_2;
    window_2->header.parent = split_2;
    window_2->header.next_sibling = (Container *)window_3;
    window_2->header.offset_in_parent = 0.0;
    window_3->header.parent = split_2;
    window_3->header.next_sibling = NULL;
    window_3->header.offset_in_parent = 0.4;
    root_container = (Container *)split_1;
    screen_buffer = malloc(3 * screen_size.width * screen_size.height);
    if (screen_buffer == NULL)
        return;
    draw_screen();
    while (1) {
        handle_t msg;
        MessageTag tag;
        err = mqueue_receive(event_queue, &tag, &msg, 0);
        if (err)
            continue;
        switch ((EventSource)tag.data[0]) {
        case EVENT_KEYBOARD_DATA: {
            KeyEvent key_event;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_free(msg);
            handle_t keyboard_data_in = get_current_window(NULL)->keyboard_data_in;
            channel_send(keyboard_data_in, &(SendMessage){1, &(SendMessageData){sizeof(KeyEvent), &key_event}, 0, NULL}, FLAG_NONBLOCK);
            break;
        }
        case EVENT_MOUSE_DATA: {
            MouseUpdate mouse_update;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(MouseUpdate), &mouse_update, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_free(msg);
            cursor.x += mouse_update.diff_x;
            cursor.y += mouse_update.diff_y;
            if (cursor.x < 0)
                cursor.x = 0;
            if (cursor.x >= (i32)screen_size.width)
                cursor.x = screen_size.width - 1;
            if (cursor.y < 0)
                cursor.y = 0;
            if (cursor.y >= (i32)screen_size.height)
                cursor.y = screen_size.height - 1;
            ScreenPos window_origin;
            handle_t mouse_data_in = get_current_window(&window_origin)->mouse_data_in;
            mouse_update.abs_x = cursor.x - window_origin.x;
            mouse_update.abs_y = cursor.y - window_origin.y;
            channel_send(mouse_data_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseUpdate), &mouse_update}, 0, NULL}, FLAG_NONBLOCK);
            break;
        }
        case EVENT_VIDEO_SIZE: {
            ScreenSize window_size;
            get_window_size((WindowContainer *)tag.data[1], &window_size);
            err = message_read_bounded(msg, &(ReceiveMessage){0, NULL, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &window_size}, 0, NULL});
            break;
        }
        case EVENT_VIDEO_DATA: {
            ScreenSize window_size;
            WindowContainer *window = (WindowContainer *)tag.data[1];
            get_window_size(window, &window_size);
            err = message_read_bounded(msg, &(ReceiveMessage){3 * window_size.width * window_size.height, window->video_buffer, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
            if (err)
                continue;
            handle_free(msg);
            draw_screen();
            break;
        }
        }
    }
}
