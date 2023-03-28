#pragma once

#include "types.h"
#include "error.h"

typedef struct Message Message;
typedef struct Channel Channel;

Message *message_alloc(size_t data_size, u8 *data);
void message_free(Message *message);
Channel *channel_alloc(void);
void channel_add_ref(Channel *channel);
void channel_del_ref(Channel *channel);
err_t channel_send(Channel *channel, Message *message);
err_t channel_receive(Channel *channel, Message **message_ptr);
err_t syscall_message_get_length(size_t i, size_t *length);
err_t syscall_message_read(size_t i, void *data);
err_t syscall_channel_send(size_t channel_i, size_t message_size, void *message_data_user);
err_t syscall_channel_receive(size_t channel_i, size_t *message_i);
