#pragma once

#include "types.h"
#include "error.h"

#include "channel.h"

typedef enum HandleType {
    HANDLE_TYPE_EMPTY,
    HANDLE_TYPE_MESSAGE,
    HANDLE_TYPE_REPLY,
    HANDLE_TYPE_CHANNEL,
    HANDLE_TYPE_MESSAGE_QUEUE,
} HandleType;

typedef struct Handle {
    HandleType type;
    union {
        Message *message;
        Channel *channel;
        MessageQueue *mqueue;
    };
} Handle;

typedef struct HandleList {
    size_t length;
    Handle *handles;
} HandleList;

err_t handle_list_init(HandleList *list);
void handle_list_free(HandleList *list);
void handle_clear(HandleList *list, handle_t i);
err_t handle_add(HandleList *list, Handle handle, handle_t *i_ptr);
err_t handle_remove(HandleList *list, handle_t i);
err_t handle_get(HandleList *list, handle_t i, Handle *handle);
err_t handle_set(HandleList *list, handle_t i, Handle handle);
