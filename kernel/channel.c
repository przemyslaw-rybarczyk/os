#include "types.h"
#include "channel.h"

#include "alloc.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"

#define CHANNEL_MAX_QUEUE_LENGTH 16

typedef struct Channel {
    spinlock_t lock;
    size_t refcount;
    Process *blocked_receiver;
    ProcessQueue blocked_senders;
    size_t queue_length;
    Message *queue_start;
    Message *queue_end;
} Channel;

// Create a message from a given data buffer
Message *message_alloc(size_t data_size, u8 *data) {
    Message *message = malloc(sizeof(Message));
    if (message == NULL)
        return NULL;
    memset(message, 0, sizeof(Message));
    message->data_size = data_size;
    message->data = data;
    return message;
}

// Free a message along with its data buffer
void message_free(Message *message) {
    free(message->data);
    free(message);
}

// Reply to a message
err_t message_reply(Message *message, Message *reply) {
    // Set the reply error code to 0 (success)
    if (message->reply_error != NULL)
        *(message->reply_error) = 0;
    // Set the reply if one is wanted
    // Otherwise, free the reply since it's no longer needed
    if (message->reply != NULL)
        *(message->reply) = reply;
    else
        message_free(reply);
    // If there is a sender blocked waiting for a reply, unblock it
    if (message->blocked_sender != NULL)
        process_enqueue(message->blocked_sender);
    message->blocked_sender = NULL;
    return 0;
}

// Reply to a message with an error code
err_t message_reply_error(Message *message, err_t error) {
    // Set the reply error code if one is wanted
    if (message->reply_error != NULL)
        *(message->reply_error) = error;
    // If there is a sender blocked waiting for a reply, unblock it
    if (message->blocked_sender != NULL)
        process_enqueue(message->blocked_sender);
    message->blocked_sender = NULL;
    return 0;
}

// Create a channel
Channel *channel_alloc(void) {
    Channel *channel = malloc(sizeof(Channel));
    if (channel == NULL)
        return NULL;
    memset(channel, 0, sizeof(Channel));
    channel->refcount = 1;
    return channel;
}

// Increment the channel reference count
void channel_add_ref(Channel *channel) {
    spinlock_acquire(&channel->lock);
    channel->refcount += 1;
    spinlock_release(&channel->lock);
}

// Decrement the channel reference count and free it if there are no remaining references
void channel_del_ref(Channel *channel) {
    spinlock_acquire(&channel->lock);
    channel->refcount -= 1;
    if (channel->refcount == 0) {
        for (Message *message = channel->queue_start; message != NULL; ) {
            Message *next_message = message->next_message;
            message_free(message);
            message = next_message;
        }
        free(channel);
    } else {
        spinlock_release(&channel->lock);
    }
}

// Send a message on a channel
err_t channel_send(Channel *channel, Message *message, Message **reply) {
    spinlock_acquire(&channel->lock);
    // If the queue is full, block until there is space
    while (channel->queue_length >= CHANNEL_MAX_QUEUE_LENGTH) {
        process_queue_add(&channel->blocked_senders, cpu_local->current_process);
        process_block(&channel->lock);
        spinlock_acquire(&channel->lock);
    }
    err_t reply_error;
    // Set the reply information
    message->reply_error = &reply_error;
    message->reply = reply;
    message->blocked_sender = cpu_local->current_process;
    // Add the message to the channel queue
    message->next_message = NULL;
    if (channel->queue_start == NULL) {
        channel->queue_start = message;
        channel->queue_end = message;
    } else {
        channel->queue_end->next_message = message;
        channel->queue_end = message;
    }
    channel->queue_length += 1;
    // If there is a receiver blocked waiting for a message, unblock it
    if (channel->blocked_receiver != NULL)
        process_enqueue(channel->blocked_receiver);
    channel->blocked_receiver = NULL;
    // Block and wait for a reply
    process_block(&channel->lock);
    return reply_error;
}

// Receive a message from a channel
err_t channel_receive(Channel *channel, Message **message_ptr) {
    spinlock_acquire(&channel->lock);
    // If there are no messages in the queue, block until a message arrives
    while (channel->queue_start == NULL) {
        channel->blocked_receiver = cpu_local->current_process;
        process_block(&channel->lock);
        spinlock_acquire(&channel->lock);
    }
    // Remove a message from the queue
    *message_ptr = channel->queue_start;
    channel->queue_start = channel->queue_start->next_message;
    channel->queue_length -= 1;
    // If there is a blocked sender, unblock it
    Process *blocked_sender = process_queue_remove(&channel->blocked_senders);
    if (blocked_sender != NULL)
        process_enqueue(blocked_sender);
    spinlock_release(&channel->lock);
    return 0;
}

// Return a message to the front of the channel queue
// Used when an error occurs
static void channel_return_message(Channel *channel, Message *message) {
    spinlock_acquire(&channel->lock);
    message->next_message = channel->queue_start;
    channel->queue_start = message;
    channel->queue_length += 1;
    spinlock_release(&channel->lock);
}

// Verify that a buffer provided by a process is contained within the process address space
// This does not handle the cases where an address is not mapped by the process - in those cases a page fault will occur and the process will be killed.
static err_t verify_user_buffer(void *start, size_t length) {
    u64 start_addr = (u64)start;
    if (start_addr + length < start_addr)
        return ERR_INVALID_ADDRESS;
    if (start_addr + length > USER_ADDR_UPPER_BOUND)
        return ERR_INVALID_ADDRESS;
    return 0;
}

// Returns the length of the message
err_t syscall_message_get_length(handle_t i, size_t *length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE && handle.type != HANDLE_TYPE_REPLY)
        return ERR_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(length, sizeof(size_t));
    if (err)
        return err;
    // Copy the length
    *length = handle.message->data_size;
    return 0;
}

// Reads the message data into the provided userspace buffer
// The buffer must be large enough to fit the entire message.
err_t syscall_message_read(handle_t i, void *data) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE && handle.type != HANDLE_TYPE_REPLY)
        return ERR_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, handle.message->data_size);
    if (err)
        return err;
    // Copy the data
    memcpy(data, handle.message->data, handle.message->data_size);
    return 0;
}

// Send a message on a channel and wait for a reply
err_t syscall_channel_call(handle_t channel_i, size_t message_size, void *message_data_user, handle_t *reply_i_ptr) {
    err_t err;
    Handle channel_handle;
    // Verify buffers are valid
    err = verify_user_buffer(message_data_user, message_size);
    if (err)
        return err;
    if (reply_i_ptr != NULL) {
        err = verify_user_buffer(reply_i_ptr, sizeof(handle_t));
        if (err)
            return err;
    }
    // Get the channel from handle
    err = process_get_handle(channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_SEND)
        return ERR_WRONG_HANDLE_TYPE;
    // Copy the message data
    void *message_data = malloc(message_size);
    if (message_data == NULL && message_size != 0)
        return ERR_NO_MEMORY;
    memcpy(message_data, message_data_user, message_size);
    // Create a message
    Message *message = message_alloc(message_size, message_data);
    if (message == NULL) {
        free(message_data);
        return ERR_NO_MEMORY;
    }
    // Send the message
    Message *reply;
    err = channel_send(channel_handle.channel, message, &reply);
    if (err)
        return err;
    // Add the reply handle
    if (reply_i_ptr != NULL) {
        err = process_add_handle((Handle){HANDLE_TYPE_REPLY, {.message = reply}}, reply_i_ptr);
        if (err)
            return err;
    }
    return 0;
}

// Get a message from a channel
err_t syscall_channel_receive(handle_t channel_i, handle_t *message_i_ptr) {
    err_t err;
    Handle channel_handle;
    // Verify buffer is valid
    err = verify_user_buffer(message_i_ptr, sizeof(handle_t));
    if (err)
        return err;
    // Get the channel from handle
    err = process_get_handle(channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_RECEIVE)
        return ERR_WRONG_HANDLE_TYPE;
    // Receive a message
    Message *message;
    err = channel_receive(channel_handle.channel, &message);
    if (err)
        return err;
    // Add the handle
    err = process_add_handle((Handle){HANDLE_TYPE_MESSAGE, {.message = message}}, message_i_ptr);
    if (err) {
        channel_return_message(channel_handle.channel, message);
        return err;
    }
    return 0;
}

err_t syscall_message_reply(handle_t message_i, size_t reply_size, void *reply_data_user) {
    err_t err;
    Handle message_handle;
    // Verify buffer is valid
    err = verify_user_buffer(reply_data_user, reply_size);
    if (err)
        return err;
    // Get the message from handle
    err = process_get_handle(message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_WRONG_HANDLE_TYPE;
    // Copy the message data
    void *reply_data = malloc(reply_size);
    if (reply_data == NULL && reply_size != 0)
        return ERR_NO_MEMORY;
    memcpy(reply_data, reply_data_user, reply_size);
    // Create a reply
    Message *reply = message_alloc(reply_size, reply_data);
    if (reply == NULL) {
        free(reply_data);
        return ERR_NO_MEMORY;
    }
    // Send the reply
    err = message_reply(message_handle.message, reply);
    if (err)
        return err;
    // Free message and handle
    process_clear_handle(message_i);
    return 0;
}

err_t syscall_message_reply_error(handle_t message_i, err_t error) {
    err_t err;
    Handle message_handle;
    // Check error code is not reserved by the kernel
    if (error <= ERR_KERNEL_MAX)
        return ERR_INVALID_ARG;
    // Get the message from handle
    err = process_get_handle(message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_WRONG_HANDLE_TYPE;
    // Send the error
    err = message_reply_error(message_handle.message, error);
    if (err)
        return err;
    // Free message and handle
    process_clear_handle(message_i);
    return 0;
}
