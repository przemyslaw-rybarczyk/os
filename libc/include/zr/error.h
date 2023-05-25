#pragma once

#include "types.h"

#define ERR_KERNEL_MIN 0xFFFFFFFFFFFF0000
#define ERR_KERNEL_SPECIFIC_MIN 0xFFFFFFFFFFFF1000

typedef enum err_t : u64 {
    // General error codes
    ERR_OTHER = 1,
    ERR_INVALID_ARG,
    ERR_NO_MEMORY,
    // General kernel error codes - analogous to the general error codes
    ERR_KERNEL_OTHER = ERR_KERNEL_MIN + 1,
    ERR_KERNEL_INVALID_ARG,
    ERR_KERNEL_NO_MEMORY,
    // Specific kernel error codes - these have to analogues to general error codes
    ERR_KERNEL_INVALID_SYSCALL_NUMBER = ERR_KERNEL_SPECIFIC_MIN,
    ERR_KERNEL_PAGE_ALREADY_MAPPED,
    ERR_KERNEL_INVALID_HANDLE,
    ERR_KERNEL_WRONG_HANDLE_TYPE,
    ERR_KERNEL_INVALID_ADDRESS,
    ERR_KERNEL_MESSAGE_DATA_TOO_SHORT,
    ERR_KERNEL_MESSAGE_DATA_TOO_LONG,
    ERR_KERNEL_MESSAGE_HANDLES_TOO_SHORT,
    ERR_KERNEL_MESSAGE_HANDLES_TOO_LONG,
    ERR_KERNEL_INVALID_RESOURCE,
    ERR_KERNEL_WRONG_RESOURCE_TYPE,
    ERR_KERNEL_CHANNEL_CLOSED,
    ERR_KERNEL_MESSAGE_WRONG_HANDLE_TYPE,
    ERR_KERNEL_UNCOPIEABLE_HANDLE_TYPE,
    ERR_KERNEL_MQUEUE_ALREADY_SET,
} err_t;

#ifdef _KERNEL

// Convert an error code to a user error code
static inline err_t user_error_code(err_t err) {
    if (err < ERR_KERNEL_MIN)
        return err;
    else if (err < ERR_KERNEL_SPECIFIC_MIN)
        return err - ERR_KERNEL_MIN;
    else
        return ERR_INVALID_ARG;
}

#endif
