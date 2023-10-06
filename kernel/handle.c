#include "types.h"
#include "handle.h"

#include "alloc.h"
#include "channel.h"
#include "string.h"

#define HANDLE_LIST_DEFAULT_LENGTH 8

// Special value for first_free_handle and next_free_handle - indicates no free handles are available
#define NO_NEXT_HANDLE SIZE_MAX

static void handle_free(Handle handle) {
    switch (handle.type) {
    case HANDLE_TYPE_EMPTY:
        break;
    case HANDLE_TYPE_MESSAGE:
        message_free(handle.message);
        break;
    case HANDLE_TYPE_CHANNEL_SEND:
        channel_del_ref(handle.channel);
        break;
    case HANDLE_TYPE_CHANNEL_RECEIVE:
        channel_close(handle.channel);
        channel_del_ref(handle.channel);
        break;
    case HANDLE_TYPE_MESSAGE_QUEUE:
        mqueue_close(handle.mqueue);
        mqueue_del_ref(handle.mqueue);
        break;
    }
}

// Initialize a handle list
err_t handle_list_init(HandleList *list) {
    list->length = HANDLE_LIST_DEFAULT_LENGTH;
    list->free_handles = list->length;
    list->handles = malloc(list->length * sizeof(Handle));
    if (list->handles == NULL)
        return ERR_KERNEL_NO_MEMORY;
    for (size_t i = 0; i < list->length - 1; i++)
        list->handles[i] = (Handle){HANDLE_TYPE_EMPTY, .next_free_handle = i + 1};
    list->handles[list->length - 1] = (Handle){HANDLE_TYPE_EMPTY, .next_free_handle = NO_NEXT_HANDLE};
    list->first_free_handle = 0;
    return 0;
}

// Free a handle list
void handle_list_free(HandleList *list) {
    for (handle_t i = 0; i < list->length; i++)
        handle_free(list->handles[i]);
    free(list->handles);
}

// Clear a handle in a list
// If `free` is set, frees the handle as well.
void handle_clear(HandleList *list, handle_t i, bool free) {
    if (i >= list->length)
        return;
    if (free)
        handle_free(list->handles[i]);
    list->handles[i] = (Handle){HANDLE_TYPE_EMPTY, .next_free_handle = list->first_free_handle};
    list->first_free_handle = i;
    list->free_handles += 1;
}

// Extend the handle list to length `new_length`
static err_t handle_list_extend(HandleList *list, size_t new_length) {
    Handle *new_handles = realloc(list->handles, new_length * sizeof(Handle));
    if (new_handles == NULL)
        return ERR_KERNEL_NO_MEMORY;
    for (size_t i = list->length; i < new_length - 1; i++)
        new_handles[i] = (Handle){HANDLE_TYPE_EMPTY, .next_free_handle = i + 1};
    new_handles[new_length - 1] = (Handle){HANDLE_TYPE_EMPTY, .next_free_handle = list->first_free_handle};
    list->first_free_handle = list->length;
    list->free_handles += new_length - list->length;
    list->length = new_length;
    list->handles = new_handles;
    return 0;
}

// Add a handle to the list in the first empty slot
err_t handle_add(HandleList *list, Handle handle, handle_t *i_ptr) {
    err_t err;
    // Search for the first empty slot
    if (list->first_free_handle != NO_NEXT_HANDLE) {
        size_t i = list->first_free_handle;
        list->first_free_handle = list->handles[i].next_free_handle;
        list->free_handles -= 1;
        list->handles[i] = handle;
        *i_ptr = i;
        return 0;
    }
    // If there are none, extend the list
    err = handle_list_extend(list, 2 * list->length);
    if (err)
        return err;
    size_t i = list->first_free_handle;
    list->first_free_handle = list->handles[i].next_free_handle;
    list->free_handles -= 1;
    list->handles[i] = handle;
    *i_ptr = i;
    return 0;
}

// Get the contents of a handle
err_t handle_get(HandleList *list, handle_t i, Handle *handle) {
    if (i >= list->length)
        return ERR_KERNEL_INVALID_HANDLE;
    if (list->handles[i].type == HANDLE_TYPE_EMPTY)
        return ERR_KERNEL_INVALID_HANDLE;
    *handle = list->handles[i];
    return 0;
}

// Set the contents of a handle
err_t handle_set(HandleList *list, handle_t i, Handle handle) {
    err_t err;
    // If the handle is too large, try extend the list so that it fits
    if (i >= list->length) {
        size_t new_length = list->length;
        while (new_length <= i)
            new_length *= 2;
        err = handle_list_extend(list, new_length);
        if (err)
            return err;
    }
    // If filling an empty handle, shrink the free handle count
    if (list->handles[i].type == HANDLE_TYPE_EMPTY)
        list->free_handles -= 1;
    // Set the handle
    list->handles[i] = handle;
    return 0;
}

// Ensure at least n handles can be allocated without errors
err_t handles_reserve(HandleList *list, size_t n) {
    err_t err;
    // Extend the list to have at least n free slots
    if (n >= list->free_handles) {
        size_t new_length = list->length;
        while (new_length <= list->length + (n - list->free_handles))
            new_length *= 2;
        err = handle_list_extend(list, new_length);
        if (err)
            return err;
    }
    return 0;
}
