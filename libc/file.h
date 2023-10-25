#pragma once

#include <stdio.h>

#include <zr/types.h>

typedef enum FileType {
    FILE_INVALID,
    FILE_BUFFER,
    FILE_CHANNEL,
} FileType;

typedef enum FileMode {
    FILE_R,
    FILE_W,
    FILE_RW,
} FileMode;

#undef _IONBF
#undef _IOLBF
#undef _IOFBF

typedef enum BufferMode {
    _IONBF,
    _IOLBF,
    _IOFBF,
} BufferMode;

struct __FILE {
    FileType type;
    FileMode mode;
    BufferMode buffer_mode;
    char *restrict buffer;
    size_t buffer_capacity;
    size_t buffer_size;
    size_t buffer_offset;
    handle_t channel;
    bool eof;
    bool error;
    bool ungetc_buffer_full;
    unsigned char ungetc_buffer;
};
