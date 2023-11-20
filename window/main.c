#include <zr/types.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zr/error.h>
#include <zr/keyboard.h>
#include <zr/mouse.h>
#include <zr/syscalls.h>
#include <zr/timezone.h>
#include <zr/video.h>

#include "font.h"
#include "included_programs.h"

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

// Each element represents one line of the cursor.
// Two bits represent one pixel - more significant bits are on the left.
// 00/01 - transparency
// 10 - contour
// 11 - inside
static u32 cursor_image[CURSOR_HEIGHT] = {
    0x80000000,
    0xA0000000,
    0xB8000000,
    0xBE000000,
    0xBF800000,
    0xBFE00000,
    0xBFF80000,
    0xBFFE0000,
    0xBFFF8000,
    0xBFFFE000,
    0xBFFFF800,
    0xBFFFFE00,
    0xBFFEAA00,
    0xBFBE0000,
    0xBE2F8000,
    0xB82F8000,
    0xA00BE000,
    0x000BE000,
    0x00028000,
};

typedef enum EventSource : uintptr_t {
    EVENT_KEYBOARD_KEY,
    EVENT_MOUSE_BUTTON,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_SCROLL,
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
    handle_t keyboard_key_in;
    handle_t mouse_button_in;
    handle_t mouse_move_in;
    handle_t mouse_scroll_in;
    handle_t window_close_in;
} WindowContainer;

typedef struct SplitContainer {
    Container header;
    Container *first_child;
} SplitContainer;

// Each element is the root of a container tree for a given workspace, covering the whole screen
static Container *root_container[9] = {0};
static u32 current_workspace = 0;

static ScreenPos cursor;

static u8 *screen_buffer;

static const u8 border_color_unfocused[3] = {0xB0, 0x90, 0xFF};
static const u8 border_color_focused[3] = {0x70, 0x50, 0xFF};
static const u8 status_bar_color[3] = {0x60, 0x40, 0xFF};
static const u8 status_bar_text_color[3] = {0xFF, 0xFF, 0xFF};

#define BORDER_THICKNESS 3
#define RESIZE_PIXELS 5
#define STATUS_BAR_HEIGHT (FONT_HEIGHT + 4)

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

typedef enum State : u32 {
    STATE_NORMAL,
    STATE_WINDOW_CREATE,
    STATE_WINDOW_RESIZE,
} State;

static State state = STATE_NORMAL;
static Container *resize_container;
static Direction resize_direction;
static i32 resize_starting_position;

#define VIDEO_BUFFER_DEFAULT_SIZE 16384

// Create a new window with an attached process
static WindowContainer *create_window(void) {
    err_t err;
    // Allocate channels for the new process
    handle_t video_size_in, video_size_out;
    handle_t video_data_in, video_data_out;
    handle_t video_resize_in, video_resize_out;
    handle_t keyboard_key_in, keyboard_key_out;
    handle_t mouse_button_in, mouse_button_out;
    handle_t mouse_move_in, mouse_move_out;
    handle_t mouse_scroll_in, mouse_scroll_out;
    handle_t text_stdout_in, text_stdout_out;
    handle_t text_stderr_in, text_stderr_out;
    handle_t text_stdin_in, text_stdin_out;
    handle_t window_close_in, window_close_out;
    err = channel_create(&video_size_in, &video_size_out);
    if (err)
        goto fail_video_size_channel_create;
    err = channel_create(&video_data_in, &video_data_out);
    if (err)
        goto fail_video_data_channel_create;
    err = channel_create(&video_resize_in, &video_resize_out);
    if (err)
        goto fail_video_resize_channel_create;
    err = channel_create(&keyboard_key_in, &keyboard_key_out);
    if (err)
        goto fail_keyboard_key_channel_create;
    err = channel_create(&mouse_button_in, &mouse_button_out);
    if (err)
        goto fail_mouse_button_channel_create;
    err = channel_create(&mouse_move_in, &mouse_move_out);
    if (err)
        goto fail_mouse_move_channel_create;
    err = channel_create(&mouse_scroll_in, &mouse_scroll_out);
    if (err)
        goto fail_mouse_scroll_channel_create;
    err = channel_create(&text_stdout_in, &text_stdout_out);
    if (err)
        goto fail_stdout_channel_create;
    err = channel_create(&text_stderr_in, &text_stderr_out);
    if (err)
        goto fail_stderr_channel_create;
    err = channel_create(&text_stdin_in, &text_stdin_out);
    if (err)
        goto fail_stdin_channel_create;
    err = channel_create(&window_close_in, &window_close_out);
    if (err)
        goto fail_window_close_channel_create;
    // Allocate window
    WindowContainer *window = malloc(sizeof(WindowContainer));
    if (window == NULL)
        goto fail_window_alloc;
    window->header.type = CONTAINER_WINDOW;
    window->video_resize_in = video_resize_in;
    window->keyboard_key_in = keyboard_key_in;
    window->mouse_button_in = mouse_button_in;
    window->mouse_move_in = mouse_move_in;
    window->mouse_scroll_in = mouse_scroll_in;
    window->window_close_in = window_close_in;
    window->video_buffer_size = (ScreenSize){0, 0};
    window->video_buffer_capacity = VIDEO_BUFFER_DEFAULT_SIZE;
    window->video_buffer = malloc(window->video_buffer_capacity);
    if (window->video_buffer == NULL)
        goto fail_video_buffer_alloc;
    // Spawn terminal process
    ResourceName program1_resource_names[] = {
        resource_name("video/size"),
        resource_name("video/data"),
        resource_name("video/resize"),
        resource_name("keyboard/key"),
        resource_name("mouse/button"),
        resource_name("mouse/move"),
        resource_name("mouse/scroll"),
        resource_name("text/stdout_r"),
        resource_name("text/stderr_r"),
        resource_name("text/stdin_r"),
        resource_name("window/close"),
    };
    SendAttachedHandle program1_resource_handles[] = {
        {ATTACHED_HANDLE_FLAG_MOVE, video_size_in},
        {ATTACHED_HANDLE_FLAG_MOVE, video_data_in},
        {ATTACHED_HANDLE_FLAG_MOVE, video_resize_out},
        {ATTACHED_HANDLE_FLAG_MOVE, keyboard_key_out},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_button_out},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_move_out},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_scroll_out},
        {ATTACHED_HANDLE_FLAG_MOVE, text_stdout_out},
        {ATTACHED_HANDLE_FLAG_MOVE, text_stderr_out},
        {ATTACHED_HANDLE_FLAG_MOVE, text_stdin_out},
        {ATTACHED_HANDLE_FLAG_MOVE, window_close_out},
    };
    err = channel_call(process_spawn_channel, &(SendMessage){
        3, (SendMessageData[]){
            {sizeof(size_t), &(size_t){0}},
            {sizeof(program1_resource_names), program1_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program1_resource_handles) / sizeof(program1_resource_handles[0]), program1_resource_handles}
    }, NULL);
    if (err)
        goto fail_process_spawn;
    // Spawn process running in terminal
    Timezone timezone = timezone_get();
    ResourceName program2_resource_names[] = {
        resource_name("locale/timezone"),
        resource_name("text/stdout"),
        resource_name("text/stderr"),
        resource_name("text/stdin"),
    };
    SendAttachedHandle program2_resource_handles[] = {
        {ATTACHED_HANDLE_FLAG_MOVE, text_stdout_in},
        {ATTACHED_HANDLE_FLAG_MOVE, text_stderr_in},
        {ATTACHED_HANDLE_FLAG_MOVE, text_stdin_in},
    };
    err = channel_call(process_spawn_channel, &(SendMessage){
        5, (SendMessageData[]){
            {sizeof(size_t), &(size_t){1}},
            {sizeof(program2_resource_names), program2_resource_names},
            {sizeof(size_t), &(size_t){sizeof(Timezone)}},
            {sizeof(Timezone), &timezone},
            {included_file_program2_end - included_file_program2, included_file_program2}},
        1, &(SendMessageHandles){sizeof(program2_resource_handles) / sizeof(program2_resource_handles[0]), program2_resource_handles}
    }, NULL);
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
    handle_free(window_close_in);
    handle_free(window_close_out);
fail_window_close_channel_create:
    handle_free(text_stdin_in);
    handle_free(text_stdin_out);
fail_stdin_channel_create:
    handle_free(text_stderr_in);
    handle_free(text_stderr_out);
fail_stderr_channel_create:
    handle_free(text_stdout_in);
    handle_free(text_stdout_out);
fail_stdout_channel_create:
    handle_free(mouse_scroll_in);
    handle_free(mouse_scroll_out);
fail_mouse_scroll_channel_create:
    handle_free(mouse_move_in);
    handle_free(mouse_move_out);
fail_mouse_move_channel_create:
    handle_free(mouse_button_in);
    handle_free(mouse_button_out);
fail_mouse_button_channel_create:
    handle_free(keyboard_key_in);
    handle_free(keyboard_key_out);
fail_keyboard_key_channel_create:
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

// Free window object
static void window_free(WindowContainer *window) {
    free(window->video_buffer);
    handle_free(window->video_resize_in);
    handle_free(window->keyboard_key_in);
    handle_free(window->mouse_button_in);
    handle_free(window->mouse_move_in);
    handle_free(window->mouse_scroll_in);
    handle_free(window->window_close_in);
}

// Allocate and initialize a split container
static SplitContainer *split_container_alloc(void) {
    SplitContainer *split = malloc(sizeof(SplitContainer));
    if (split == NULL)
        return NULL;
    memset(split, 0, sizeof(SplitContainer));
    return split;
}

// Get the offset of the origin of a container within its parent's length
static i32 get_child_offset(const Container *child, i32 parent_length) {
    return (i32)(child->offset_in_parent * parent_length + 0.5);
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
        container_size->width = screen_size.width;
        container_size->height = screen_size.height - STATUS_BAR_HEIGHT;
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
    window_size->width = container_size.width >= 2 * BORDER_THICKNESS ? container_size.width - 2 * BORDER_THICKNESS : 0;
    window_size->height = container_size.height >= 2 * BORDER_THICKNESS ? container_size.height - 2 * BORDER_THICKNESS : 0;
}

// Get the window that the cursor is currently in
// Sets window_origin to the origin of the window if it's not null.
// Returns NULL if not pointing at a window.
static WindowContainer *get_pointed_at_window(ScreenPos *window_origin) {
    if (root_container[current_workspace] == NULL)
        return NULL;
    Container *container = root_container[current_workspace];
    // The position and properties of the current container
    i32 origin_x = 0;
    i32 origin_y = 0;
    i32 width = screen_size.width;
    i32 height = screen_size.height - STATUS_BAR_HEIGHT;
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
            for (Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.x < origin_x + get_child_offset(child->next_sibling, width)) {
                    origin_x += get_child_offset(child, width);
                    width = get_child_length(child, width);
                    container = child;
                    break;
                }
            }
            break;
        case CONTAINER_SPLIT_VERTICAL:
            for (Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.y < origin_y + get_child_offset(child->next_sibling, height)) {
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

// Check if cursor is currently pointing at the edge of a container
// If it is, sets *direction appropriately and returns the container.
// The highest container with the edge in the hierarchy is selected.
// If not pointing at an edge, returns NULL.
static Container *get_pointed_at_edge(Direction *direction) {
    if (root_container[current_workspace] == NULL)
        return NULL;
    Container *container = root_container[current_workspace];
    // The position and properties of the current container
    i32 origin_x = 0;
    i32 origin_y = 0;
    i32 width = screen_size.width;
    i32 height = screen_size.height - STATUS_BAR_HEIGHT;
    while (1) {
        // Check if cursor lies at a border and return if it does
        if (cursor.x >= origin_x && cursor.x < origin_x + BORDER_THICKNESS) {
            *direction = DIRECTION_LEFT;
            return container;
        }
        if (cursor.x < origin_x + width && cursor.x >= origin_x + width - BORDER_THICKNESS) {
            *direction = DIRECTION_RIGHT;
            return container;
        }
        if (cursor.y >= origin_y && cursor.y < origin_y + BORDER_THICKNESS) {
            *direction = DIRECTION_UP;
            return container;
        }
        if (cursor.y < origin_y + height && cursor.y >= origin_y + height - BORDER_THICKNESS) {
            *direction = DIRECTION_DOWN;
            return container;
        }
        // Go down the container tree based on the cursor position
        switch (container->type) {
        case CONTAINER_WINDOW:
            return NULL;
        case CONTAINER_SPLIT_HORIZONTAL:
            for (Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.x < origin_x + get_child_offset(child->next_sibling, width)) {
                    origin_x += get_child_offset(child, width);
                    width = get_child_length(child, width);
                    container = child;
                    break;
                }
            }
            break;
        case CONTAINER_SPLIT_VERTICAL:
            for (Container *child = ((SplitContainer *)container)->first_child;; child = child->next_sibling) {
                if (child->next_sibling == NULL || cursor.y < origin_y + get_child_offset(child->next_sibling, height)) {
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
    if (container->parent == NULL)
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
    if (root_container[current_workspace] == NULL)
        return;
    Container *forward_sibling = get_sibling_of_ancestor_in_direction(&root_container[current_workspace]->focused_window->header, direction);
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
static void container_resize(Container *container, Direction side, i32 diff) {
    if (root_container[current_workspace] == NULL)
        return;
    container = get_ancestor_with_sibling_in_direction(container, side);
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

// Resize a container and its siblings after it has been inserted into its parent
// Its length will be set to 1 / (number of siblings including itself).
static void resize_after_insertion(Container *container) {
    // Count siblings
    size_t num_siblings = 0;
    for (Container *child = container->parent->first_child; child != NULL; child = child->next_sibling)
        num_siblings++;
    // Shrink all siblings to make room for new container
    for (Container *child = container->parent->first_child; child != NULL; child = child->next_sibling)
        if (child != container)
            child->offset_in_parent *= (double)(num_siblings - 1) / num_siblings;
    // Set the new container's offset to the offset of the one coming after it
    container->offset_in_parent = container->next_sibling != NULL
        ? container->next_sibling->offset_in_parent
        : (double)(num_siblings - 1) / num_siblings;
    // Shift all containers after the new one to the right to make room for it
    for (Container *child = container->next_sibling; child != NULL; child = child->next_sibling)
        child->offset_in_parent += 1.0 / num_siblings;
}

// Insert a container before another one
// Does not check if the resulting state is valid.
static void container_insert_before(Container *container, Container *next_container) {
    container->prev_sibling = next_container->prev_sibling;
    container->next_sibling = next_container;
    if (next_container->prev_sibling != NULL)
        next_container->prev_sibling->next_sibling = container;
    next_container->prev_sibling = container;
    container->parent = next_container->parent;
    if (next_container->parent->first_child == next_container)
        next_container->parent->first_child = container;
    resize_after_insertion(container);
}

// Insert a container after another one
// Does not check if the resulting state is valid.
static void container_insert_after(Container *container, Container *prev_container) {
    container->prev_sibling = prev_container;
    container->next_sibling = prev_container->next_sibling;
    if (prev_container->next_sibling != NULL)
        prev_container->next_sibling->prev_sibling = container;
    prev_container->next_sibling = container;
    container->parent = prev_container->parent;
    resize_after_insertion(container);
}

// Remove a container from the its parent
// Does not check if the resulting state is valid.
static void container_remove(Container *container) {
    if (container->prev_sibling != NULL)
        container->prev_sibling->next_sibling = container->next_sibling;
    if (container->next_sibling != NULL)
        container->next_sibling->prev_sibling = container->prev_sibling;
    if (container->parent->first_child == container)
        container->parent->first_child = container->next_sibling;
    // Resize siblings to take up remaining space evenly
    double container_length = (container->next_sibling != NULL ? container->next_sibling->offset_in_parent : 1.0) - container->offset_in_parent;
    for (Container *child = container->next_sibling; child != NULL; child = child->next_sibling)
        child->offset_in_parent -= container_length;
    for (Container *child = container->parent->first_child; child != NULL; child = child->next_sibling)
        child->offset_in_parent *= 1.0 / (1.0 - container_length);
}

// Replace a window with a different one
static void container_replace(Container *container, Container *old_container) {
    container->parent = old_container->parent;
    container->prev_sibling = old_container->prev_sibling;
    container->next_sibling = old_container->next_sibling;
    if (container->prev_sibling != NULL)
        container->prev_sibling->next_sibling = container;
    if (container->next_sibling != NULL)
        container->next_sibling->prev_sibling = container;
    if (old_container->parent != NULL) {
        if (old_container->parent->first_child == old_container)
            old_container->parent->first_child = container;
    } else {
        root_container[current_workspace] = container;
    }
    container->offset_in_parent = old_container->offset_in_parent;
}

// Replace a window with another's children
static void container_replace_with_children(SplitContainer *parent, Container *old_container) {
    if (old_container->parent == NULL) {
        parent->header.parent = NULL;
        parent->header.prev_sibling = NULL;
        parent->header.next_sibling = NULL;
        root_container[current_workspace] = (Container *)parent;
        return;
    }
    Container *last_child = NULL;
    for (Container *child = parent->first_child; child != NULL; child = child->next_sibling) {
        child->parent = old_container->parent;
        last_child = child;
    }
    parent->first_child->prev_sibling = old_container->prev_sibling;
    last_child->next_sibling = old_container->next_sibling;
    if (parent->first_child->prev_sibling != NULL)
        parent->first_child->prev_sibling->next_sibling = parent->first_child;
    if (last_child->next_sibling != NULL)
        last_child->next_sibling->prev_sibling = last_child;
    if (old_container->parent->first_child == old_container)
        old_container->parent->first_child = parent->first_child;
    // Resize children to fit in space left after old container
    double old_container_length =
        (old_container->next_sibling != NULL ? old_container->next_sibling->offset_in_parent : 1.0)
        - old_container->offset_in_parent;
    for (Container *child = parent->first_child; child != NULL; child = child->next_sibling)
        child->offset_in_parent = child->offset_in_parent * old_container_length + old_container->offset_in_parent;
}

// Swap a container with its next sibling
static void container_swap_with_next_sibling(Container *container1) {
    Container *container2 = container1->next_sibling;
    container2->prev_sibling = container1->prev_sibling;
    if (container2->prev_sibling != NULL)
        container2->prev_sibling->next_sibling = container2;
    container1->next_sibling = container2->next_sibling;
    if (container1->next_sibling != NULL)
        container1->next_sibling->prev_sibling = container1;
    container1->prev_sibling = container2;
    container2->next_sibling = container1;
    if (container1->parent->first_child == container1)
        container1->parent->first_child = container2;
    double container1_offset = container1->offset_in_parent;
    container1->offset_in_parent = container2->offset_in_parent;
    container2->offset_in_parent = container1_offset;
}

// Add two children to an uninitialized split container
static void container_add_one_child(SplitContainer *split, Container *child) {
    split->first_child = child;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;
    child->offset_in_parent = 0.0;
    child->parent = split;
}

// Normalize container if it only has one child
// May replace the container with one or multiple other ones.
static void container_normalize(SplitContainer *split) {
    if (split->first_child->next_sibling == NULL) {
        if (split->first_child->type == CONTAINER_WINDOW)
            container_replace(split->first_child, (Container *)split);
        else
            container_replace_with_children((SplitContainer *)split->first_child, (Container *)split);
        free(split);
    }
}

// Add a new window neighboring the focused window in the given direction
static void add_new_window_next_to_focused(Direction side) {
    if (root_container[current_workspace] == NULL) {
        // There are no containers, so the one we create becomes the root
        WindowContainer *window = create_window();
        if (window == NULL)
            return;
        window->header.parent = NULL;
        window->header.prev_sibling = NULL;
        window->header.next_sibling = NULL;
        window->header.focused_window = window;
        window->header.offset_in_parent = 0.0;
        root_container[current_workspace] = (Container *)window;
        return;
    }
    WindowContainer *focused_window = root_container[current_workspace]->focused_window;
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
        split = split_container_alloc();
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
        // Create a new split container containing the old focused window and place it in place of the focused window
        split->header.type = direction_is_horizontal(side) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL;
        container_replace((Container *)split, (Container *)focused_window);
        container_add_one_child(split, (Container *)focused_window);
    }
    // Add the window as a sibling to the focused window
    if (direction_is_forward(side))
        container_insert_after((Container *)window, (Container *)focused_window);
    else
        container_insert_before((Container *)window, (Container *)focused_window);
    // Send messages informing of the resize
    for (Container *child = focused_window->header.parent->first_child; child != NULL; child = child->next_sibling)
        if (child != (Container *)window)
            send_resize_messages(child);
    // Set the new window as focused
    // This will also update all `focused_window` links to have correct values.
    set_focused_window(window);
}

// Close a window
static void close_window(WindowContainer *window) {
    // Ask window process to terminate
    channel_send(root_container[current_workspace]->focused_window->window_close_in, NULL, FLAG_NONBLOCK);
    if (window->header.parent == NULL) {
        // The window is at the root
        root_container[current_workspace] = NULL;
    } else {
        container_remove((Container *)window);
        set_focused_window(window->header.next_sibling != NULL ? window->header.next_sibling->focused_window : window->header.prev_sibling->focused_window);
        send_resize_messages((Container *)window->header.parent);
        container_normalize(window->header.parent);
    }
    // Free the window
    window_free(window);
}

// Move the focused window in a given direction
static void move_focused_window(Direction direction) {
    if (root_container[current_workspace] == NULL)
        return;
    WindowContainer *window = root_container[current_workspace]->focused_window;
    if (window->header.parent == NULL)
        return;
    if (window->header.parent->header.type == (direction_is_horizontal(direction) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL)) {
        // We're moving along the parent's length
        Container *forward_sibling = direction_is_forward(direction) ? window->header.next_sibling : window->header.prev_sibling;
        if (forward_sibling == NULL) {
            // There is no sibling in the direction of movement, so move next to the grandparent
            SplitContainer *parent = window->header.parent;
            SplitContainer *gparent = parent->header.parent;
            if (gparent == NULL)
                return;
            if (gparent->header.parent == NULL) {
                // The grandparent is already at the root, so we have to create a new container above it
                SplitContainer *ggparent = split_container_alloc();
                if (ggparent == NULL)
                    return;
                ggparent->header.type = direction_is_horizontal(direction) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL;
                container_add_one_child(ggparent, (Container *)gparent);
                root_container[current_workspace] = (Container *)ggparent;
            }
            container_remove((Container *)window);
            set_focused_window(window->header.next_sibling != NULL ? window->header.next_sibling->focused_window : window->header.prev_sibling->focused_window);
            if (direction_is_forward(direction))
                container_insert_after((Container *)window, (Container *)gparent);
            else
                container_insert_before((Container *)window, (Container *)gparent);
            container_normalize(parent);
            set_focused_window(window);
            send_resize_messages((Container *)window->header.parent);
        } else if (forward_sibling->type == CONTAINER_WINDOW) {
            // The next sibling in the direction of movement is a window, so we swap it with the current one
            container_swap_with_next_sibling(direction_is_forward(direction) ? (Container *)window : window->header.prev_sibling);
            send_resize_messages((Container *)window);
            send_resize_messages((Container *)forward_sibling);
        } else {
            // The next sibling in the direction of movement is split, so we put the window at its beginning
            SplitContainer *parent = window->header.parent;
            container_remove((Container *)window);
            container_insert_before((Container *)window, ((SplitContainer *)forward_sibling)->first_child);
            send_resize_messages((Container *)parent);
            container_normalize(parent);
            set_focused_window(window);
        }
    } else {
        // We're moving perpendicular to the parent's length - move to next to the parent
        SplitContainer *parent = window->header.parent;
        if (parent->header.parent == NULL) {
            // The parent is already at the root, so we have to create a new container above it
            SplitContainer *gparent = split_container_alloc();
            if (gparent == NULL)
                return;
            gparent->header.type = direction_is_horizontal(direction) ? CONTAINER_SPLIT_HORIZONTAL : CONTAINER_SPLIT_VERTICAL;
            container_add_one_child(gparent, (Container *)parent);
            root_container[current_workspace] = (Container *)gparent;
        }
        container_remove((Container *)window);
        set_focused_window(window->header.next_sibling != NULL ? window->header.next_sibling->focused_window : window->header.prev_sibling->focused_window);
        if (direction_is_forward(direction))
            container_insert_after((Container *)window, (Container *)parent);
        else
            container_insert_before((Container *)window, (Container *)parent);
        container_normalize(parent);
        set_focused_window(window);
        send_resize_messages((Container *)window->header.parent);
    }
}

// Move the focused window to a given workspace
static void move_focused_window_to_workspace(u32 workspace) {
    WindowContainer *window = root_container[current_workspace]->focused_window;
    // If the target workspace only has one window at the root, create a new container above it
    if (root_container[workspace] != NULL && root_container[workspace]->type == CONTAINER_WINDOW) {
        SplitContainer *parent = split_container_alloc();
        if (parent == NULL)
            return;
        parent->header.type = CONTAINER_SPLIT_HORIZONTAL;
        container_add_one_child(parent, (Container *)root_container[workspace]);
        root_container[workspace] = (Container *)parent;
    }
    // Remove window from current workspace
    if (window->header.parent == NULL) {
        // The window is at the root
        root_container[current_workspace] = NULL;
    } else {
        container_remove((Container *)window);
        set_focused_window(window->header.next_sibling != NULL ? window->header.next_sibling->focused_window : window->header.prev_sibling->focused_window);
        send_resize_messages((Container *)window->header.parent);
        container_normalize(window->header.parent);
    }
    // Place window in new workspace
    if (root_container[workspace] == NULL) {
        window->header.parent = NULL;
        window->header.prev_sibling = NULL;
        window->header.next_sibling = NULL;
        window->header.focused_window = window;
        window->header.offset_in_parent = 0.0;
        root_container[workspace] = (Container *)window;
    } else {
        container_insert_before((Container *)window, ((SplitContainer *)root_container[workspace])->first_child);
    }
    send_resize_messages(root_container[workspace]);
    set_focused_window(window);
}

// Draw a solid rectangle
static void draw_rectangle(const u8 *color, i32 origin_x, i32 origin_y, i32 width, i32 height) {
    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;
    if (origin_x < 0)
        origin_x = 0;
    if (origin_x + width >= (i32)screen_size.width)
        width = (i32)screen_size.width - origin_x;
    if (origin_y < 0)
        origin_y = 0;
    if (origin_y + height >= (i32)screen_size.height)
        height = (i32)screen_size.height - origin_y;
    for (i32 y = 0; y < height; y++) {
        for (i32 x = 0; x < width; x++) {
            size_t offset = 3 * (screen_size.width * (origin_y + y) + origin_x + x);
            screen_buffer[offset + 0] = color[0];
            screen_buffer[offset + 1] = color[1];
            screen_buffer[offset + 2] = color[2];
        }
    }
}

typedef struct Rectangle {
    i32 origin_x;
    i32 origin_y;
    i32 width;
    i32 height;
} Rectangle;

// Draw a container onto the screen buffer at a given position
static void draw_container(Container *container, u32 origin_x, u32 origin_y, u32 width, u32 height, Rectangle *resize_edge) {
    if (state == STATE_WINDOW_RESIZE && container == resize_container && width >= 2 * BORDER_THICKNESS && height >= 2 * BORDER_THICKNESS) {
        if (direction_is_horizontal(resize_direction)) {
            *resize_edge = (Rectangle){
                cursor.x - resize_starting_position + (origin_x + (resize_direction == DIRECTION_LEFT ? 0 : width - BORDER_THICKNESS)),
                origin_y + BORDER_THICKNESS,
                BORDER_THICKNESS,
                height - 2 * BORDER_THICKNESS,
            };
        } else {
            *resize_edge = (Rectangle){
                origin_x + BORDER_THICKNESS,
                cursor.y - resize_starting_position + (origin_y + (resize_direction == DIRECTION_UP ? 0 : height - BORDER_THICKNESS)),
                width - 2 * BORDER_THICKNESS,
                BORDER_THICKNESS,
            };
        }
    }
    switch (container->type) {
    case CONTAINER_WINDOW: {
        WindowContainer *window = (WindowContainer *)container;
        // Draw window border
        const u8 *border_color = window == root_container[current_workspace]->focused_window ? border_color_focused : border_color_unfocused;
        if (width <= 2 * BORDER_THICKNESS || height <= 2 * BORDER_THICKNESS) {
            draw_rectangle(border_color, origin_x, origin_y, width, height);
            return;
        }
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
            draw_container(child, origin_x + get_child_offset(child, width), origin_y, get_child_length(child, width), height, resize_edge);
        break;
    case CONTAINER_SPLIT_VERTICAL:
        for (Container *child = ((SplitContainer *)container)->first_child; child != NULL; child = child->next_sibling)
            draw_container(child, origin_x, origin_y + get_child_offset(child, height), width, get_child_length(child, height), resize_edge);
        break;
    }
}

#define STATUS_BAR_NUMBER_WIDTH (FONT_WIDTH + 7)
#define STATUS_BAR_NUMBER_OFFSET 5

static char time_fmt_buf[32];

// Draw the screen
static void draw_screen(void) {
    Rectangle resize_edge = (Rectangle){0, 0, 0, 0};
    // Draw the root container to the screen buffer if the is one, otherwise fill the screen with a gray background
    if (root_container[current_workspace] == NULL)
        memset(screen_buffer, 0x30, 3 * screen_size.width * screen_size.height - STATUS_BAR_HEIGHT);
    else
        draw_container(root_container[current_workspace], 0, 0, screen_size.width, screen_size.height - STATUS_BAR_HEIGHT, &resize_edge);
    // Draw edge showing resize position
    draw_rectangle(border_color_focused, resize_edge.origin_x, resize_edge.origin_y, resize_edge.width, resize_edge.height);
    // Draw the status bar background
    draw_rectangle(status_bar_color, 0, screen_size.height - STATUS_BAR_HEIGHT, screen_size.width, STATUS_BAR_HEIGHT);
    // Draw the workspace indicators
    for (u32 i = 0; i < 9; i++) {
        if (current_workspace == i) {
            draw_rectangle(status_bar_text_color, (STATUS_BAR_NUMBER_WIDTH + 3) * i + 1, screen_size.height - STATUS_BAR_HEIGHT + 1, STATUS_BAR_NUMBER_WIDTH + 2, STATUS_BAR_HEIGHT - 2);
            draw_rectangle(status_bar_color, (STATUS_BAR_NUMBER_WIDTH + 3) * i + 2, screen_size.height - STATUS_BAR_HEIGHT + 2, STATUS_BAR_NUMBER_WIDTH, STATUS_BAR_HEIGHT - 4);
        }
        if (root_container[i] != NULL || current_workspace == i)
            draw_font_char(i + '1', (STATUS_BAR_NUMBER_WIDTH + 3) * i + 1 + STATUS_BAR_NUMBER_OFFSET, screen_size.height - (FONT_HEIGHT + 2), status_bar_text_color, screen_size.width, screen_size.height, screen_buffer);
    }
    // Print the time
    time_t time_ = time(NULL);
    struct tm time_tm;
    if (!localtime_r(&time_, &time_tm))
        goto time_print_fail;
    size_t time_fmt_len = strftime(time_fmt_buf, sizeof(time_fmt_buf), "%F %T", &time_tm);
    if (time_fmt_len == 0)
        goto time_print_fail;
    for (size_t i = 0; i < time_fmt_len; i++)
        draw_font_char(time_fmt_buf[i], screen_size.width - FONT_WIDTH * (time_fmt_len - i) - 2, screen_size.height - (FONT_HEIGHT + 2), status_bar_text_color, screen_size.width, screen_size.height, screen_buffer);
time_print_fail:
    // Draw the cursor
    for (size_t y = 0; y < CURSOR_HEIGHT; y++) {
        for (size_t x = 0; x < CURSOR_WIDTH; x++) {
            if (cursor.x + x < screen_size.width && cursor.y + y < screen_size.height && ((cursor_image[y] << (2 * x)) & 0x80000000)) {
                for (size_t i = 0; i < 3; i++) {
                    screen_buffer[3 * (screen_size.width * (cursor.y + y) + cursor.x + x) + i] = (cursor_image[y] << (2 * x + 1)) & 0x80000000 ? 0x00 : 0xFF;
                }
            }
        }
    }
    // Send the screen buffer
    channel_send(video_data_channel, &(SendMessage){1, &(SendMessageData){3 * screen_size.width * screen_size.height, screen_buffer}, 0, NULL}, 0);
}

typedef enum ModKeys : u32 {
    MOD_KEY_LEFT_META = UINT32_C(1) << 0,
    MOD_KEY_RIGHT_META = UINT32_C(1) << 1,
    MOD_KEY_LEFT_SHIFT = UINT32_C(1) << 2,
    MOD_KEY_RIGHT_SHIFT = UINT32_C(1) << 3,
    MOD_KEY_LEFT_CTRL = UINT32_C(1) << 4,
    MOD_KEY_RIGHT_CTRL = UINT32_C(1) << 5,
} ModKeys;

void main(void) {
    timezone_set((Timezone){4, DST_EU});
    err_t err;
    handle_t video_size_channel;
    err = resource_get(&resource_name("video/size"), RESOURCE_TYPE_CHANNEL_SEND, &video_size_channel);
    if (err)
        return;
    err = resource_get(&resource_name("video/data"), RESOURCE_TYPE_CHANNEL_SEND, &video_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("process/spawn"), RESOURCE_TYPE_CHANNEL_SEND, &process_spawn_channel);
    if (err)
        return;
    err = mqueue_create(&event_queue);
    if (err)
        return;
    err = mqueue_add_channel_resource(event_queue, &resource_name("keyboard/key"), (MessageTag){EVENT_KEYBOARD_KEY, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_queue, &resource_name("mouse/button"), (MessageTag){EVENT_MOUSE_BUTTON, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_queue, &resource_name("mouse/move"), (MessageTag){EVENT_MOUSE_MOVE, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(event_queue, &resource_name("mouse/scroll"), (MessageTag){EVENT_MOUSE_SCROLL, 0});
    if (err)
        return;
    err = channel_call_read(video_size_channel, NULL, &(ReceiveMessage){sizeof(ScreenSize), &screen_size, 0, NULL}, NULL);
    if (err)
        return;
    cursor.x = screen_size.width / 2;
    cursor.y = screen_size.height / 2;
    screen_buffer = malloc(3 * screen_size.width * screen_size.height);
    if (screen_buffer == NULL)
        return;
    ModKeys mod_keys_held = 0;
    draw_screen();
    while (1) {
        handle_t msg;
        MessageTag tag;
        i64 t;
        time_get(&t);
        err = mqueue_receive(event_queue, &tag, &msg, (t / 10000000 + 1) * 10000000, 0);
        if (err) {
            if (err == ERR_KERNEL_TIMEOUT)
                draw_screen();
            continue;
        }
        switch ((EventSource)tag.data[0]) {
        case EVENT_KEYBOARD_KEY: {
            // Read key event
            KeyEvent key_event;
            err = message_read(msg, &(ReceiveMessage){sizeof(KeyEvent), &key_event, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
            if (err)
                continue;
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
            bool workspace_selected = false;
            u32 workspace;
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
                if (KEY_1 <= key_event.keycode && key_event.keycode <= KEY_9) {
                    workspace_selected = true;
                    workspace = key_event.keycode - KEY_1;
                }
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
                                container_resize((Container *)root_container[current_workspace]->focused_window, direction, - RESIZE_PIXELS);
                            else
                                container_resize((Container *)root_container[current_workspace]->focused_window, direction, RESIZE_PIXELS);
                        } else if (shift_held) {
                            move_focused_window(direction);
                        } else {
                            switch_focused_window(direction);
                        }
                    } else if (workspace_selected) {
                        if (shift_held)
                            move_focused_window_to_workspace(workspace);
                        else
                            current_workspace = workspace;
                    } else if (key_event.keycode == KEY_ENTER) {
                        if (root_container[current_workspace] != NULL)
                            state = STATE_WINDOW_CREATE;
                        else
                            add_new_window_next_to_focused(DIRECTION_UP);
                    } else if (key_event.keycode == KEY_Q) {
                        if (root_container[current_workspace] != NULL)
                            close_window(root_container[current_workspace]->focused_window);
                    }
                    draw_screen();
                } else if (!meta_held && key_event.keycode != KEY_LEFT_META && key_event.keycode != KEY_RIGHT_META && root_container[current_workspace] != NULL) {
                    // Send the key event to the focused window
                    handle_t keyboard_data_in = root_container[current_workspace]->focused_window->keyboard_key_in;
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
            case STATE_WINDOW_RESIZE:
                state = STATE_NORMAL;
                draw_screen();
                break;
            }
            break;
        }
        case EVENT_MOUSE_BUTTON: {
            // Read event
            MouseButtonEvent button_event;
            err = message_read(msg, &(ReceiveMessage){sizeof(MouseButtonEvent), &button_event, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
            if (err)
                continue;
            WindowContainer *pointed_at_window = get_pointed_at_window(NULL);
            if (pointed_at_window != NULL) {
                channel_send(pointed_at_window->mouse_button_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseButtonEvent), &button_event}, 0, NULL}, FLAG_NONBLOCK);
                if (button_event.button == MOUSE_BUTTON_LEFT && button_event.pressed) {
                    switch (state) {
                    case STATE_NORMAL:
                        set_focused_window(pointed_at_window);
                        resize_container = get_pointed_at_edge(&resize_direction);
                        if (resize_container != NULL) {
                            resize_starting_position = direction_is_horizontal(resize_direction) ? cursor.x : cursor.y;
                            state = STATE_WINDOW_RESIZE;
                        }
                        draw_screen();
                        break;
                    case STATE_WINDOW_CREATE:
                        state = STATE_NORMAL;
                        break;
                    case STATE_WINDOW_RESIZE:
                        break;
                    }
                } else if (button_event.button == MOUSE_BUTTON_LEFT && !button_event.pressed) {
                    if (state == STATE_WINDOW_RESIZE) {
                        i32 diff = (direction_is_forward(resize_direction) ? 1 : -1) *
                            ((direction_is_horizontal(resize_direction) ? cursor.x : cursor.y) - resize_starting_position);
                        container_resize(resize_container, resize_direction, diff);
                        state = STATE_NORMAL;
                        draw_screen();
                    }
                }
            }
            break;
        }
        case EVENT_MOUSE_MOVE: {
            // Read event
            MouseMoveEvent move_event;
            err = message_read(msg, &(ReceiveMessage){sizeof(MouseMoveEvent), &move_event, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
            if (err)
                continue;
            // Get the window pointed at before updating the cursor
            WindowContainer *old_pointed_at_window = get_pointed_at_window(NULL);
            // Update the cursor position
            cursor.x += move_event.diff_x;
            cursor.y += move_event.diff_y;
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
            switch (state) {
            case STATE_NORMAL:
            case STATE_WINDOW_CREATE: {
                WindowContainer *pointed_at_window = get_pointed_at_window(&window_origin);
                if (pointed_at_window != NULL) {
                    // If the cursor moved to another window, focus on it
                    if (pointed_at_window != old_pointed_at_window)
                        set_focused_window(pointed_at_window);
                    // Send the mouse update to the window pointed at
                    move_event.abs_x = cursor.x - window_origin.x;
                    move_event.abs_y = cursor.y - window_origin.y;
                    channel_send(pointed_at_window->mouse_move_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseMoveEvent), &move_event}, 0, NULL}, FLAG_NONBLOCK);
                }
                break;
            }
            case STATE_WINDOW_RESIZE:
                break;
            }
            draw_screen();
            break;
        }
        case EVENT_MOUSE_SCROLL: {
            // Read event
            MouseScrollEvent scroll_event;
            err = message_read(msg, &(ReceiveMessage){sizeof(MouseScrollEvent), &scroll_event, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
            if (err)
                continue;
            WindowContainer *pointed_at_window = get_pointed_at_window(NULL);
            if (pointed_at_window != NULL)
                channel_send(pointed_at_window->mouse_scroll_in, &(SendMessage){1, &(SendMessageData){sizeof(MouseScrollEvent), &scroll_event}, 0, NULL}, FLAG_NONBLOCK);
            break;
        }
        case EVENT_VIDEO_SIZE: {
            ScreenSize window_size;
            get_window_size((WindowContainer *)tag.data[1], &window_size);
            err = message_read(msg, &(ReceiveMessage){0, NULL, 0, NULL}, NULL, NULL, ERR_INVALID_ARG, 0);
            if (err)
                continue;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(ScreenSize), &window_size}, 0, NULL}, FLAG_FREE_MESSAGE);
            break;
        }
        case EVENT_VIDEO_DATA: {
            ScreenSize window_size;
            WindowContainer *window = (WindowContainer *)tag.data[1];
            get_window_size(window, &window_size);
            // Get the dimensions of the received buffer
            ScreenSize video_buffer_size;
            err = message_read(msg, &(ReceiveMessage){sizeof(ScreenSize), &video_buffer_size, 0, NULL}, NULL, NULL, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
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
            err = message_read(msg, &(ReceiveMessage){window_data_size, window->video_buffer, 0, NULL}, &(MessageLength){sizeof(ScreenSize), 0}, NULL, 0, FLAG_FREE_MESSAGE);
            if (err)
                continue;
            draw_screen();
            break;
        }
        }
    }
}
