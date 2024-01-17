#pragma once

#include <zr/types.h>
#include <zr/error.h>

#define MAP_PAGES_WRITE (UINT64_C(1) << 0)
#define MAP_PAGES_EXECUTE (UINT64_C(1) << 1)
#define FLAG_NONBLOCK (UINT64_C(1) << 0)
#define FLAG_ALLOW_PARTIAL_DATA_READ (UINT64_C(1) << 1)
#define FLAG_ALLOW_PARTIAL_HANDLES_READ (UINT64_C(1) << 2)
#define FLAG_FREE_MESSAGE (UINT64_C(1) << 3)
#define FLAG_PRIORITIZE_TIMEOUT (UINT64_C(1) << 4)
#define FLAG_REPLY_ON_FAILURE (UINT64_C(1) << 5)

typedef struct MessageTag {
    uintptr_t data[2];
} MessageTag;

typedef enum AttachedHandleType : uintptr_t {
    ATTACHED_HANDLE_TYPE_CHANNEL_SEND,
    ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE,
} AttachedHandleType;

typedef enum ResourceType : uintptr_t {
    RESOURCE_TYPE_EMPTY,
    RESOURCE_TYPE_CHANNEL_SEND,
    RESOURCE_TYPE_CHANNEL_RECEIVE,
    RESOURCE_TYPE_MESSAGE,
} ResourceType;

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

typedef struct SendMessageData {
    size_t length;
    const void *data;
} SendMessageData;

typedef struct SendMessageHandles {
    size_t length;
    const SendAttachedHandle *handles;
} SendMessageHandles;

typedef struct SendMessage {
    size_t data_buffers_num;
    const SendMessageData *data_buffers;
    size_t handles_buffers_num;
    const SendMessageHandles *handles_buffers;
} SendMessage;

typedef struct ReceiveMessage {
    size_t data_length;
    void *data;
    size_t handles_length;
    ReceiveAttachedHandle *handles;
} ReceiveMessage;

#define RESOURCE_NAME_MAX 32

typedef struct ResourceName {
    u8 bytes[RESOURCE_NAME_MAX];
} ResourceName;

// Convert a string to a resource name
// The string is padded with zeroes if shorter than RESOURCE_NAME_MAX and truncated if longer.
static inline ResourceName resource_name(const char *str) {
    ResourceName name;
    size_t i = 0;
    for (; i < RESOURCE_NAME_MAX && str[i] != 0; i++)
        name.bytes[i] = str[i];
    for (; i < RESOURCE_NAME_MAX; i++)
        name.bytes[i] = 0;
    return name;
}

#define resource_name(str) (*(ResourceName[]){resource_name(str)})

#define TIMEOUT_NONE INT64_MAX

#ifndef _KERNEL

err_t map_pages(u64 start, u64 length, u64 flags);
_Noreturn void process_exit(void);
void process_yield(void);
err_t message_get_length(handle_t i, MessageLength *length);
err_t message_read(handle_t i, ReceiveMessage *message, const MessageLength *offset, const MessageLength *min_length, err_t reply_error, u64 flags);
err_t channel_call(handle_t channel_i, const SendMessage *message, handle_t *reply_i_ptr);
err_t mqueue_receive(handle_t mqueue_i, MessageTag *tag, handle_t *message_i_ptr, i64 timeout, u64 flags);
err_t message_reply(handle_t message_i, const SendMessage *message, u64 flags);
void handle_free(handle_t i);
err_t message_reply_error(handle_t message_i, err_t error, u64 flags);
err_t channel_call_read(handle_t channel_i, const SendMessage *message, ReceiveMessage *reply, const MessageLength *min_length);
err_t resource_get(const ResourceName *name, ResourceType type, handle_t *handle_i);
err_t mqueue_create(handle_t *handle_i_ptr);
err_t mqueue_add_channel(handle_t mqueue_i, handle_t channel_i, MessageTag tag);
err_t mqueue_add_channel_resource(handle_t mqueue_i, const ResourceName *channel_name, MessageTag tag);
err_t channel_create(handle_t *channel_send_i, handle_t *channel_receive_i);
err_t channel_send(handle_t channel_i, const SendMessage *message, u64 flags);
void time_get(i64 *time_ptr);
err_t message_resource_read(const ResourceName *name, size_t data_length, void *data, size_t min_data_length, u64 flags);
void process_time_get(i64 *time_ptr);
void process_wait(i64 time);
err_t channel_call_async(handle_t channel_i, const SendMessage *message, handle_t mqueue_i, MessageTag tag);

#endif
