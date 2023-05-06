#include "types.h"
#include "channel.h"

#include "alloc.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"

#define MESSAGE_QUEUE_MAX_LENGTH 16

typedef struct MessageQueue {
    spinlock_t lock;
    size_t refcount;
    Process *blocked_receiver;
    ProcessQueue blocked_senders;
    size_t length;
    Message *start;
    Message *end;
} MessageQueue;

typedef struct Channel {
    spinlock_t lock;
    size_t refcount;
    MessageQueue *queue;
    uintptr_t tag[2];
} Channel;

// Create a message from a given data buffer
Message *message_alloc(size_t data_size, const void *data) {
    // Allocate message
    Message *message = malloc(sizeof(Message));
    if (message == NULL)
        return NULL;
    memset(message, 0, sizeof(Message));
    message->data_size = data_size;
    // Allocate message data
    message->data = malloc(data_size);
    if (message->data == NULL && data_size != 0) {
        free(message);
        return NULL;
    }
    memcpy(message->data, data, data_size);
    return message;
}

// Free a message along with its data buffer
void message_free(Message *message) {
    if (message == NULL)
        return;
    free(message->data);
    free(message);
}

// Reply to a message
void message_reply(Message *message, Message *reply) {
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
}

// Reply to a message with an error code
void message_reply_error(Message *message, err_t error) {
    // Set the reply error code if one is wanted
    if (message->reply_error != NULL)
        *(message->reply_error) = error;
    // If there is a sender blocked waiting for a reply, unblock it
    if (message->blocked_sender != NULL)
        process_enqueue(message->blocked_sender);
    message->blocked_sender = NULL;
}

// Create a message queue
MessageQueue *mqueue_alloc(void) {
    MessageQueue *mqueue = malloc(sizeof(MessageQueue));
    if (mqueue == NULL)
        return NULL;
    memset(mqueue, 0, sizeof(MessageQueue));
    mqueue->refcount = 1;
    return mqueue;
}

// Increment the message queue reference count
void mqueue_add_ref(MessageQueue *queue) {
    spinlock_acquire(&queue->lock);
    queue->refcount += 1;
    spinlock_release(&queue->lock);
}

// Decrement the message queue reference count and free it if there are no remaining references
void mqueue_del_ref(MessageQueue *queue) {
    spinlock_acquire(&queue->lock);
    queue->refcount -= 1;
    if (queue->refcount == 0) {
        for (Message *message = queue->start; message != NULL; ) {
            Message *next_message = message->next_message;
            message_free(message);
            message = next_message;
        }
        free(queue);
    } else {
        spinlock_release(&queue->lock);
    }
}

// Send a message to a message queue and wait for a reply
err_t mqueue_call(MessageQueue *queue, Message *message, Message **reply) {
    spinlock_acquire(&queue->lock);
    // If the queue is full, block until there is space
    while (queue->length >= MESSAGE_QUEUE_MAX_LENGTH) {
        process_queue_add(&queue->blocked_senders, cpu_local->current_process);
        process_block(&queue->lock);
        spinlock_acquire(&queue->lock);
    }
    err_t reply_error;
    // Set the reply information
    message->reply_error = &reply_error;
    message->reply = reply;
    message->blocked_sender = cpu_local->current_process;
    // Add the message to the queue
    message->next_message = NULL;
    if (queue->start == NULL) {
        queue->start = message;
        queue->end = message;
    } else {
        queue->end->next_message = message;
        queue->end = message;
    }
    queue->length += 1;
    // If there is a receiver blocked waiting for a message, unblock it
    if (queue->blocked_receiver != NULL)
        process_enqueue(queue->blocked_receiver);
    queue->blocked_receiver = NULL;
    // Block and wait for a reply
    process_block(&queue->lock);
    return reply_error;
}

// Receive a message from a queue
void mqueue_receive(MessageQueue *queue, Message **message_ptr) {
    spinlock_acquire(&queue->lock);
    // If there are no messages in the queue, block until a message arrives
    while (queue->start == NULL) {
        queue->blocked_receiver = cpu_local->current_process;
        process_block(&queue->lock);
        spinlock_acquire(&queue->lock);
    }
    // Remove a message from the queue
    *message_ptr = queue->start;
    queue->start = queue->start->next_message;
    queue->length -= 1;
    // If there is a blocked sender, unblock it
    Process *blocked_sender = process_queue_remove(&queue->blocked_senders);
    if (blocked_sender != NULL)
        process_enqueue(blocked_sender);
    spinlock_release(&queue->lock);
}

// Return a message to the front of the queue
// Used when an error occurs
static void mqueue_return_message(MessageQueue *queue, Message *message) {
    spinlock_acquire(&queue->lock);
    message->next_message = queue->start;
    queue->start = message;
    queue->length += 1;
    spinlock_release(&queue->lock);
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
        mqueue_del_ref(channel->queue);
        free(channel);
    } else {
        spinlock_release(&channel->lock);
    }
}

// Set the channel's message queue and tag
void channel_set_mqueue(Channel *channel, MessageQueue *mqueue, uintptr_t tag[2]) {
    mqueue_add_ref(mqueue);
    channel->queue = mqueue;
    channel->tag[0] = tag[0];
    channel->tag[1] = tag[1];
}

// Send a message on a channel and wait for a reply
err_t channel_call(Channel *channel, Message *message, Message **reply) {
    message->tag[0] = channel->tag[0];
    message->tag[1] = channel->tag[1];
    return mqueue_call(channel->queue, message, reply);
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
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(length, sizeof(size_t));
    if (err)
        return err;
    // Copy the length
    if (handle.message != NULL)
        *length = handle.message->data_size;
    else
        *length = 0;
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
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, handle.message->data_size);
    if (err)
        return err;
    // Copy the data
    if (handle.message != NULL)
        memcpy(data, handle.message->data, handle.message->data_size);
    return 0;
}

// Send a message on a channel and wait for a reply
err_t syscall_channel_call(handle_t channel_i, size_t message_size, const void *message_data, handle_t *reply_i_ptr) {
    err_t err;
    Handle channel_handle;
    // Verify buffers are valid
    err = verify_user_buffer(message_data, message_size);
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
    if (channel_handle.type != HANDLE_TYPE_CHANNEL)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message = message_alloc(message_size, message_data);
    if (message == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Send the message
    Message *reply;
    err = channel_call(channel_handle.channel, message, &reply);
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
err_t syscall_mqueue_receive(handle_t mqueue_i, uintptr_t tag[2], handle_t *message_i_ptr) {
    err_t err;
    Handle mqueue_handle;
    // Verify buffer is valid
    err = verify_user_buffer(message_i_ptr, sizeof(handle_t));
    if (err)
        return err;
    // Get the channel from handle
    err = process_get_handle(mqueue_i, &mqueue_handle);
    if (err)
        return err;
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Receive a message
    Message *message;
    mqueue_receive(mqueue_handle.mqueue, &message);
    // Return the tag
    if (tag != NULL) {
        tag[0] = message->tag[0];
        tag[1] = message->tag[1];
    }
    // Add the handle
    err = process_add_handle((Handle){HANDLE_TYPE_MESSAGE, {.message = message}}, message_i_ptr);
    if (err) {
        mqueue_return_message(mqueue_handle.mqueue, message);
        return err;
    }
    return 0;
}

err_t syscall_message_reply(handle_t message_i, size_t reply_size, const void *reply_data) {
    err_t err;
    Handle message_handle;
    // Verify buffer is valid
    err = verify_user_buffer(reply_data, reply_size);
    if (err)
        return err;
    // Get the message from handle
    err = process_get_handle(message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a reply
    // If the reply size is zero, the reply is set to NULL and no allocation occurs.
    Message *reply;
    if (reply_size != 0) {
        // Allocate the reply
        reply = message_alloc(reply_size, reply_data);
        if (reply == NULL)
            return ERR_KERNEL_NO_MEMORY;
    } else {
        reply = NULL;
    }
    // Send the reply
    message_reply(message_handle.message, reply);
    // Free message and handle
    process_clear_handle(message_i);
    return 0;
}

err_t syscall_message_reply_error(handle_t message_i, err_t error) {
    err_t err;
    Handle message_handle;
    // Check error code is not reserved by the kernel or zero
    if (error >= ERR_KERNEL_MIN || error == 0)
        return ERR_KERNEL_INVALID_ARG;
    // Get the message from handle
    err = process_get_handle(message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Send the error
    message_reply_error(message_handle.message, error);
    // Free message and handle
    process_clear_handle(message_i);
    return 0;
}

// Read the contents of a message with bounds checking
// Functions like message_read(), but if the message size is outside of the given bounds it instead replies with a given error code
// and returns either ERR_KERNEL_MESSAGE_TOO_SHORT or ERR_KERNEL_MESSAGE_TOO_LONG.
err_t syscall_message_read_bounded(handle_t i, void *data, size_t *length_ptr, size_t min_length, size_t max_length, err_t err_low, err_t err_high) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, max_length);
    if (err)
        return err;
    if (length_ptr != NULL) {
        err = verify_user_buffer(length_ptr, sizeof(size_t));
        if (err)
            return err;
    }
    // Check provided error codes are not reserved or zero
    if (err_low >= ERR_KERNEL_MIN || err_low == 0 || err_high >= ERR_KERNEL_MIN || err_high == 0)
        return ERR_KERNEL_INVALID_ARG;
    // Perform bounds check
    size_t length = handle.message != NULL ? handle.message->data_size : 0;
    if (length < min_length) {
        message_reply_error(handle.message, err_low);
        process_clear_handle(i);
        return ERR_KERNEL_MESSAGE_TOO_SHORT;
    }
    if (length > max_length) {
        message_reply_error(handle.message, err_high);
        process_clear_handle(i);
        return ERR_KERNEL_MESSAGE_TOO_LONG;
    }
    // Copy the data and length
    if (handle.message != NULL)
        memcpy(data, handle.message->data, handle.message->data_size);
    if (length_ptr != NULL)
        *length_ptr = length;
    return 0;
}

// Read the contents of a reply with bounds checking
// Functions like message_read_bounded(), but frees the reply instead of replying to it.
err_t syscall_reply_read_bounded(handle_t i, void *data, size_t *length_ptr, size_t min_length, size_t max_length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = process_get_handle(i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_REPLY)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, max_length);
    if (err)
        return err;
    if (length_ptr != NULL) {
        err = verify_user_buffer(length_ptr, sizeof(size_t));
        if (err)
            return err;
    }
    // Perform bounds check
    size_t length = handle.message != NULL ? handle.message->data_size : 0;
    if (length < min_length) {
        process_clear_handle(i);
        return ERR_KERNEL_MESSAGE_TOO_SHORT;
    }
    if (length > max_length) {
        process_clear_handle(i);
        return ERR_KERNEL_MESSAGE_TOO_LONG;
    }
    // Copy the data and length
    if (handle.message != NULL)
        memcpy(data, handle.message->data, handle.message->data_size);
    if (length_ptr != NULL)
        *length_ptr = length;
    return 0;
}

// Send a message on a channel, wait for a reply and check its size against the given bounds
// Functions similar to channel_call() followed by reply_read_bounded() and handle_free()
err_t syscall_channel_call_bounded(handle_t channel_i, size_t message_size, const void *message_data, void *reply_data, size_t *reply_length_ptr, size_t min_length, size_t max_length) {
    err_t err;
    Handle channel_handle;
    // Verify buffers are valid
    err = verify_user_buffer(message_data, message_size);
    if (err)
        return err;
    err = verify_user_buffer(reply_data, max_length);
    if (err)
        return err;
    if (reply_length_ptr != NULL) {
        err = verify_user_buffer(reply_length_ptr, sizeof(size_t));
        if (err)
            return err;
    }
    // Get the channel from handle
    err = process_get_handle(channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message = message_alloc(message_size, message_data);
    if (message == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Send the message
    Message *reply;
    err = channel_call(channel_handle.channel, message, &reply);
    if (err)
        return err;
    // Perform bounds check on the reply
    size_t length = reply != NULL ? reply->data_size : 0;
    if (length < min_length) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_TOO_SHORT;
    }
    if (length > max_length) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_TOO_LONG;
    }
    // Copy the data and length
    if (reply != NULL)
        memcpy(reply_data, reply->data, reply->data_size);
    if (reply_length_ptr != NULL)
        *reply_length_ptr = length;
    message_free(reply);
    return 0;
}

// Create a new message queue
err_t syscall_mqueue_create(handle_t *handle_i_ptr) {
    err_t err;
    MessageQueue *mqueue = mqueue_alloc();
    if (mqueue == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Verify buffer is valid
    err = verify_user_buffer(handle_i_ptr, sizeof(handle_t));
    if (err)
        return err;
    // Add the handle
    err = process_add_handle((Handle){HANDLE_TYPE_MESSAGE_QUEUE, {.mqueue = mqueue}}, handle_i_ptr);
    if (err)
        return err;
    return 0;
}
