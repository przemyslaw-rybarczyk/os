#pragma once

#include "types.h"

typedef enum err_t : u64 {
    ERR_NONE,
    ERR_INVALID_SYSCALL_NUMBER,
    ERR_INVALID_ARG,
    ERR_OUT_OF_RANGE,
    ERR_NO_MEMORY,
    ERR_UNSUPPORTED,
    ERR_PAGE_ALREADY_MAPPED,
} err_t;
