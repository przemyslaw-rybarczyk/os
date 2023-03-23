#pragma once

#include "types.h"
#include "error.h"

typedef struct Message {
    size_t data_size;
    u8 *data;
} Message;

typedef enum HandleType {
    HANDLE_TYPE_EMPTY,
    HANDLE_TYPE_MESSAGE,
} HandleType;

typedef struct Handle {
    HandleType type;
    union {
        Message *message;
    };
} Handle;

typedef struct HandleList {
    size_t length;
    Handle *handles;
} HandleList;

err_t handle_list_init(HandleList *list);
void handle_list_free(HandleList *list);
err_t handle_add(HandleList *list, Handle handle, size_t *i_ptr);
err_t handle_remove(HandleList *list, size_t i);
err_t handle_get(HandleList *list, size_t i, Handle *handle);
