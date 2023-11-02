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
static err_t resource_list_get(ResourceList *list, ResourceName *name, size_t *i_ptr) {
    for (size_t i = 0; i < list->length; i++) {
        if (memcmp(list->entries[i].name.bytes, name->bytes, RESOURCE_NAME_MAX) == 0) {
            *i_ptr = i;
            return 0;
        }
    }
    return ERR_KERNEL_INVALID_RESOURCE;
}

// Get a sending channel resource and bind it to a handle
err_t syscall_resource_get(ResourceName *name, ResourceType type, handle_t *handle_i_ptr) {
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
err_t syscall_mqueue_add_channel_resource(handle_t mqueue_i, ResourceName *channel_name, MessageTag tag) {
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
