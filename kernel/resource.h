#pragma once

#include "types.h"
#include "error.h"

#include <zr/syscalls.h>

#include "channel.h"

typedef struct Resource {
    ResourceType type;
    union {
        Channel *channel;
        Message *message;
    };
} Resource;

typedef struct ResourceListEntry {
    ResourceName name;
    Resource resource;
} ResourceListEntry;

typedef struct ResourceList {
    size_t length;
    ResourceListEntry *entries;
} ResourceList;

void resource_list_free(ResourceList *list);
err_t syscall_resource_get(const ResourceName *name, ResourceType type, handle_t *handle_i_ptr);
err_t syscall_mqueue_add_channel_resource(handle_t mqueue_i, const ResourceName *channel_name, MessageTag tag);
err_t syscall_message_resource_read(const ResourceName *message_name, size_t data_length, void *data, size_t min_data_length, u64 flags);
