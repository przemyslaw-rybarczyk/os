#pragma once

#include "types.h"

#define ERR_KERNEL_MIN 0xFFFFFFFFFFFF0000

typedef enum err_t : u64 {
    ERR_OTHER = 1,
    ERR_INVALID_ARG,
    ERR_NO_MEMORY,
    ERR_KERNEL_OTHER = ERR_KERNEL_MIN,
    ERR_KERNEL_INVALID_ARG,
    ERR_KERNEL_NO_MEMORY,
    ERR_KERNEL_INVALID_SYSCALL_NUMBER,
    ERR_KERNEL_PAGE_ALREADY_MAPPED,
    ERR_KERNEL_INVALID_HANDLE,
    ERR_KERNEL_WRONG_HANDLE_TYPE,
    ERR_KERNEL_INVALID_ADDRESS,
    ERR_KERNEL_MESSAGE_TOO_SHORT,
    ERR_KERNEL_MESSAGE_TOO_LONG,
    ERR_KERNEL_INVALID_RESOURCE,
    ERR_KERNEL_WRONG_RESOURCE_TYPE,
} err_t;
