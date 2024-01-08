#include "types.h"
#include "channel.h"

#include "alloc.h"
#include "error.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"
#include "time.h"

#define MESSAGE_QUEUE_MAX_LENGTH 16

typedef struct MessageQueue {
    spinlock_t lock;
    size_t refcount;
    bool closed;
    bool waiting_for_timeout;
    Process *blocked_receiver;
    ProcessQueue blocked_senders;
    size_t length;
    Message *start;
    Message *end;
} MessageQueue;

typedef struct Channel {
    spinlock_t lock;
    size_t refcount;
    bool closed;
    MessageQueue *queue;
    MessageTag tag;
    ProcessQueue blocked_senders;
} Channel;

static void attached_handle_free(AttachedHandle handle) {
    switch (handle.type) {
    case ATTACHED_HANDLE_TYPE_CHANNEL_SEND:
    case ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE:
        channel_del_ref(handle.channel);
        break;
    }
}

static err_t verify_user_send_message(const SendMessage *user_message) {
    err_t err;
    if (user_message == NULL)
        return 0;
    err = verify_user_buffer(user_message, sizeof(SendMessage), false);
    if (err)
        return err;
    err = verify_user_buffer(user_message->data_buffers, user_message->data_buffers_num * sizeof(SendMessageData), false);
    if (err)
        return err;
    for (size_t i = 0; i < user_message->data_buffers_num; i++) {
        err = verify_user_buffer(user_message->data_buffers[i].data, user_message->data_buffers[i].length, false);
        if (err)
            return err;
    }
    err = verify_user_buffer(user_message->handles_buffers, user_message->handles_buffers_num * sizeof(SendMessageHandles), false);
    if (err)
        return err;
    for (size_t i = 0; i < user_message->handles_buffers_num; i++) {
        err = verify_user_buffer(user_message->handles_buffers[i].handles, user_message->handles_buffers[i].length * sizeof(SendAttachedHandle), false);
        if (err)
            return err;
    }
    return 0;
}

static err_t verify_user_receive_message(const ReceiveMessage *user_message) {
    err_t err;
    err = verify_user_buffer(user_message, sizeof(ReceiveMessage), true);
    if (err)
        return err;
    err = verify_user_buffer(user_message->data, user_message->data_length, true);
    if (err)
        return err;
    err = verify_user_buffer(user_message->handles, user_message->handles_length * sizeof(ReceiveAttachedHandle), true);
    if (err)
        return err;
    return 0;
}

// Create a message from a given data buffer
Message *message_alloc_copy(size_t data_size, const void *data) {
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
    message->handles_size = 0;
    message->handles = NULL;
    return message;
}

// Create a message from a user-provided message specification
static err_t message_alloc_user(const SendMessage *user_message, Message **message_ptr, Message *message) {
    err_t err;
    // If the user message is NULL, allocate an empty message
    if (user_message == NULL) {
        if (message == NULL) {
            message = malloc(sizeof(Message));
            if (message == NULL)
                return ERR_KERNEL_NO_MEMORY;
        }
        memset(message, 0, sizeof(Message));
        *message_ptr = message;
        return 0;
    }
    // Calculate total data and handles length
    size_t data_length = 0;
    for (size_t i = 0; i < user_message->data_buffers_num; i++)
        data_length += user_message->data_buffers[i].length;
    size_t handles_length = 0;
    for (size_t i = 0; i < user_message->handles_buffers_num; i++)
        handles_length += user_message->handles_buffers[i].length;
    // Allocate data buffer
    void *data = malloc(data_length);
    if (data_length != 0 && data == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Allocate handle list
    AttachedHandle *handles = malloc(handles_length * sizeof(AttachedHandle));
    if (handles_length != 0 && handles == NULL) {
        free(data);
        return ERR_KERNEL_NO_MEMORY;
    }
    // Allocate the message
    bool message_allocated = false;
    if (message == NULL) {
        message_allocated = true;
        message = malloc(sizeof(Message));
        if (message == NULL) {
            free(handles);
            free(data);
            return ERR_KERNEL_NO_MEMORY;
        }
    }
    memset(message, 0, sizeof(Message));
    message->data_size = data_length;
    message->data = data;
    message->handles_size = handles_length;
    message->handles = handles;
    // Verify the handles
    for (size_t buffer_i = 0; buffer_i < user_message->handles_buffers_num; buffer_i++) {
        const SendMessageHandles *buffer = &user_message->handles_buffers[buffer_i];
        for (size_t handle_i = 0; handle_i < buffer->length; handle_i++) {
            Handle handle;
            err = handle_get(&cpu_local->current_process->handles, buffer->handles[handle_i].handle_i, &handle);
            if (err)
                goto fail;
            switch (handle.type) {
            case HANDLE_TYPE_CHANNEL_SEND:
                break;
            case HANDLE_TYPE_CHANNEL_RECEIVE:
                if (!(buffer->handles[handle_i].flags & ATTACHED_HANDLE_FLAG_MOVE)) {
                    err = ERR_KERNEL_UNCOPIEABLE_HANDLE_TYPE;
                    goto fail;
                }
                break;
            default:
                err = ERR_KERNEL_WRONG_HANDLE_TYPE;
                goto fail;
            }
            continue;
fail:
            if (message_allocated)
                free(message);
            free(handles);
            free(data);
            return err;
        }
    }
    // Copy the handles
    size_t handles_offset = 0;
    for (size_t buffer_i = 0; buffer_i < user_message->handles_buffers_num; buffer_i++) {
        const SendMessageHandles *buffer = &user_message->handles_buffers[buffer_i];
        for (size_t handle_i = 0; handle_i < buffer->length; handle_i++) {
            // Confirm flags are valid
            if (buffer->handles[handle_i].flags & ~ATTACHED_HANDLE_FLAG_MOVE) {
                err = ERR_KERNEL_INVALID_ARG;
                goto fail;
            }
            // Copy the handle data
            Handle handle;
            err = handle_get(&cpu_local->current_process->handles, buffer->handles[handle_i].handle_i, &handle);
            if (err)
                goto fail;
            switch (handle.type) {
            case HANDLE_TYPE_CHANNEL_SEND:
                if (!(buffer->handles[handle_i].flags & ATTACHED_HANDLE_FLAG_MOVE))
                    channel_add_ref(handle.channel);
                handles[handles_offset + handle_i] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, {.channel = handle.channel}};
                break;
            case HANDLE_TYPE_CHANNEL_RECEIVE:
                handles[handles_offset + handle_i] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = handle.channel}};
                break;
            default:
                break;
            }
            // Remove the handle if move flag is set
            if (buffer->handles[handle_i].flags & ATTACHED_HANDLE_FLAG_MOVE)
                handle_clear(&cpu_local->current_process->handles, buffer->handles[handle_i].handle_i, false);
        }
        handles_offset += buffer->length;
    }
    // Copy the data
    size_t data_offset = 0;
    for (size_t i = 0; i < user_message->data_buffers_num; i++) {
        memcpy(data + data_offset, user_message->data_buffers[i].data, user_message->data_buffers[i].length);
        data_offset += user_message->data_buffers[i].length;
    }
    *message_ptr = message;
    return 0;
}

// Read a message into user-provided buffers
err_t message_read_user(const Message *message, ReceiveMessage *user_message, const MessageLength *offset, bool check_types) {
    err_t err;
    if (message->data_size >= offset->data) {
        if (user_message->data_length > message->data_size - offset->data)
            user_message->data_length = message->data_size - offset->data;
        memcpy(user_message->data, message->data + offset->data, user_message->data_length);
    } else {
        user_message->data_length = 0;
    }
    if (message->handles_size >= offset->handles) {
        if (user_message->handles_length > message->handles_size - offset->handles)
            user_message->handles_length = message->handles_size - offset->handles;
        err = handles_reserve(&cpu_local->current_process->handles, user_message->handles_length);
        if (err)
            return err;
        // Check handle types if necessary
        if (check_types) {
            for (size_t i = 0; i < user_message->handles_length; i++) {
                if (user_message->handles[i].type != message->handles[offset->handles + i].type) {
                    return ERR_KERNEL_MESSAGE_WRONG_HANDLE_TYPE;
                }
            }
        }
        // Read the handles
        for (size_t i = 0; i < user_message->handles_length; i++) {
            switch (message->handles[offset->handles + i].type) {
            case ATTACHED_HANDLE_TYPE_CHANNEL_SEND: {
                channel_add_ref(message->handles[offset->handles + i].channel);
                handle_t handle_i;
                handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_SEND, {.channel = message->handles[offset->handles + i].channel}}, &handle_i);
                user_message->handles[i] = (ReceiveAttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, handle_i};
                break;
            }
            case ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE: {
                channel_add_ref(message->handles[offset->handles + i].channel);
                handle_t handle_i;
                handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = message->handles[offset->handles + i].channel}}, &handle_i);
                user_message->handles[i] = (ReceiveAttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE, handle_i};
                break;
            }
            }
        }
    } else {
        user_message->handles_length = 0;
    }
    return 0;
}

// Free a message along with its data buffer
void message_free(Message *message) {
    free(message->data);
    for (size_t i = 0; i < message->handles_size; i++)
        attached_handle_free(message->handles[i]);
    if (message->async_reply) {
        mqueue_del_ref(message->mqueue);
        free(message->reply_template);
    }
    free(message);
}

static err_t mqueue_send(MessageQueue *queue, Message *message, bool nonblock);

// Reply to a message
err_t message_reply(Message *message, Message *reply) {
    // Fail if message was already replied to
    if (message->replied_to)
        return ERR_KERNEL_MESSAGE_ALREADY_REPLIED_TO;
    // Mark message as replied to
    message->replied_to = true;
    // Branch depending on whether message is bound to a queue
    if (message->async_reply) {
        // Set reply fields
        reply->tag = message->reply_tag;
        reply->is_reply = true;
        // Send message to queue
        mqueue_send(message->mqueue, reply, false);
        // Remove queue reference
        message->async_reply = false;
        mqueue_del_ref(message->mqueue);
    } else {
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
    return 0;
}

// Reply to a message with an error code
err_t message_reply_error(Message *message, err_t error) {
    // Fail if message was already replied to
    if (message->replied_to)
        return ERR_KERNEL_MESSAGE_ALREADY_REPLIED_TO;
    // Mark message as replied to
    message->replied_to = true;
    if (message->async_reply) {
        // Create reply
        Message *reply = message->reply_template;
        memset(reply, 0, sizeof(Message));
        // Set tag and error code
        reply->tag = message->reply_tag;
        reply->error_code = error;
        reply->is_reply = true;
        // Send message to queue
        mqueue_send(message->mqueue, reply, false);
        // Remove queue reference
        message->async_reply = false;
        mqueue_del_ref(message->mqueue);
    } else {
        // Set the reply error code if one is wanted
        if (message->reply_error != NULL)
            *(message->reply_error) = error;
        // If there is a sender blocked waiting for a reply, unblock it
        if (message->blocked_sender != NULL)
            process_enqueue(message->blocked_sender);
        message->blocked_sender = NULL;
    }
    return 0;
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

// Close a message queue
void mqueue_close(MessageQueue *queue) {
    spinlock_acquire(&queue->lock);
    // Mark the queue as closed
    queue->closed = true;
    // Notify all pending messages that the queue has been closed
    for (Message *message = queue->start; message != NULL; message = message->next_message)
        message_reply_error(message, ERR_KERNEL_CHANNEL_CLOSED);
    spinlock_release(&queue->lock);
}

// Send a message to a message queue - assumes the queue lock is already held
static err_t mqueue_send_(MessageQueue *queue, Message *message, bool nonblock) {
    // Fail if queue is closed
    if (queue->closed) {
        message_free(message);
        return ERR_KERNEL_CHANNEL_CLOSED;
    }
    // If the queue is full, block until there is space
    while (!message->is_reply && queue->length >= MESSAGE_QUEUE_MAX_LENGTH) {
        if (nonblock) {
            message_free(message);
            return ERR_KERNEL_MQUEUE_FULL;
        }
        process_queue_add(&queue->blocked_senders, cpu_local->current_process);
        process_block(&queue->lock);
        spinlock_acquire(&queue->lock);
    }
    // Add the message to the queue
    message->next_message = NULL;
    if (queue->start == NULL) {
        queue->start = message;
        queue->end = message;
    } else {
        queue->end->next_message = message;
        queue->end = message;
    }
    if (!message->is_reply)
        queue->length += 1;
    // If there is a receiver blocked waiting for a message, unblock it
    // Don't unblock if the process has already been unblocked by a timeout
    if (queue->blocked_receiver != NULL) {
        bool unblock;
        if (queue->waiting_for_timeout) {
            spinlock_acquire(&wait_queue_lock);
            unblock = wait_queue_remove_process(queue->blocked_receiver);
            spinlock_release(&wait_queue_lock);
            queue->waiting_for_timeout = false;
        } else {
            unblock = true;
        }
        if (unblock) {
            queue->blocked_receiver->timed_out = false;
            process_enqueue(queue->blocked_receiver);
        }
    }
    queue->blocked_receiver = NULL;
    return 0;
}

// Send a message to a message queue
static err_t mqueue_send(MessageQueue *queue, Message *message, bool nonblock) {
    err_t err;
    spinlock_acquire(&queue->lock);
    err = mqueue_send_(queue, message, nonblock);
    if (err) {
        spinlock_release(&queue->lock);
        return err;
    }
    spinlock_release(&queue->lock);
    return 0;
}

// Send a message to a message queue and wait for a reply
static err_t mqueue_call(MessageQueue *queue, Message *message, Message **reply) {
    err_t err;
    // Set the reply information
    err_t reply_error;
    message->reply_error = &reply_error;
    message->reply = reply;
    message->blocked_sender = cpu_local->current_process;
    // Send the message
    spinlock_acquire(&queue->lock);
    err = mqueue_send_(queue, message, false);
    if (err) {
        spinlock_release(&queue->lock);
        return err;
    }
    // Block and wait for a reply
    process_block(&queue->lock);
    return reply_error;
}

// Receive a message from a queue
err_t mqueue_receive(MessageQueue *queue, Message **message_ptr, bool nonblock, bool prioritize_timeout, i64 timeout) {
    // If timeouts are prioritized, check for timeout first
    if (prioritize_timeout && timeout != TIMEOUT_NONE && time_get() >= timeout)
        return ERR_KERNEL_TIMEOUT;
    spinlock_acquire(&queue->lock);
    // If there are no messages in the queue, block until either a message arrives or timeout occurs
    while (queue->start == NULL) {
        if (nonblock) {
            // If nonblock flag is set, return early
            spinlock_release(&queue->lock);
            return ERR_KERNEL_MQUEUE_EMPTY;
        }
        if (timeout == TIMEOUT_NONE) {
            // If there is no timeout, block until message is received
            queue->blocked_receiver = cpu_local->current_process;
            queue->waiting_for_timeout = false;
            process_block(&queue->lock);
            spinlock_acquire(&queue->lock);
        } else {
            // If there is a timeout and we're past it, return a timeout error
            if (time_get() >= timeout) {
                spinlock_release(&queue->lock);
                return ERR_KERNEL_TIMEOUT;
            }
            // Add to timeout queue and wait for message at the same time
            queue->blocked_receiver = cpu_local->current_process;
            queue->waiting_for_timeout = true;
            spinlock_acquire(&wait_queue_lock);
            wait_queue_insert_current_process(timeout);
            spinlock_release(&queue->lock);
            process_block(&wait_queue_lock);
            // After unblocking, check if the cause was a timeout and return an error if it was
            spinlock_acquire(&queue->lock);
            queue->waiting_for_timeout = false;
            queue->blocked_receiver = NULL;
            if (cpu_local->current_process->timed_out || (prioritize_timeout && time_get() >= timeout)) {
                spinlock_release(&queue->lock);
                return ERR_KERNEL_TIMEOUT;
            }
        }
    }
    // Remove a message from the queue
    Message *message = queue->start;
    queue->start = queue->start->next_message;
    if (message->error_code)
        return message->error_code;
    *message_ptr = message;
    if (!message->is_reply)
        queue->length -= 1;
    // If there is a blocked sender, unblock it
    Process *blocked_sender = process_queue_remove(&queue->blocked_senders);
    if (blocked_sender != NULL)
        process_enqueue(blocked_sender);
    spinlock_release(&queue->lock);
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
        if (channel->queue != NULL)
            mqueue_del_ref(channel->queue);
        free(channel);
    } else {
        spinlock_release(&channel->lock);
    }
}

// Close a channel
void channel_close(Channel *channel) {
    spinlock_acquire(&channel->lock);
    channel->closed = true;
    spinlock_release(&channel->lock);
}

// Set the channel's message queue and tag
err_t channel_set_mqueue(Channel *channel, MessageQueue *mqueue, MessageTag tag) {
    spinlock_acquire(&channel->lock);
    if (channel->queue != NULL) {
        spinlock_release(&channel->lock);
        return ERR_KERNEL_MQUEUE_ALREADY_SET;
    }
    mqueue_add_ref(mqueue);
    channel->queue = mqueue;
    channel->tag = tag;
    for (Process *process = process_queue_remove(&channel->blocked_senders); process != NULL; process = process_queue_remove(&channel->blocked_senders))
        process_enqueue(process);
    spinlock_release(&channel->lock);
    return 0;
}

// Prepare for sending a message on a channel
// This is the common part of channel_send() and channel_call().
static err_t channel_prepare_for_send(Channel *channel, Message *message, MessageQueue **queue, bool nonblock) {
    spinlock_acquire(&channel->lock);
    // Fail if the channel has been closed
    if (channel->closed) {
        spinlock_release(&channel->lock);
        return ERR_KERNEL_CHANNEL_CLOSED;
    }
    // Block if the channel has no assigned queue
    if (channel->queue == NULL) {
        if (nonblock) {
            spinlock_release(&channel->lock);
            return ERR_KERNEL_CHANNEL_NOT_BOUND;
        }
        process_queue_add(&channel->blocked_senders, cpu_local->current_process);
        process_block(&channel->lock);
        spinlock_acquire(&channel->lock);
    }
    // Set message tag
    message->tag = channel->tag;
    // Get channel queue
    *queue = channel->queue;
    spinlock_release(&channel->lock);
    return 0;
}

// Send a message on a channel
err_t channel_send(Channel *channel, Message *message, bool nonblock) {
    err_t err;
    MessageQueue *queue;
    err = channel_prepare_for_send(channel, message, &queue, nonblock);
    if (err) {
        message_free(message);
        return err;
    }
    return mqueue_send(queue, message, nonblock);
}

// Send a message on a channel and wait for a reply
err_t channel_call(Channel *channel, Message *message, Message **reply) {
    err_t err;
    MessageQueue *queue;
    err = channel_prepare_for_send(channel, message, &queue, false);
    if (err) {
        message_free(message);
        return err;
    }
    return mqueue_call(queue, message, reply);
}

// Set message to expect an async reply and send it on a channel
err_t channel_call_async(Channel *channel, Message *message, MessageQueue *mqueue, MessageTag tag) {
    err_t err;
    MessageQueue *queue;
    err = channel_prepare_for_send(channel, message, &queue, false);
    if (err) {
        message_free(message);
        return err;
    }
    message->reply_template = malloc(sizeof(Message));
    if (message->reply_template == NULL) {
        message_free(message);
        return ERR_KERNEL_NO_MEMORY;
    }
    message->async_reply = true;
    message->reply_tag = tag;
    mqueue_add_ref(mqueue);
    message->mqueue = mqueue;
    return mqueue_send(queue, message, false);
}

// Returns the length of the message
err_t syscall_message_get_length(handle_t i, MessageLength *length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(length, sizeof(MessageLength), true);
    if (err)
        return err;
    // Copy the length
    length->data = handle.message->data_size;
    length->handles = handle.message->handles_size;
    return 0;
}

// Read the contents of a message into a buffer starting at a given offset
// If the message size is outside of the given bounds, it instead replies with a given error code
// and returns either ERR_KERNEL_MESSAGE_TOO_SHORT or ERR_KERNEL_MESSAGE_TOO_LONG.
// If the error code is 0, does not reply.
// The flags FLAG_ALLOW_PARTIAL_DATA_READ and FLAG_ALLOW_PARTIAL_HANDLES_READ disable this behavior
// when the data or handles upper bounds are exceeded.
err_t syscall_message_read(handle_t i, ReceiveMessage *user_message, const MessageLength *offset, const MessageLength *min_length, err_t reply_error, u64 flags) {
    err_t err;
    Handle handle;
    // Verify flags are valid
    if (flags & ~(FLAG_ALLOW_PARTIAL_DATA_READ | FLAG_ALLOW_PARTIAL_HANDLES_READ | FLAG_FREE_MESSAGE))
        return ERR_KERNEL_INVALID_ARG;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Check provided error code is not reserved
    if (reply_error >= ERR_KERNEL_MIN)
        return ERR_KERNEL_INVALID_ARG;
    // Verify buffers are valid
    err = verify_user_receive_message(user_message);
    if (err)
        return err;
    if (offset != NULL) {
        err = verify_user_buffer(offset, sizeof(MessageLength), false);
        if (err)
            return err;
    }
    if (min_length != NULL) {
        err = verify_user_buffer(min_length, sizeof(MessageLength), false);
        if (err)
            return err;
    }
    // Perform bounds check
    size_t data_length = handle.message != NULL ? handle.message->data_size : 0;
    size_t handles_length = handle.message != NULL ? handle.message->handles_size : 0;
    MessageLength zero_offset = {0, 0};
    if (offset == NULL)
        offset = &zero_offset;
    err_t range_err = 0;
    if (data_length < (min_length ? min_length->data : user_message->data_length) + offset->data)
        range_err = ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    else if (data_length > user_message->data_length + offset->data && !(flags & FLAG_ALLOW_PARTIAL_DATA_READ))
        range_err = ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    else if (handles_length < (min_length ? min_length->handles : user_message->handles_length) + offset->handles)
        range_err = ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT;
    else if (handles_length > user_message->handles_length + offset->handles && !(flags & FLAG_ALLOW_PARTIAL_HANDLES_READ))
        range_err = ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG;
    if (range_err != 0) {
        if (reply_error != 0)
            message_reply_error(handle.message, reply_error);
        handle_clear(&cpu_local->current_process->handles, i, true);
        err = range_err;
    } else {
        // Copy the message data if bounds check passed
        err = message_read_user(handle.message, user_message, offset, true);
    }
    // Free message and handle if requested
    if (flags & FLAG_FREE_MESSAGE)
        handle_clear(&cpu_local->current_process->handles, i, true);
    return err;
}

// Send a message on a channel
err_t syscall_channel_send(handle_t channel_i, const SendMessage *user_message, u64 flags) {
    err_t err;
    Handle channel_handle;
    // Verify flags are valid
    if (flags & ~FLAG_NONBLOCK)
        return ERR_KERNEL_INVALID_ARG;
    // Verify buffers are valid
    err = verify_user_send_message(user_message);
    if (err)
        return err;
    // Get the channel from handle
    err = handle_get(&cpu_local->current_process->handles, channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_SEND)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message;
    err = message_alloc_user(user_message, &message, NULL);
    if (err)
        return err;
    // Send the message
    err = channel_send(channel_handle.channel, message, (bool)(flags & FLAG_NONBLOCK));
    if (err)
        return err;
    return 0;
}

// Send a message on a channel and wait for a reply
err_t syscall_channel_call(handle_t channel_i, const SendMessage *user_message, handle_t *reply_i_ptr) {
    err_t err;
    Handle channel_handle;
    // Verify buffers are valid
    err = verify_user_send_message(user_message);
    if (err)
        return err;
    if (reply_i_ptr != NULL) {
        err = verify_user_buffer(reply_i_ptr, sizeof(handle_t), true);
        if (err)
            return err;
    }
    // Get the channel from handle
    err = handle_get(&cpu_local->current_process->handles, channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_SEND)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message;
    err = message_alloc_user(user_message, &message, NULL);
    if (err)
        return err;
    // Send the message
    Message *reply;
    err = channel_call(channel_handle.channel, message, &reply);
    if (err)
        return err;
    // Add the reply handle
    if (reply_i_ptr != NULL) {
        err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_MESSAGE, {.message = reply}}, reply_i_ptr);
        if (err)
            return err;
    }
    return 0;
}

// Get a message from a channel
err_t syscall_mqueue_receive(handle_t mqueue_i, MessageTag *tag_ptr, handle_t *message_i_ptr, i64 timeout, u64 flags) {
    err_t err;
    Handle mqueue_handle;
    // Verify flags are valid
    if (flags & ~(FLAG_NONBLOCK | FLAG_PRIORITIZE_TIMEOUT))
        return ERR_KERNEL_INVALID_ARG;
    // Verify buffer is valid
    if (tag_ptr != NULL) {
        err = verify_user_buffer(tag_ptr, sizeof(MessageTag), true);
        if (err)
            return err;
    }
    err = verify_user_buffer(message_i_ptr, sizeof(handle_t), true);
    if (err)
        return err;
    // Get the channel from handle
    err = handle_get(&cpu_local->current_process->handles, mqueue_i, &mqueue_handle);
    if (err)
        return err;
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Receive a message
    Message *message;
    err = mqueue_receive(mqueue_handle.mqueue, &message, (bool)(flags & FLAG_NONBLOCK), (bool)(flags & FLAG_PRIORITIZE_TIMEOUT), timeout);
    if (err)
        return err;
    // Return the tag
    if (tag_ptr != NULL)
        *tag_ptr = message->tag;
    // Add the handle
    err = handles_reserve(&cpu_local->current_process->handles, 1);
    if (err)
        return err;
    handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_MESSAGE, {.message = message}}, message_i_ptr);
    return 0;
}

err_t syscall_message_reply(handle_t message_i, const SendMessage *user_reply, u64 flags) {
    err_t err;
    Handle message_handle;
    // Verify flags are valid
    if (flags & ~FLAG_FREE_MESSAGE)
        return ERR_KERNEL_INVALID_ARG;
    // Verify buffer is valid
    err = verify_user_send_message(user_reply);
    if (err)
        return err;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a reply
    Message *reply;
    if (message_handle.message->async_reply)
        err = message_alloc_user(user_reply, &reply, message_handle.message->reply_template);
    else
        err = message_alloc_user(user_reply, &reply, NULL);
    if (!err)
        // Send the reply
        err = message_reply(message_handle.message, reply);
    // Free message and handle if requested
    if (flags & FLAG_FREE_MESSAGE)
        handle_clear(&cpu_local->current_process->handles, message_i, true);
    return err;
}

err_t syscall_message_reply_error(handle_t message_i, err_t error, u64 flags) {
    err_t err;
    Handle message_handle;
    // Verify flags are valid
    if (flags & ~FLAG_FREE_MESSAGE)
        return ERR_KERNEL_INVALID_ARG;
    // Check error code is not reserved by the kernel or zero
    if (error >= ERR_KERNEL_MIN || error == 0)
        return ERR_KERNEL_INVALID_ARG;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, message_i, &message_handle);
    if (err)
        return err;
    if (message_handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Send the error
    err = message_reply_error(message_handle.message, error);
    // Free message and handle if requested
    if (flags & FLAG_FREE_MESSAGE)
        handle_clear(&cpu_local->current_process->handles, message_i, true);
    return err;
}

// Send a message on a channel, wait for a reply and check its size against the given bounds
// Functions similar to channel_call() followed by reply_read() and handle_free()
err_t syscall_channel_call_read(handle_t channel_i, const SendMessage *user_message, ReceiveMessage *user_reply, const MessageLength *min_length) {
    err_t err;
    Handle channel_handle;
    // Verify buffers are valid
    err = verify_user_send_message(user_message);
    if (err)
        return err;
    err = verify_user_receive_message(user_reply);
    if (err)
        return err;
    if (min_length != NULL) {
        err = verify_user_buffer(min_length, sizeof(MessageLength), false);
        if (err)
            return err;
    }
    // Get the channel from handle
    err = handle_get(&cpu_local->current_process->handles, channel_i, &channel_handle);
    if (err)
        return err;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_SEND)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message;
    err = message_alloc_user(user_message, &message, NULL);
    if (err)
        return err;
    // Send the message
    Message *reply;
    err = channel_call(channel_handle.channel, message, &reply);
    if (err)
        return err;
    // Perform bounds check on the reply
    size_t data_length = reply != NULL ? reply->data_size : 0;
    size_t handles_length = reply != NULL ? reply->handles_size : 0;
    if (data_length < (min_length ? min_length->data : user_reply->data_length)) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    }
    if (data_length > user_reply->data_length) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    }
    if (handles_length < (min_length ? min_length->handles : user_reply->handles_length)) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT;
    }
    if (handles_length > user_reply->handles_length) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG;
    }
    // Copy the message data
    err = message_read_user(reply, user_reply, &(MessageLength){0, 0}, true);
    message_free(reply);
    if (err)
        return err;
    return 0;
}

// Create a new message queue
err_t syscall_mqueue_create(handle_t *handle_i_ptr) {
    err_t err;
    // Verify buffer is valid
    err = verify_user_buffer(handle_i_ptr, sizeof(handle_t), true);
    if (err)
        return err;
    // Allocate the message queue
    MessageQueue *mqueue = mqueue_alloc();
    if (mqueue == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Add the handle
    err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_MESSAGE_QUEUE, {.mqueue = mqueue}}, handle_i_ptr);
    if (err) {
        mqueue_del_ref(mqueue);
        return err;
    }
    return 0;
}

err_t syscall_mqueue_add_channel(handle_t mqueue_i, handle_t channel_i, MessageTag tag) {
    err_t err;
    // Get the handles
    Handle mqueue_handle;
    Handle channel_handle;
    err = handle_get(&cpu_local->current_process->handles, mqueue_i, &mqueue_handle);
    if (err)
        return err;
    err = handle_get(&cpu_local->current_process->handles, channel_i, &channel_handle);
    if (err)
        return err;
    // Check handle types
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_RECEIVE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Add the channel to the message queue
    err = channel_set_mqueue(channel_handle.channel, mqueue_handle.mqueue, tag);
    if (err)
        return err;
    // Remove the channel handle
    channel_del_ref(channel_handle.channel);
    handle_clear(&cpu_local->current_process->handles, channel_i, false);
    return 0;
}

err_t syscall_channel_create(handle_t *channel_send_i_ptr, handle_t *channel_receive_i_ptr) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_buffer(channel_send_i_ptr, sizeof(handle_t), true);
    if (err)
        return err;
    err = verify_user_buffer(channel_receive_i_ptr, sizeof(handle_t), true);
    if (err)
        return err;
    // Allocate the channel
    Channel *channel = channel_alloc();
    if (channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    // Increment refcount since two references are created
    channel_add_ref(channel);
    // Add the handles
    err = handles_reserve(&cpu_local->current_process->handles, 2);
    if (err) {
        channel_del_ref(channel);
        return err;
    }
    handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_SEND, {.channel = channel}}, channel_send_i_ptr);
    handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = channel}}, channel_receive_i_ptr);
    return 0;
}

err_t syscall_channel_call_async(handle_t channel_i, const SendMessage *user_message, handle_t mqueue_i, MessageTag tag) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_send_message(user_message);
    if (err)
        return err;
    // Get the handles
    Handle channel_handle;
    Handle mqueue_handle;
    err = handle_get(&cpu_local->current_process->handles, channel_i, &channel_handle);
    if (err)
        return err;
    err = handle_get(&cpu_local->current_process->handles, mqueue_i, &mqueue_handle);
    if (err)
        return err;
    // Check handle types
    if (channel_handle.type != HANDLE_TYPE_CHANNEL_SEND)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Create a message
    Message *message;
    err = message_alloc_user(user_message, &message, NULL);
    if (err)
        return err;
    // Send the message
    err = channel_call_async(channel_handle.channel, message, mqueue_handle.mqueue, tag);
    if (err)
        return err;
    return 0;
}
