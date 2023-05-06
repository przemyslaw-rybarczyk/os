#pragma once

#include "types.h"
#include "error.h"

#include "channel.h"

#define RESOURCE_NAME_MAX 32

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

typedef struct ResourceName {
    // The name is a string of nonzero bytes, padded with zeroes if shorter than the maximum length
    u8 bytes[RESOURCE_NAME_MAX];
} ResourceName;

typedef struct ResourceListEntry {
    ResourceName name;
    Resource resource;
} ResourceListEntry;

typedef struct ResourceList {
    size_t length;
    ResourceListEntry *entries;
} ResourceList;

ResourceName resource_name(const char *str);
err_t syscall_channel_get(const char *name_str, handle_t *handle_i_ptr);
err_t syscall_mqueue_add_channel(handle_t mqueue_i, const char *channel_name_str, MessageTag tag);
