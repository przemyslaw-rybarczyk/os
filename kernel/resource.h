#pragma once

#include "types.h"
#include "error.h"

#include <zr/syscalls.h>

#include "channel.h"

typedef enum ResourceType {
    RESOURCE_TYPE_EMPTY,
    RESOURCE_TYPE_CHANNEL_SEND,
    RESOURCE_TYPE_CHANNEL_RECEIVE,
} ResourceType;

typedef struct Resource {
    ResourceType type;
    union {
        Channel *channel;
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

err_t syscall_channel_get(ResourceName *name, handle_t *handle_i_ptr);
err_t syscall_mqueue_add_channel_resource(handle_t mqueue_i, ResourceName *channel_name, MessageTag tag);
