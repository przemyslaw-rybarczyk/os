#pragma once

#include <zr/types.h>
#include <zr/error.h>

#define MAP_PAGES_WRITE (UINT64_C(1) << 0)
#define MAP_PAGES_EXECUTE (UINT64_C(1) << 1)

typedef struct MessageTag {
    uintptr_t data[2];
} MessageTag;

typedef enum AttachedHandleType : uintptr_t {
    ATTACHED_HANDLE_TYPE_CHANNEL_SEND,
    ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE,
} AttachedHandleType;

typedef struct MessageLength {
    size_t data;
    size_t handles;
} MessageLength;

#define ATTACHED_HANDLE_FLAG_MOVE (UINT64_C(1) << 0)

typedef struct SendAttachedHandle {
    u64 flags;
    handle_t handle_i;
} SendAttachedHandle;

typedef struct ReceiveAttachedHandle {
    AttachedHandleType type;
    handle_t handle_i;
} ReceiveAttachedHandle;

typedef struct SendMessage {
    MessageLength length;
    const void *data;
    const SendAttachedHandle *handles;
} SendMessage;

typedef struct ReceiveMessage {
    MessageLength length;
    void *data;
    ReceiveAttachedHandle *handles;
} ReceiveMessage;

typedef struct ErrorReplies {
    err_t data_low;
    err_t data_high;
    err_t handles_low;
    err_t handles_high;
} ErrorReplies;

#ifndef _KERNEL

err_t map_pages(u64 start, u64 length, u64 flags);
_Noreturn void process_exit(void);
void process_yield(void);
err_t message_get_length(handle_t i, MessageLength *length);
err_t message_read(handle_t i, void *data, ReceiveAttachedHandle *handles);
err_t channel_call(handle_t channel_i, const SendMessage *message, handle_t *reply_i_ptr);
err_t mqueue_receive(handle_t mqueue_i, MessageTag *tag, handle_t *message_i_ptr);
err_t message_reply(handle_t message_i, const SendMessage *message);
void handle_free(handle_t i);
err_t message_reply_error(handle_t message_i, err_t error);
err_t message_read_bounded(handle_t i, ReceiveMessage *message, const MessageLength *min_length, const ErrorReplies *errors);
err_t reply_read_bounded(handle_t i, ReceiveMessage *message, const MessageLength *min_length);
err_t channel_call_bounded(handle_t channel_i, const SendMessage *message, ReceiveMessage *reply, const MessageLength *min_length);
err_t channel_get(const char *name, handle_t *handle_i);
err_t mqueue_create(handle_t *handle_i_ptr);
err_t mqueue_add_channel(handle_t mqueue_i, handle_t channel_i, MessageTag tag);
err_t mqueue_add_channel_resource(handle_t mqueue_i, const char *channel_name, MessageTag tag);
err_t channel_create(handle_t *channel_send_i, handle_t *channel_receive_i);

#define error_replies(error) ((ErrorReplies){(error), (error), (error), (error)})

static inline err_t message_read_sized(handle_t i, size_t length, void *data, err_t error) {
    return message_read_bounded(i, &(ReceiveMessage){{length, 0}, data, NULL}, NULL, &(ErrorReplies){error, error, error, error});
}

static inline err_t reply_read_sized(handle_t i, size_t length, void *data) {
    return reply_read_bounded(i, &(ReceiveMessage){{length, 0}, data, NULL}, NULL);
}

static inline err_t channel_call_sized(handle_t i, const SendMessage *message, size_t reply_length, void *reply_data) {
    return channel_call_bounded(i, message, &(ReceiveMessage){{reply_length, 0}, reply_data, NULL}, NULL);
}

#endif
