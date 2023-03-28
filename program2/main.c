#include "types.h"

#include <syscalls.h>

void main(void) {
    err_t err;
    while (1) {
        size_t message;
        err = channel_receive(1, &message);
        if (err)
            continue;
        size_t message_length;
        err = message_get_length(message, &message_length);
        if (err || message_length != sizeof(u64))
            continue;
        u64 c;
        err = message_read(message, &c);
        if (err)
            continue;
        print_char(c);
    }
}
