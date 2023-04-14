#pragma once

#include "types.h"
#include "error.h"

typedef struct Process Process;

typedef struct Message {
    size_t data_size;
    u8 *data;
    err_t *reply_error;
    struct Message **reply;
    Process *blocked_sender;
    struct Message *next_message;
} Message;

typedef struct Channel Channel;

Message *message_alloc(size_t data_size, u8 *data);
void message_free(Message *message);
err_t message_reply(Message *message, Message *reply);
err_t message_reply_error(Message *message, err_t error);
Channel *channel_alloc(void);
void channel_add_ref(Channel *channel);
void channel_del_ref(Channel *channel);
err_t channel_send(Channel *channel, Message *message, Message **reply);
err_t channel_receive(Channel *channel, Message **message_ptr);
err_t syscall_message_get_length(handle_t i, size_t *length);
err_t syscall_message_read(handle_t i, void *data);
err_t syscall_channel_call(handle_t channel_i, size_t message_size, void *message_data_user, handle_t *reply_i_ptr);
err_t syscall_channel_receive(handle_t channel_i, handle_t *message_i_ptr);
err_t syscall_message_reply(handle_t message_i, size_t reply_size, void *reply_data_user);
err_t syscall_message_reply_error(handle_t message_i, err_t error);
