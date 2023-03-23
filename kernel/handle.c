#include "types.h"
#include "handle.h"

#include "alloc.h"
#include "string.h"

#define HANDLE_LIST_DEFAULT_LENGTH 8

static void handle_free(Handle handle) {
    switch (handle.type) {
    case HANDLE_TYPE_EMPTY:
        break;
    case HANDLE_TYPE_MESSAGE:
        free(handle.message->data);
        free(handle.message);
        break;
    }
}

// Initialize a handle list
err_t handle_list_init(HandleList *list) {
    list->length = HANDLE_LIST_DEFAULT_LENGTH;
    list->handles = malloc(list->length * sizeof(Handle));
    if (list->handles == NULL)
        return ERR_NO_MEMORY;
    memset(list->handles, 0, list->length * sizeof(Handle));
    return 0;
}

// Free a handle list
void handle_list_free(HandleList *list) {
    for (size_t i = 0; i < list->length; i++)
        handle_free(list->handles[i]);
    free(list->handles);
}

// Add a handle to the list in the first empty slot
err_t handle_add(HandleList *list, Handle handle, size_t *i_ptr) {
    // Search for the first empty slot
    for (size_t i = 0; i < list->length; i++) {
        if (list->handles[i].type == HANDLE_TYPE_EMPTY) {
            if (i_ptr)
                *i_ptr = i;
            list->handles[i] = handle;
            return 0;
        }
    }
    // If there are none, try to extend the list
    size_t new_length = 2 * list->length;
    Handle *new_handles = realloc(list->handles, new_length);
    if (new_handles == NULL)
        return ERR_NO_MEMORY;
    if (i_ptr)
        *i_ptr = list->length;
    list->length = new_length;
    list->handles = new_handles;
    return 0;
}

// Remove a handle from a list
err_t handle_remove(HandleList *list, size_t i) {
    if (i >= list->length)
        return ERR_INVALID_HANDLE;
    if (list->handles[i].type == HANDLE_TYPE_EMPTY)
        return ERR_INVALID_HANDLE;
    handle_free(list->handles[i]);
    list->handles[i].type = HANDLE_TYPE_EMPTY;
    return 0;
}

// Get the contents of a handle
err_t handle_get(HandleList *list, size_t i, Handle *handle) {
    if (i >= list->length)
        return ERR_INVALID_HANDLE;
    if (list->handles[i].type == HANDLE_TYPE_EMPTY)
        return ERR_INVALID_HANDLE;
    *handle = list->handles[i];
    return 0;
}
