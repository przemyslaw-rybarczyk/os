#pragma once

#include "types.h"
#include "error.h"

typedef struct Process Process;

// A NULL message pointer is used to represent an empty reply
typedef struct Message {
    uintptr_t tag[2];
    size_t data_size;
    void *data;
    err_t *reply_error;
    struct Message **reply;
    Process *blocked_sender;
    struct Message *next_message;
} Message;

typedef struct MessageQueue MessageQueue;
typedef struct Channel Channel;

Message *message_alloc(size_t data_size, const void *data);
void message_free(Message *message);
void message_reply(Message *message, Message *reply);
void message_reply_error(Message *message, err_t error);

MessageQueue *mqueue_alloc(void);
void mqueue_add_ref(MessageQueue *queue);
void mqueue_del_ref(MessageQueue *queue);
err_t mqueue_call(MessageQueue *queue, Message *message, Message **reply);
void mqueue_receive(MessageQueue *queue, Message **message_ptr);

Channel *channel_alloc(void);
void channel_add_ref(Channel *channel);
void channel_del_ref(Channel *channel);
void channel_set_mqueue(Channel *channel, MessageQueue *mqueue, uintptr_t tag[2]);
err_t channel_call(Channel *channel, Message *message, Message **reply);

err_t syscall_message_get_length(handle_t i, size_t *length);
err_t syscall_message_read(handle_t i, void *data);
err_t syscall_channel_call(handle_t channel_i, size_t message_size, const void *message_data, handle_t *reply_i_ptr);
err_t syscall_mqueue_receive(handle_t mqueue_i, uintptr_t tag[2], handle_t *message_i_ptr);
err_t syscall_message_reply(handle_t message_i, size_t reply_size, const void *reply_data);
err_t syscall_message_reply_error(handle_t message_i, err_t error);
err_t syscall_message_read_bounded(handle_t i, void *data, size_t *length, size_t min_length, size_t max_length, err_t err_low, err_t err_high);
err_t syscall_reply_read_bounded(handle_t i, void *data, size_t *length_ptr, size_t min_length, size_t max_length);
err_t syscall_channel_call_bounded(handle_t channel_i, size_t message_size, const void *message_data, void *reply_data, size_t *reply_length_ptr, size_t min_length, size_t max_length);
err_t syscall_mqueue_create(handle_t *handle_i_ptr);
