#include "types.h"
#include "resource.h"

#include "handle.h"
#include "string.h"
#include "page.h"
#include "process.h"

// Convert a string to a resource name
// The string is truncated if longer than RESOURCE_NAME_MAX bytes.
ResourceName resource_name(const char *str) {
    ResourceName name;
    size_t i = 0;
    for (; i < RESOURCE_NAME_MAX && str[i] != 0; i++)
        name.bytes[i] = str[i];
    for (; i < RESOURCE_NAME_MAX; i++)
        name.bytes[i] = 0;
    return name;
}

// Get an element of a resource list by its name
err_t resource_list_get(ResourceList *list, ResourceName name, Resource *resource) {
    for (size_t i = 0; i < list->length; i++) {
        if (memcmp(list->entries[i].name.bytes, name.bytes, RESOURCE_NAME_MAX) == 0) {
            *resource = list->entries[i].resource;
            return 0;
        }
    }
    return ERR_KERNEL_INVALID_RESOURCE;
}

// Get a sending channel resource and bind it to a handle
err_t syscall_channel_get(const char *name_str, handle_t *handle_i_ptr) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_buffer(handle_i_ptr, sizeof(handle_t));
    if (err)
        return err;
    err = verify_user_buffer(name_str, RESOURCE_NAME_MAX);
    if (err)
        return err;
    // Get the resource
    Resource resource;
    err = process_resource_list_get(resource_name(name_str), &resource);
    if (err)
        return err;
    if (resource.type != RESOURCE_TYPE_CHANNEL_SEND)
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    // Add the handle
    err = process_add_handle((Handle){HANDLE_TYPE_CHANNEL, {.channel = resource.channel}}, handle_i_ptr);
    if (err)
        return err;
    return 0;
}

// Get a receiving channel resource and add it to a message queue
err_t syscall_mqueue_add_channel(handle_t mqueue_i, const char *channel_name_str, uintptr_t tag[2]) {
    err_t err;
    // Verify buffers are valid
    err = verify_user_buffer(channel_name_str, RESOURCE_NAME_MAX);
    if (err)
        return err;
    err = verify_user_buffer(tag, 2 * sizeof(uintptr_t));
    if (err)
        return err;
    // Get the message queue handle
    Handle mqueue_handle;
    err = process_get_handle(mqueue_i, &mqueue_handle);
    if (err)
        return err;
    if (mqueue_handle.type != HANDLE_TYPE_MESSAGE_QUEUE)
        return ERR_KERNEL_WRONG_HANDLE_TYPE;
    // Get the channel resource
    Resource channel_resource;
    err = process_resource_list_get(resource_name(channel_name_str), &channel_resource);
    if (err)
        return err;
    if (channel_resource.type != RESOURCE_TYPE_CHANNEL_RECEIVE)
        return ERR_KERNEL_WRONG_RESOURCE_TYPE;
    // Add the channel to the message queue
    channel_set_mqueue(channel_resource.channel, mqueue_handle.mqueue, tag);
    return 0;
}
