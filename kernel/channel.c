#include "types.h"
#include "channel.h"

#include "alloc.h"
#include "page.h"
#include "percpu.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"

typedef struct Message {
    size_t data_size;
    u8 *data;
    Message *next_message;
} Message;

typedef struct Channel {
    spinlock_t lock;
    size_t refcount;
    Process *blocked_receiver;
    Message *queue_start;
    Message *queue_end;
} Channel;

// Create a message from a given data buffer
Message *message_alloc(size_t data_size, u8 *data) {
    Message *message = malloc(sizeof(Message));
    if (message == NULL)
        return NULL;
    message->data_size = data_size;
    message->data = data;
    return message;
}

// Free a message along with its data buffer
void message_free(Message *message) {
    free(message->data);
    free(message);
}

// Create a channel
Channel *channel_alloc(void) {
    Channel *channel = malloc(sizeof(Channel));
    if (channel == NULL)
        return NULL;
    channel->lock = SPINLOCK_FREE;
    channel->refcount = 1;
    channel->blocked_receiver = NULL;
    channel->queue_start = NULL;
    channel->queue_end = NULL;
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
err_t channel_send(Channel *channel, Message *message) {
    spinlock_acquire(&channel->lock);
    // Add the message to the channel queue
    message->next_message = NULL;
    if (channel->queue_start == NULL) {
        channel->queue_start = message;
        channel->queue_end = message;
    } else {
        channel->queue_end->next_message = message;
        channel->queue_end = message;
    }
    // If there is a received blocked waiting for a message, unblock it
    if (channel->blocked_receiver != NULL)
        process_enqueue(channel->blocked_receiver);
    channel->blocked_receiver = NULL;
    spinlock_release(&channel->lock);
    return 0;
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
    spinlock_release(&channel->lock);
    return 0;
}

// Return a message to the front of the channel queue
// Used when an error occurs
static void channel_return_message(Channel *channel, Message *message) {
    spinlock_acquire(&channel->lock);
    message->next_message = channel->queue_start;
    channel->queue_start = message;
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
err_t syscall_message_get_length(size_t i, size_t *length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
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
err_t syscall_message_read(size_t i, void *data) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, handle.message->data_size);
    if (err)
        return err;
    // Copy the data
    memcpy(data, handle.message->data, handle.message->data_size);
    return 0;
}

err_t syscall_channel_send(size_t channel_i, size_t message_size, void *message_data_user) {
    err_t err;
    Handle channel_handle;
    // Verify buffer is valid
    err = verify_user_buffer(message_data_user, message_size);
    if (err)
        return err;
    // Get the channel from handle
    err = process_get_handle(channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL)
        return ERR_WRONG_HANDLE_TYPE;
    // Copy the message data
    void *message_data = malloc(message_size);
    if (message_data == NULL)
        return ERR_NO_MEMORY;
    memcpy(message_data, message_data_user, message_size);
    // Create a message
    Message *message = message_alloc(message_size, message_data);
    if (message == NULL) {
        free(message_data);
        return ERR_NO_MEMORY;
    }
    // Send the message
    err = channel_send(channel_handle.channel, message);
    if (err) {
        message_free(message);
        return err;
    }
    return 0;
}

err_t syscall_channel_receive(size_t channel_i, size_t *message_i_ptr) {
    err_t err;
    Handle channel_handle;
    // Verify buffer is valid
    err = verify_user_buffer(message_i_ptr, sizeof(size_t *));
    if (err)
        return err;
    // Get the channel from handle
    err = process_get_handle(channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL)
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
