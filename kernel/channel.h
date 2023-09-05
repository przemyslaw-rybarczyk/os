#pragma once

#include "types.h"
#include "error.h"

#include <zr/syscalls.h>

typedef struct Channel Channel;
typedef struct MessageQueue MessageQueue;
typedef struct Process Process;

typedef struct AttachedHandle {
    AttachedHandleType type;
    union {
        Channel *channel;
    };
} AttachedHandle;

typedef struct Message {
    MessageTag tag;
    size_t data_size;
    void *data;
    size_t handles_size;
    AttachedHandle *handles;
    err_t *reply_error;
    struct Message **reply;
    Process *blocked_sender;
    struct Message *next_message;
} Message;

Message *message_alloc_copy(size_t data_size, const void *data);
void message_free(Message *message);
void message_reply(Message *message, Message *reply);
void message_reply_error(Message *message, err_t error);

MessageQueue *mqueue_alloc(void);
void mqueue_add_ref(MessageQueue *queue);
void mqueue_del_ref(MessageQueue *queue);
void mqueue_close(MessageQueue *queue);
err_t mqueue_receive(MessageQueue *queue, Message **message_ptr, bool nonblock);

Channel *channel_alloc(void);
void channel_add_ref(Channel *channel);
void channel_del_ref(Channel *channel);
err_t channel_set_mqueue(Channel *channel, MessageQueue *mqueue, MessageTag tag);
err_t channel_send(Channel *channel, Message *message, bool nonblock);
err_t channel_call(Channel *channel, Message *message, Message **reply);

err_t syscall_message_get_length(handle_t i, MessageLength *length);
err_t syscall_message_read(handle_t i, void *data, ReceiveAttachedHandle *handles);
err_t syscall_channel_send(handle_t channel_i, const SendMessage *user_message, u64 flags);
err_t syscall_channel_call(handle_t channel_i, const SendMessage *user_message, handle_t *reply_i_ptr);
err_t syscall_mqueue_receive(handle_t mqueue_i, MessageTag *tag, handle_t *message_i_ptr, u64 flags);
err_t syscall_message_reply(handle_t message_i, const SendMessage *user_message);
err_t syscall_message_reply_error(handle_t message_i, err_t error);
err_t syscall_message_read_bounded(handle_t i, ReceiveMessage *user_message, const MessageLength *min_length, const ErrorReplies *errors);
err_t syscall_reply_read_bounded(handle_t i, ReceiveMessage *user_message, const MessageLength *min_length);
err_t syscall_channel_call_bounded(handle_t channel_i, const SendMessage *user_message, ReceiveMessage *user_reply, const MessageLength *min_length);
err_t syscall_mqueue_create(handle_t *handle_i_ptr);
err_t syscall_mqueue_add_channel(handle_t mqueue_i, handle_t channel_i, MessageTag tag);
err_t syscall_channel_create(handle_t *channel_send_i_ptr, handle_t *channel_receive_i_ptr);
