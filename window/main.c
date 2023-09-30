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
// Split containers may not contain other containers split along the same axis.

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
    CONTAINER_SPLIT_HORIZONTAL,
    CONTAINER_SPLIT_VERTICAL,
} ContainerType;

typedef struct Container {
    ContainerType type;
    // Pointer to the parent
    struct SplitContainer *parent;
    // Next sibling up or to the left, NULL if there is none
    struct Container *prev_sibling;
    // Next sibling down or to the right, NULL if there is none
    struct Container *next_sibling;
    // The window that gets focus when moving to this container
    struct WindowContainer *focused_window;
    // The fraction of the parent container's length after which this container starts
    // For example, if the parent is split horizontally and the offset_in_parent value is 0.5,
    // the left edge of the container is located at half the parent's width.
    double offset_in_parent;
} Container;

typedef struct WindowContainer {
    Container header;
    // The buffer for the window contents
    ScreenSize video_buffer_size;
    size_t video_buffer_capacity;
    u8 *video_buffer;
    // Input channels
    handle_t video_resize_in;
    handle_t keyboard_data_in;
    handle_t mouse_data_in;
} WindowContainer;

typedef struct SplitContainer {
    Container header;
    Container *first_child;
} SplitContainer;

// Root of the container tree, covering the whole window
static Container *root_container = NULL;

static ScreenPos cursor;

static u8 *screen_buffer;

static const u8 border_color_unfocused[3] = {0xB0, 0x90, 0xFF};
static const u8 border_color_focused[3] = {0x70, 0x50, 0xFF};

#define BORDER_THICKNESS 3
#define CURSOR_SIZE 5
#define RESIZE_PIXELS 5

typedef enum Direction {
    DIRECTION_UP,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
    DIRECTION_RIGHT,
} Direction;

static bool direction_is_horizontal(Direction direction) {
    return direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT;
}

static bool direction_is_forward(Direction direction) {
    return direction == DIRECTION_DOWN || direction == DIRECTION_RIGHT;
}

#define VIDEO_BUFFER_DEFAULT_SIZE 16384

// Create a new window with an attached process
static WindowContainer *create_window(void) {
    err_t err;
    // Allocate channels for the new process
    handle_t video_size_in, video_size_out;
    handle_t video_data_in, video_data_out;
    handle_t video_resize_in, video_resize_out;
    handle_t keyboard_data_in, keyboard_data_out;
    handle_t mouse_data_in, mouse_data_out;
    handle_t text_stdout_in, text_stdout_out;
    err = channel_create(&video_size_in, &video_size_out);
    if (err)
        goto fail_video_size_channel_create;
    err = channel_create(&video_data_in, &video_data_out);
    if (err)
        goto fail_video_data_channel_create;
    err = channel_create(&video_resize_in, &video_resize_out);
    if (err)
        goto fail_video_resize_channel_create;
    err = channel_create(&keyboard_data_in, &keyboard_data_out);
    if (err)
        goto fail_keyboard_channel_create;
    err = channel_create(&mouse_data_in, &mouse_data_out);
    if (err)
        goto fail_mouse_channel_create;
    err = channel_create(&text_stdout_in, &text_stdout_out);
    if (err)
        goto fail_stdout_channel_create;
    // Allocate window
    WindowContainer *window = malloc(sizeof(WindowContainer));
    if (window == NULL)
        goto fail_window_alloc;
    window->header.type = CONTAINER_WINDOW;
    window->video_resize_in = video_resize_in;
    window->keyboard_data_in = keyboard_data_in;
    window->mouse_data_in = mouse_data_in;
    window->video_buffer_size = (ScreenSize){0, 0};
    window->video_buffer_capacity = VIDEO_BUFFER_DEFAULT_SIZE;
    window->video_buffer = malloc(window->video_buffer_capacity);
    if (window->video_buffer == NULL)
        goto fail_video_buffer_alloc;
    // Spawn terminal process
    ResourceName program1_resource_names[] = {resource_name("video/size"), resource_name("video/data"), resource_name("video/resize"), resource_name("keyboard/data"), resource_name("mouse/data"), resource_name("text/stdout_r")};
    SendAttachedHandle program1_resource_handles[] = {{ATTACHED_HANDLE_FLAG_MOVE, video_size_in}, {ATTACHED_HANDLE_FLAG_MOVE, video_data_in}, {ATTACHED_HANDLE_FLAG_MOVE, video_resize_out}, {ATTACHED_HANDLE_FLAG_MOVE, keyboard_data_out}, {ATTACHED_HANDLE_FLAG_MOVE, mouse_data_out}, {ATTACHED_HANDLE_FLAG_MOVE, text_stdout_out}};
    err = channel_send(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program1_resource_names), program1_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program1_resource_handles) / sizeof(program1_resource_handles[0]), program1_resource_handles}
    }, 0);
    if (err)
        goto fail_process_spawn;
    // Spawn process running in terminal
    ResourceName program2_resource_names[] = {resource_name("text/stdout")};
    SendAttachedHandle program2_resource_handles[] = {{ATTACHED_HANDLE_FLAG_MOVE, text_stdout_in}};
    err = channel_send(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program2_resource_names), program2_resource_names},
            {included_file_program2_end - included_file_program2, included_file_program2}},
        1, &(SendMessageHandles){sizeof(program2_resource_handles) / sizeof(program2_resource_handles[0]), program2_resource_handles}
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
    handle_free(text_stdout_in);
    handle_free(text_stdout_out);
fail_stdout_channel_create:
    handle_free(mouse_data_in);
    handle_free(mouse_data_out);
fail_mouse_channel_create:
    handle_free(keyboard_data_in);
    handle_free(keyboard_data_out);
fail_keyboard_channel_create:
    handle_free(video_resize_in);
    handle_free(video_resize_out);
fail_video_resize_channel_create:
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
static void get_container_size(const Container *container, ScreenSize *container_size) {
    // If the parent is NULL, this is the root container covering the whole screen
    if (container->parent == NULL) {
        *container_size = screen_size;
        return;
    }
    // Otherwise, return the appropriate portion of the parent's size
    ScreenSize parent_size;
    get_container_size(&container->parent->header, &parent_size);
    switch (container->parent->header.type) {
    case CONTAINER_SPLIT_HORIZONTAL:
        container_size->width = get_child_length(container, parent_size.width);
        container_size->height = parent_size.height;
        break;
    case CONTAINER_SPLIT_VERTICAL:
        container_size->width = parent_size.width;
        container_size->height = get_child_length(container, parent_size.height);
        break;
    default:
        break;
    }
}

// Get the size of a window excluding borders
static void get_window_size(const WindowContainer *window, ScreenSize *window_size) {
    ScreenSize container_size;
    get_container_size(&window->header, &container_size);
    window_size->width = container_size.width - 2 * BORDER_THICKNESS;
    window_size->height = container_size.height - 2 * BORDER_THICKNESS;
}

// Get the window that the cursor is currently in
// Sets window_origin to the origin of the window if it's not null.
// Returns NULL if not pointing at a window.
static WindowContainer *get_pointed_at_window(ScreenPos *window_origin) {
    if (root_container == NULL)
        return NULL;
    const Container *container = root_container;
    // The position and properties of the current container
    u32 origin_x = 0;
    u32 origin_y = 0;
    u32 width = screen_size.width;
    u32 height = screen_size.height;
    // Go down the container tree based on the cursor position until finding a window
    while (1) {
        switch (container->type) {
        case CONTAINER_WINDOW:
            if (window_origin != NULL) {
                window_origin->x = origin_x + BORDER_THICKNESS;
                window_origin->y = origin_y + BORDER_THICKNESS;
            }
            return (WindowContainer *)container;
        case CONTAINER_SPLIT_HORIZONTAL:
            for (const Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.x < (i32)(origin_x + get_child_offset(child->next_sibling, width))) {
                    origin_x += get_child_offset(child, width);
                    width = get_child_length(child, width);
                    container = child;
                    break;
                }
            }
            break;
        case CONTAINER_SPLIT_VERTICAL:
            for (const Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.y < (i32)(origin_y + get_child_offset(child->next_sibling, height))) {
                    origin_y += get_child_offset(child, height);
                    height = get_child_length(child, height);
                    container = child;
                    break;
                }
            }
            break;
        }
    }
}

// Get the lowest ancestor for a given container that has a sibling in a given direction
// If there is no such ancestor, returns NULL.
static Container *get_ancestor_with_sibling_in_direction(Container *container, Direction direction) {
    // If the container is the root, there is nothing further in any direction
    if (root_container->focused_window->header.parent == NULL)
        return NULL;
    // Get the lowest ancestor of the window whose parent is split along the direction's axis
    // This is either the window or its parent, depending on which direction the parent is split.
    Container *ancestor =
        container->parent->header.type == (direction_is_horizontal(direction) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL)
        ? container : &container->parent->header;
    while (1) {
        // If there is a sibling in the given direction, return the ancestor
        Container *forward_sibling = direction_is_forward(direction) ? ancestor->next_sibling : ancestor->prev_sibling;
        if (forward_sibling != NULL)
            return ancestor;
        // Otherwise, we're already at the edge of the ancestor.
        // Get the container two levels up, since it will be split along the same direction, and repeat.
        // If there are no more levels, we're at the edge of the window, so we can't move any further.
        if (ancestor->parent == NULL || ancestor->parent->header.parent == NULL)
            return NULL;
        ancestor = &ancestor->parent->header.parent->header;
    }
}

// Same as get_ancestor_with_sibling_in_direction(), but returns the sibling
static Container *get_sibling_of_ancestor_in_direction(Container *container, Direction direction) {
    Container *ancestor = get_ancestor_with_sibling_in_direction(container, direction);
    if (ancestor == NULL)
        return NULL;
    return direction_is_forward(direction) ? ancestor->next_sibling : ancestor->prev_sibling;
}

// Set the given window as the focused window
static void set_focused_window(WindowContainer *window) {
    // Set the window as the focused window for all its ancestors
    for (Container *ancestor = &window->header; ancestor != NULL; ancestor = &ancestor->parent->header)
        ancestor->focused_window = window;
}

// Switch the focused window to the next one in a given direction
static void switch_focused_window(Direction direction) {
    if (root_container == NULL)
        return;
    Container *forward_sibling = get_sibling_of_ancestor_in_direction(&root_container->focused_window->header, direction);
    if (forward_sibling != NULL)
        set_focused_window(forward_sibling->focused_window);
}

// Change the offset of the container by the given amount
// This effectively moves either the left or upper edge of the screen, depending on the parent's split direction.
// Performs checks to make sure there is room to move the edge.
// Returns true if window borders were changed, false otherwise.
static bool container_move_offset(Container *container, double diff) {
    bool valid = diff < 0.0
        ? container->offset_in_parent + diff > container->prev_sibling->offset_in_parent
        : (container->next_sibling != NULL
            ? container->offset_in_parent + diff < container->next_sibling->offset_in_parent
            : container->offset_in_parent + diff < 1.0);
    if (valid)
        container->offset_in_parent += diff;
    return valid && diff != 0.0;
}

// Send a resize message to every window in the container
static void send_resize_messages(Container *container) {
    switch (container->type) {
    case CONTAINER_WINDOW: {
        WindowContainer *window = (WindowContainer *)container;
        ScreenSize window_size;
        get_window_size(window, &window_size);
        channel_send(window->video_resize_in, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &window_size}, 0, NULL}, FLAG_NONBLOCK);
        break;
    }
    case CONTAINER_SPLIT_HORIZONTAL:
    case CONTAINER_SPLIT_VERTICAL:
        for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
            send_resize_messages(child);
        break;
    }
}

// Shift the given edge of the focused window by the given number of pixels
// Positive `diff` values represent expanding the window, negative ones represent shrinking.
static void resize_focused_window(Direction side, i32 diff) {
    if (root_container == NULL)
        return;
    Container *container = get_ancestor_with_sibling_in_direction(&root_container->focused_window->header, side);
    if (container == NULL)
        return;
    ScreenSize parent_size;
    get_container_size((Container *)container->parent, &parent_size);
    u32 parent_length = direction_is_horizontal(side) ? parent_size.width : parent_size.height;
    if (direction_is_forward(side)) {
        bool borders_changed = container_move_offset(container->next_sibling, (double)diff / parent_length);
        if (borders_changed) {
            send_resize_messages(container);
            send_resize_messages(container->next_sibling);
        }
    } else {
        bool borders_changed = container_move_offset(container, - (double)diff / parent_length);
        if (borders_changed) {
            send_resize_messages(container);
            send_resize_messages(container->prev_sibling);
        }
    }
}

// Add a new window neighboring the focused window in the given direction
static void add_new_window_next_to_focused(Direction side) {
    if (root_container == NULL) {
        WindowContainer *window = create_window();
        if (window == NULL)
            return;
        window->header.parent = NULL;
        window->header.prev_sibling = NULL;
        window->header.next_sibling = NULL;
        window->header.focused_window = window;
        window->header.offset_in_parent = 0.0;
        root_container = (Container *)window;
        return;
    }
    WindowContainer *focused_window = root_container->focused_window;
    // Determine whether a new split container should be created containing the focused window and the new window,
    // or if the new window should be placed as a sibling of the focused window.
    // This depends on whether the direction along which the new window is created the same or opposite
    // to the direction along which the focused window's parent container is split.
    // If there is no parent container, then there is only window at the root and a new container has to be created.
    bool create_new_split = focused_window->header.parent != NULL
        ? focused_window->header.parent->header.type == (direction_is_horizontal(side) ? CONTAINER_SPLIT_VERTICAL : CONTAINER_SPLIT_HORIZONTAL)
        : true;
    // If necessary, create a new split container
    SplitContainer *split = NULL;
    if (create_new_split) {
        split = malloc(sizeof(SplitContainer));
        if (split == NULL)
            return;
    }
    // Create a new window
    WindowContainer *window = create_window();
    if (window == NULL) {
        free(split);
        return;
    }
    if (create_new_split) {
        // Create a new split container containing the old focused window and the new window and place it in place of the focused window
        // Each will occupy half ot the split container.
        split->header.type = direction_is_horizontal(side) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL;
        split->header.parent = focused_window->header.parent;
        split->header.prev_sibling = focused_window->header.prev_sibling;
        split->header.next_sibling = focused_window->header.next_sibling;
        if (split->header.prev_sibling != NULL)
            split->header.prev_sibling->next_sibling = (Container *)split;
        if (split->header.next_sibling != NULL)
            split->header.next_sibling->prev_sibling = (Container *)split;
        split->header.offset_in_parent = focused_window->header.offset_in_parent;
        if (focused_window->header.parent != NULL) {
            if (focused_window->header.parent->first_child == (Container *)focused_window)
                focused_window->header.parent->first_child = (Container *)split;
        } else {
            root_container = (Container *)split;
        }
        focused_window->header.parent = split;
        window->header.parent = split;
        if (direction_is_forward(side)) {
            split->first_child = (Container *)focused_window;
            focused_window->header.prev_sibling = NULL;
            focused_window->header.next_sibling = (Container *)window;
            focused_window->header.offset_in_parent = 0.0;
            window->header.prev_sibling = (Container *)focused_window;
            window->header.next_sibling = NULL;
            window->header.offset_in_parent = 0.5;
        } else {
            split->first_child = (Container *)window;
            window->header.prev_sibling = NULL;
            window->header.next_sibling = (Container *)focused_window;
            window->header.offset_in_parent = 0.0;
            focused_window->header.prev_sibling = (Container *)window;
            focused_window->header.next_sibling = NULL;
            focused_window->header.offset_in_parent = 0.5;
        }
        // Inform the focused window that it was resized
        send_resize_messages((Container *)focused_window);
    } else {
        // Add the window as a sibling to the focused window
        // Its witdth will be set to 1 / (number of siblings).
        size_t num_siblings = 0;
        for (Container *child = focused_window->header.parent->first_child; child != NULL; child = child->next_sibling)
            num_siblings++;
        // Shrink all siblings to make room for new window
        for (Container *child = focused_window->header.parent->first_child; child != NULL; child = child->next_sibling)
            child->offset_in_parent *= (double)num_siblings / (num_siblings + 1);
        // Insert into tree structure, either after or before the focused window depending on the direction
        window->header.parent = focused_window->header.parent;
        if (direction_is_forward(side)) {
            window->header.prev_sibling = (Container *)focused_window;
            window->header.next_sibling = focused_window->header.next_sibling;
            if (focused_window->header.next_sibling != NULL)
                focused_window->header.next_sibling->prev_sibling = (Container *)window;
            focused_window->header.next_sibling = (Container *)window;
        } else {
            window->header.prev_sibling = focused_window->header.prev_sibling;
            window->header.next_sibling = (Container *)focused_window;
            if (focused_window->header.prev_sibling != NULL) {
                focused_window->header.prev_sibling->next_sibling = (Container *)window;
            }
            focused_window->header.prev_sibling = (Container *)window;
            if (window->header.parent->first_child == (Container *)focused_window)
                window->header.parent->first_child = (Container *)window;
        }
        // Set the new window's offset the offset of the window coming after it
        window->header.offset_in_parent = window->header.next_sibling != NULL ? window->header.next_sibling->offset_in_parent : (double)num_siblings / (num_siblings + 1);
        // Shift all windows after the new window to the right to make room for it
        for (Container *child = window->header.next_sibling; child != NULL; child = child->next_sibling)
            child->offset_in_parent += 1.0 / (num_siblings + 1);
        // Send messages informing of the resize
        for (Container *child = focused_window->header.parent->first_child; child != NULL; child = child->next_sibling)
            if (child != (Container *)window)
                send_resize_messages(child);
    }
    // Set the new window as focused
    // This will also update all `focused_window` links to have correct values.
    set_focused_window(window);
}

// Draw a solid rectangle
static void draw_rectangle(const u8 *color, u32 origin_x, u32 origin_y, u32 width, u32 height) {
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            size_t offset = 3 * (screen_size.width * (origin_y + y) + origin_x + x);
            screen_buffer[offset + 0] = color[0];
            screen_buffer[offset + 1] = color[1];
            screen_buffer[offset + 2] = color[2];
        }
    }
}

// Draw a container onto the screen buffer at a given position
static void draw_container(const Container *container, u32 origin_x, u32 origin_y, u32 width, u32 height) {
    switch (container->type) {
    case CONTAINER_WINDOW: {
        WindowContainer *window = (WindowContainer *)container;
        // Draw window border
        const u8 *border_color = window == root_container->focused_window ? border_color_focused : border_color_unfocused;
        draw_rectangle(border_color, origin_x, origin_y, width, BORDER_THICKNESS);
        draw_rectangle(border_color, origin_x, origin_y + BORDER_THICKNESS, BORDER_THICKNESS, height - 2 * BORDER_THICKNESS);
        draw_rectangle(border_color, origin_x + width - BORDER_THICKNESS, origin_y + BORDER_THICKNESS, BORDER_THICKNESS, height - 2 * BORDER_THICKNESS);
        draw_rectangle(border_color, origin_x, origin_y + height - BORDER_THICKNESS, width, BORDER_THICKNESS);
        // Draw window contents
        origin_x += BORDER_THICKNESS;
        origin_y += BORDER_THICKNESS;
        width -= 2 * BORDER_THICKNESS;
        height -= 2 * BORDER_THICKNESS;
        u32 copy_height = window->video_buffer_size.height < height ? window->video_buffer_size.height : height;
        for (u32 y = 0; y < copy_height; y++) {
            u32 copy_width = window->video_buffer_size.width < width ? window->video_buffer_size.width : width;
            memcpy(
                screen_buffer + 3 * (screen_size.width * (origin_y + y) + origin_x),
                window->video_buffer + 3 * window->video_buffer_size.width * y,
                3 * copy_width
            );
            memset(screen_buffer + 3 * (screen_size.width * (origin_y + y) + origin_x + copy_width), 0, 3 * (width - copy_width));
        }
        for (u32 y = copy_height; y < height; y++)
            memset(screen_buffer + 3 * (screen_size.width * (origin_y + y) + origin_x), 0, 3 * width);
        break;
    }
    case CONTAINER_SPLIT_HORIZONTAL:
        for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
            draw_container(child, origin_x + get_child_offset(child, width), origin_y, get_child_length(child, width), height);
        break;
    case CONTAINER_SPLIT_VERTICAL:
        for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
            draw_container(child, origin_x, origin_y + get_child_offset(child, height), width, get_child_length(child, height));
        break;
    }
}

// Draw the screen
static void draw_screen(void) {
    if (root_container == NULL) {
        memset(screen_buffer, 0x30, 3 * screen_size.width * screen_size.height);
    } else {
        // Draw the root container to the screen buffer
        draw_container(root_container, 0, 0, screen_size.width, screen_size.height);
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
    }
    // Send the screen buffer
    channel_send(video_data_channel, &(SendMessage){1, &(SendMessageData){3 * screen_size.width * screen_size.height, screen_buffer}, 0, NULL}, 0);
}

typedef enum State : u32 {
    STATE_NORMAL,
    STATE_WINDOW_CREATE,
} State;

typedef enum ModKeys : u32 {
    MOD_KEY_LEFT_META = UINT32_C(1) << 0,
    MOD_KEY_RIGHT_META = UINT32_C(1) << 1,
    MOD_KEY_LEFT_SHIFT = UINT32_C(1) << 2,
    MOD_KEY_RIGHT_SHIFT = UINT32_C(1) << 3,
    MOD_KEY_LEFT_CTRL = UINT32_C(1) << 4,
    MOD_KEY_RIGHT_CTRL = UINT32_C(1) << 5,
} ModKeys;

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
    screen_buffer = malloc(3 * screen_size.width * screen_size.height);
    if (screen_buffer == NULL)
        return;
    State state = STATE_NORMAL;
    ModKeys mod_keys_held = 0;
    draw_screen();
    while (1) {
        handle_t msg;
        MessageTag tag;
        err = mqueue_receive(event_queue, &tag, &msg, 0);
        if (err)
            continue;
        switch ((EventSource)tag.data[0]) {
        case EVENT_KEYBOARD_DATA: {
            // Read key event
            KeyEvent key_event;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            handle_free(msg);
            // Update mod key states
            ModKeys mod_key;
            switch (key_event.keycode) {
            case KEY_LEFT_META:
                mod_key = MOD_KEY_LEFT_META;
                break;
            case KEY_RIGHT_META:
                mod_key = MOD_KEY_RIGHT_META;
                break;
            case KEY_LEFT_SHIFT:
                mod_key = MOD_KEY_LEFT_SHIFT;
                break;
            case KEY_RIGHT_SHIFT:
                mod_key = MOD_KEY_RIGHT_SHIFT;
                break;
            case KEY_LEFT_CTRL:
                mod_key = MOD_KEY_LEFT_CTRL;
                break;
            case KEY_RIGHT_CTRL:
                mod_key = MOD_KEY_RIGHT_CTRL;
                break;
            default:
                mod_key = 0;
                break;
            }
            if (key_event.pressed)
                mod_keys_held |= mod_key;
            else
                mod_keys_held &= ~mod_key;
            // Check for directional keys
            bool direction_selected = false;
            Direction direction;
            switch (key_event.keycode) {
            case KEY_LEFT:
            case KEY_H:
                direction_selected = true;
                direction = DIRECTION_LEFT;
                break;
            case KEY_DOWN:
            case KEY_J:
                direction_selected = true;
                direction = DIRECTION_DOWN;
                break;
            case KEY_UP:
            case KEY_K:
                direction_selected = true;
                direction = DIRECTION_UP;
                break;
            case KEY_RIGHT:
            case KEY_L:
                direction_selected = true;
                direction = DIRECTION_RIGHT;
                break;
            default:
                break;
            }
            // Handle the event
            bool meta_held = mod_keys_held & (MOD_KEY_LEFT_META | MOD_KEY_RIGHT_META);
            bool shift_held = mod_keys_held & (MOD_KEY_LEFT_SHIFT | MOD_KEY_RIGHT_SHIFT);
            bool ctrl_held = mod_keys_held & (MOD_KEY_LEFT_CTRL | MOD_KEY_RIGHT_CTRL);
            switch (state) {
            case STATE_NORMAL:
                if (meta_held && key_event.pressed) {
                    if (direction_selected) {
                        if (ctrl_held) {
                            if (shift_held)
                                resize_focused_window(direction, - RESIZE_PIXELS);
                            else
                                resize_focused_window(direction, RESIZE_PIXELS);
                        } else {
                            switch_focused_window(direction);
                        }
                    } else if (key_event.keycode == KEY_ENTER) {
                        if (root_container != NULL)
                            state = STATE_WINDOW_CREATE;
                        else
                            add_new_window_next_to_focused(DIRECTION_UP);
                    }
                    draw_screen();
                } else if (!meta_held && key_event.keycode != KEY_LEFT_META && key_event.keycode != KEY_RIGHT_META && root_container != NULL) {
                    // Send the key event to the focused window
                    handle_t keyboard_data_in = root_container->focused_window->keyboard_data_in;
                    channel_send(keyboard_data_in, &(SendMessage){1, &(SendMessageData){sizeof(KeyEvent), &key_event}, 0, NULL}, FLAG_NONBLOCK);
                }
                break;
            case STATE_WINDOW_CREATE:
                if (key_event.pressed) {
                    if (direction_selected)
                        add_new_window_next_to_focused(direction);
                    state = STATE_NORMAL;
                }
                break;
            }
            break;
        }
        case EVENT_MOUSE_DATA: {
            // Read mouse update
            MouseUpdate mouse_update;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(MouseUpdate), &mouse_update, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            handle_free(msg);
            // Get the window pointed at before updating the cursor
            WindowContainer *old_pointed_at_window = get_pointed_at_window(NULL);
            // Update the cursor position
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
            // Get the window pointed at after updating the cursor
            ScreenPos window_origin;
            WindowContainer *pointed_at_window = get_pointed_at_window(&window_origin);
            if (pointed_at_window != NULL) {
                // If the cursor moved to another window, focus on it
                if (pointed_at_window != old_pointed_at_window)
                    set_focused_window(pointed_at_window);
                // Send the mouse update to the window pointed at
                handle_t mouse_data_in = pointed_at_window->mouse_data_in;
                mouse_update.abs_x = cursor.x - window_origin.x;
                mouse_update.abs_y = cursor.y - window_origin.y;
                channel_send(mouse_data_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseUpdate), &mouse_update}, 0, NULL}, FLAG_NONBLOCK);
            }
            break;
        }
        case EVENT_VIDEO_SIZE: {
            ScreenSize window_size;
            get_window_size((WindowContainer *)tag.data[1], &window_size);
            err = message_read_bounded(msg, &(ReceiveMessage){0, NULL, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &window_size}, 0, NULL});
            break;
        }
        case EVENT_VIDEO_DATA: {
            ScreenSize window_size;
            WindowContainer *window = (WindowContainer *)tag.data[1];
            get_window_size(window, &window_size);
            // Get the dimensions of the received buffer
            ScreenSize video_buffer_size;
            err = message_read_bounded(msg, &(ReceiveMessage){sizeof(ScreenSize), &video_buffer_size, 0, NULL}, NULL, NULL, &error_replies(ERR_INVALID_ARG), FLAG_ALLOW_PARTIAL_READ);
            if (err)
                continue;
            // Extend the window buffer if necessary
            size_t window_data_size = 3 * video_buffer_size.width * video_buffer_size.height;
            if (window->video_buffer_capacity < window_data_size) {
                size_t new_video_buffer_capacity = window->video_buffer_capacity;
                while (new_video_buffer_capacity < window_data_size)
                    new_video_buffer_capacity *= 2;
                u8 *new_video_buffer = realloc(window->video_buffer, new_video_buffer_capacity);
                if (new_video_buffer == NULL) {
                    handle_free(msg);
                    continue;
                }
                window->video_buffer = new_video_buffer;
                window->video_buffer_capacity = new_video_buffer_capacity;
            }
            window->video_buffer_size = video_buffer_size;
            // Read the video data
            err = message_read_bounded(msg, &(ReceiveMessage){window_data_size, window->video_buffer, 0, NULL}, &(MessageLength){sizeof(ScreenSize), 0}, NULL, &error_replies(ERR_INVALID_ARG), 0);
            if (err)
                continue;
            handle_free(msg);
            draw_screen();
            break;
        }
        }
    }
}
