#include "types.h"
#include "resource.h"

#include "alloc.h"
#include "channel.h"
#include "handle.h"
#include "string.h"
#include "page.h"
#include "percpu.h"
#include "process.h"

static void resource_free(Resource resource) {
    switch (resource.type) {
    case RESOURCE_TYPE_EMPTY:
        break;
    case RESOURCE_TYPE_CHANNEL_SEND:
        channel_del_ref(resource.channel);
        break;
    case RESOURCE_TYPE_CHANNEL_RECEIVE:
        channel_close(resource.channel);
        channel_del_ref(resource.channel);
        break;
    case RESOURCE_TYPE_MESSAGE:
        message_free(resource.message);
        break;
    }
}

void resource_list_free(ResourceList *list) {
    for (size_t i = 0; i < list->length; i++)
        resource_free(list->entries[i].resource);
    free(list->entries);
}

// Get an element of a resource list by its name
static err_t resource_list_get(ResourceList *list, const ResourceName *name, size_t *i_ptr) {
    for (size_t i = 0; i < list->length; i++) {
        if (memcmp(list->entries[i].name.bytes, name->bytes, RESOURCE_NAME_MAX) == 0) {
            *i_ptr = i;
            return 0;
        }
    }
    return ERR_KERNEL_INVALID_RESOURCE;
}

// Get a sending channel resource and bind it to a handle
err_t syscall_resource_get(const ResourceName *name, ResourceType type, handle_t *handle_i_ptr) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_buffer(handle_i_ptr, sizeof(handle_t), true);
    if (err)
        return err;
    err = verify_user_buffer(name, sizeof(ResourceName), false);
    if (err)
        return err;
    // Get the resource
    ResourceList *resources = &cpu_local->current_process->resources;
    size_t channel_i;
    err = resource_list_get(resources, name, &channel_i);
    if (err)
        return err;
    Resource *channel_resource = &resources->entries[channel_i].resource;
    if (channel_resource->type != type)
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    // Add the handle
    switch (type) {
    case RESOURCE_TYPE_EMPTY:
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    case RESOURCE_TYPE_CHANNEL_SEND:
        err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_SEND, {.channel = channel_resource->channel}}, handle_i_ptr);
        if (err)
            return err;
        break;
    case RESOURCE_TYPE_CHANNEL_RECEIVE:
        err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_CHANNEL_RECEIVE, {.channel = channel_resource->channel}}, handle_i_ptr);
        if (err)
            return err;
        break;
    case RESOURCE_TYPE_MESSAGE:
        err = handle_add(&cpu_local->current_process->handles, (Handle){HANDLE_TYPE_MESSAGE, {.message = channel_resource->message}}, handle_i_ptr);
        if (err)
            return err;
        break;
    }
    // Remove the resource
    channel_resource->type = RESOURCE_TYPE_EMPTY;
    return 0;
}

// Get a receiving channel resource and add it to a message queue
err_t syscall_mqueue_add_channel_resource(handle_t mqueue_i, const ResourceName *channel_name, MessageTag tag) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_buffer(channel_name, sizeof(ResourceName), false);
    if (err)
        return err;
    // Get the message queue handle
    Handle mqueue_handle;
    err = handle_get(&cpu_local->current_process->handles, mqueue_i, &mqueue_handle);
    if (err)
        return err;
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Get the channel resource
    ResourceList *resources = &cpu_local->current_process->resources;
    size_t channel_i;
    err = resource_list_get(resources, channel_name, &channel_i);
    if (err)
        return err;
    Resource *channel_resource = &resources->entries[channel_i].resource;
    if (channel_resource->type != RESOURCE_TYPE_CHANNEL_RECEIVE)
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    // Add the channel to the message queue
    err = channel_set_mqueue(channel_resource->channel, mqueue_handle.mqueue, tag);
    if (err)
        return err;
    // Remove the resource
    channel_resource->type = RESOURCE_TYPE_EMPTY;
    return 0;
}

// Read the contents of a message resource
err_t syscall_message_resource_read(const ResourceName *message_name, size_t data_length, void *data, size_t min_data_length, u64 flags) {
    err_t err;
    // Verify flags are valid
    if (flags & ~(FLAG_ALLOW_PARTIAL_DATA_READ | FLAG_ALLOW_PARTIAL_HANDLES_READ | FLAG_FREE_MESSAGE))
        return ERR_KERNEL_INVALID_ARG;
    // Verify buffers are valid
    err = verify_user_buffer(message_name, sizeof(ResourceName), false);
    if (err)
        return err;
    err = verify_user_buffer(data, data_length, true);
    if (err)
        return err;
    // Get message from the resource
    ResourceList *resources = &cpu_local->current_process->resources;
    size_t message_i;
    err = resource_list_get(resources, message_name, &message_i);
    if (err)
        return err;
    Resource *message_resource = &resources->entries[message_i].resource;
    if (message_resource->type != RESOURCE_TYPE_MESSAGE)
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    Message *message = message_resource->message;
    // Perform bounds check
    if (min_data_length == SIZE_MAX)
        min_data_length = data_length;
    if (message->data_size < min_data_length)
        return ERR_KERNEL_MESSAGE_DATA_TOO_SHORT;
    if (message->data_size > data_length && !(flags & FLAG_ALLOW_PARTIAL_DATA_READ))
        return ERR_KERNEL_MESSAGE_DATA_TOO_LONG;
    // Copy the message data
    err = message_read_user(message, &(ReceiveMessage){data_length, data, 0, NULL}, &(MessageLength){0, 0}, true);
    // Remove the resource if requested
    if (flags & FLAG_FREE_MESSAGE) {
        message_free(message);
        message_resource->type = RESOURCE_TYPE_EMPTY;
    }
    return err;
}
