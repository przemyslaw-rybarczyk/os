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

#define MESSAGE_QUEUE_MAX_LENGTH 16

typedef struct MessageQueue {
    spinlock_t lock;
    size_t refcount;
    bool closed;
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
    if (user_message != NULL) {
        err = verify_user_buffer(user_message, sizeof(SendMessage), false);
        if (err)
            return err;
        err = verify_user_buffer(user_message->data, user_message->length.data, false);
        if (err)
            return err;
        err = verify_user_buffer(user_message->handles, user_message->length.handles * sizeof(SendAttachedHandle), false);
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
    err = verify_user_buffer(user_message->data, user_message->length.data, true);
    if (err)
        return err;
    err = verify_user_buffer(user_message->handles, user_message->length.handles * sizeof(ReceiveAttachedHandle), true);
    if (err)
        return err;
    return 0;
}

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
    message->handles_size = 0;
    message->handles = NULL;
    return message;
}

// Create a message from a user-provided message specification
static err_t message_alloc_user(const SendMessage *user_message, Message **message_ptr) {
    err_t err;
    // If the user message is NULL, allocate an empty message
    if (user_message == NULL) {
        Message *message = message_alloc(0, NULL);
        if (message == NULL)
            return ERR_KERNEL_NO_MEMORY;
        *message_ptr = message;
        return 0;
    }
    // Allocate handle list
    AttachedHandle *handles = malloc(user_message->length.handles * sizeof(AttachedHandle));
    if (user_message->length.handles != 0 && handles == NULL) {
        return ERR_KERNEL_NO_MEMORY;
    }
    // Copy the handles
    for (size_t i = 0; i < user_message->length.handles; i++) {
        // Confirm flags are valid
        if (user_message->handles[i].flags & ~ATTACHED_HANDLE_FLAG_MOVE) {
            err = ERR_KERNEL_INVALID_ARG;
            goto fail;
        }
        // Copy the handle data
        Handle handle;
        err = handle_get(&cpu_local->current_process->handles, user_message->handles[i].handle_i, &handle);
        if (err)
            goto fail;
        switch (handle.type) {
        case HANDLE_TYPE_CHANNEL_SEND:
            channel_add_ref(handle.channel);
            handles[i] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, {.channel = handle.channel}};
            break;
        case HANDLE_TYPE_CHANNEL_RECEIVE:
            if (!(user_message->handles[i].flags & ATTACHED_HANDLE_FLAG_MOVE)) {
                err = ERR_KERNEL_UNCOPIEABLE_HANDLE_TYPE;
                goto fail;
            }
            channel_add_ref(handle.channel);
            handles[i] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = handle.channel}};
            break;
        default:
            err = ERR_KERNEL_WRONG_HANDLE_TYPE;
            goto fail;
        }
        // Remove the original handle if move flag is set
        if (user_message->handles[i].flags & ATTACHED_HANDLE_FLAG_MOVE)
            handle_clear(&cpu_local->current_process->handles, user_message->handles[i].handle_i);
        continue;
fail:
        for (size_t j = 0; j < i; j++)
            attached_handle_free(handles[j]);
        free(handles);
        return err;
    }
    // Allocate the message
    Message *message = message_alloc(user_message->length.data, user_message->data);
    if (message == NULL)
        return ERR_KERNEL_NO_MEMORY;
    message->handles_size = user_message->length.handles;
    message->handles = handles;
    *message_ptr = message;
    return 0;
}

// Read a message into user-provided buffers
static err_t message_read_user(const Message *message, ReceiveMessage *user_message, bool check_types) {
    err_t err;
    // If the message is NULL treat it as empty
    if (message == NULL) {
        user_message->length.data = 0;
        user_message->length.handles = 0;
        return 0;
    }
    user_message->length.data = message->data_size;
    user_message->length.handles = message->handles_size;
    memcpy(user_message->data, message->data, message->data_size);
    err = handles_reserve(&cpu_local->current_process->handles, message->handles_size);
    if (err)
        return err;
    // Check handle types if necessary
    if (check_types) {
        for (size_t i = 0; i < message->handles_size; i++) {
            if (user_message->handles[i].type != message->handles[i].type) {
                return ERR_KERNEL_MESSAGE_WRONG_HANDLE_TYPE;
            }
        }
    }
    // Read the handles
    for (size_t i = 0; i < message->handles_size; i++) {
        switch (message->handles[i].type) {
        case ATTACHED_HANDLE_TYPE_CHANNEL_SEND: {
            channel_add_ref(message->handles[i].channel);
            handle_t handle_i;
            handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_SEND, {.channel = message->handles[i].channel}}, &handle_i);
            user_message->handles[i] = (ReceiveAttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, handle_i};
            break;
        }
        case ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE: {
            channel_add_ref(message->handles[i].channel);
            handle_t handle_i;
            handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = message->handles[i].channel}}, &handle_i);
            user_message->handles[i] = (ReceiveAttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE, handle_i};
            break;
        }
        }
    }
    return 0;
}

// Free a message along with its data buffer
void message_free(Message *message) {
    if (message == NULL)
        return;
    free(message->data);
    for (size_t i = 0; i < message->handles_size; i++)
        attached_handle_free(message->handles[i]);
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

// Send a message on a channel and wait for a reply
err_t channel_call(Channel *channel, Message *message, Message **reply) {
    spinlock_acquire(&channel->lock);
    message->tag = channel->tag;
    if (channel->queue == NULL) {
        process_queue_add(&channel->blocked_senders, cpu_local->current_process);
        process_block(&channel->lock);
        spinlock_acquire(&channel->lock);
    }
    if (channel->queue->closed) {
        spinlock_release(&channel->lock);
        return ERR_KERNEL_CHANNEL_CLOSED;
    }
    MessageQueue *queue = channel->queue;
    spinlock_release(&channel->lock);
    return mqueue_call(queue, message, reply);
}

// Returns the length of the message
err_t syscall_message_get_length(handle_t i, MessageLength *length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE && handle.type != HANDLE_TYPE_REPLY)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(length, sizeof(MessageLength), true);
    if (err)
        return err;
    // Copy the length
    length->data = handle.message != NULL ? handle.message->data_size : 0;
    length->handles = handle.message != NULL ? handle.message->handles_size : 0;
    return 0;
}

// Reads the message data into the provided userspace buffer
// The buffer must be large enough to fit the entire message.
err_t syscall_message_read(handle_t i, void *data, ReceiveAttachedHandle *handles) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE && handle.type != HANDLE_TYPE_REPLY)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffer is valid
    err = verify_user_buffer(data, handle.message->data_size, true);
    if (err)
        return err;
    err = verify_user_buffer(handles, handle.message->handles_size * sizeof(ReceiveAttachedHandle), true);
    if (err)
        return err;
    // Copy the data
    ReceiveMessage user_message = (ReceiveMessage){{}, data, handles};
    return message_read_user(handle.message, &user_message, false);
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
    err = message_alloc_user(user_message, &message);
    if (err)
        return err;
    // Send the message
    Message *reply;
    err = channel_call(channel_handle.channel, message, &reply);
    if (err)
        return err;
    // Add the reply handle
    if (reply_i_ptr != NULL) {
        err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_REPLY, {.message = reply}}, reply_i_ptr);
        if (err)
            return err;
    }
    return 0;
}

// Get a message from a channel
err_t syscall_mqueue_receive(handle_t mqueue_i, MessageTag *tag_ptr, handle_t *message_i_ptr) {
    err_t err;
    Handle mqueue_handle;
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
    mqueue_receive(mqueue_handle.mqueue, &message);
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

err_t syscall_message_reply(handle_t message_i, const SendMessage *user_reply) {
    err_t err;
    Handle message_handle;
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
    // If the reply size is zero, the reply is set to NULL and no allocation occurs.
    Message *reply;
    if (user_reply != NULL && (user_reply->length.data != 0 || user_reply->length.handles != 0)) {
        // Allocate the reply
        err = message_alloc_user(user_reply, &reply);
        if (err)
            return err;
    } else {
        reply = NULL;
    }
    // Send the reply
    message_reply(message_handle.message, reply);
    // Free message and handle
    handle_clear(&cpu_local->current_process->handles, message_i);
    return 0;
}

err_t syscall_message_reply_error(handle_t message_i, err_t error) {
    err_t err;
    Handle message_handle;
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
    message_reply_error(message_handle.message, error);
    // Free message and handle
    handle_clear(&cpu_local->current_process->handles, message_i);
    return 0;
}

// Read the contents of a message with bounds checking
// Functions like message_read(), but if the message size is outside of the given bounds it instead replies with a given error code
// and returns either ERR_KERNEL_MESSAGE_TOO_SHORT or ERR_KERNEL_MESSAGE_TOO_LONG.
err_t syscall_message_read_bounded(handle_t i, ReceiveMessage *user_message, const MessageLength *min_length, const ErrorReplies *errors) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffers are valid
    err = verify_user_receive_message(user_message);
    if (err)
        return err;
    if (min_length != NULL) {
        err = verify_user_buffer(min_length, sizeof(MessageLength), false);
        if (err)
            return err;
    }
    err = verify_user_buffer(errors, sizeof(ErrorReplies), false);
    if (err)
        return err;
    // Check provided error codes are not reserved or zero
    if (
        errors->data_low >= ERR_KERNEL_MIN || errors->data_low == 0 ||
        errors->data_high >= ERR_KERNEL_MIN || errors->data_high == 0 ||
        errors->handles_low >= ERR_KERNEL_MIN || errors->handles_low == 0 ||
        errors->handles_high >= ERR_KERNEL_MIN || errors->handles_high == 0
    )
        return ERR_KERNEL_INVALID_ARG;
    // Perform bounds check
    size_t data_length = handle.message != NULL ? handle.message->data_size : 0;
    size_t handles_length = handle.message != NULL ? handle.message->handles_size : 0;
    if (data_length < (min_length ? min_length->data : user_message->length.data)) {
        message_reply_error(handle.message, errors->data_low);
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    }
    if (data_length > user_message->length.data) {
        message_reply_error(handle.message, errors->data_high);
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    }
    if (handles_length < (min_length ? min_length->handles : user_message->length.handles)) {
        message_reply_error(handle.message, errors->handles_low);
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT;
    }
    if (handles_length > user_message->length.handles) {
        message_reply_error(handle.message, errors->handles_high);
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG;
    }
    // Copy the message data
    return message_read_user(handle.message, user_message, true);
}

// Read the contents of a reply with bounds checking
// Functions like message_read_bounded(), but frees the reply instead of replying to it.
err_t syscall_reply_read_bounded(handle_t i, ReceiveMessage *user_message, const MessageLength *min_length) {
    err_t err;
    Handle handle;
    // Get the message from handle
    err = handle_get(&cpu_local->current_process->handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_REPLY)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Verify buffers are valid
    err = verify_user_receive_message(user_message);
    if (err)
        return err;
    if (min_length != NULL) {
        err = verify_user_buffer(min_length, sizeof(MessageLength), false);
        if (err)
            return err;
    }
    // Perform bounds check
    size_t data_length = handle.message != NULL ? handle.message->data_size : 0;
    size_t handles_length = handle.message != NULL ? handle.message->handles_size : 0;
    if (data_length < (min_length ? min_length->data : user_message->length.data)) {
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    }
    if (data_length > user_message->length.data) {
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    }
    if (handles_length < (min_length ? min_length->handles : user_message->length.handles)) {
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT;
    }
    if (handles_length > user_message->length.handles) {
        handle_clear(&cpu_local->current_process->handles, i);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG;
    }
    // Copy the message data
    return message_read_user(handle.message, user_message, true);
}

// Send a message on a channel, wait for a reply and check its size against the given bounds
// Functions similar to channel_call() followed by reply_read_bounded() and handle_free()
err_t syscall_channel_call_bounded(handle_t channel_i, const SendMessage *user_message, ReceiveMessage *user_reply, const MessageLength *min_length) {
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
    err = message_alloc_user(user_message, &message);
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
    if (data_length < (min_length ? min_length->data : user_reply->length.data)) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    }
    if (data_length > user_reply->length.data) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    }
    if (handles_length < (min_length ? min_length->handles : user_reply->length.handles)) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT;
    }
    if (handles_length > user_reply->length.handles) {
        message_free(reply);
        return ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG;
    }
    // Copy the message data
    err = message_read_user(reply, user_reply, true);
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
    handle_clear(&cpu_local->current_process->handles, channel_i);
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
