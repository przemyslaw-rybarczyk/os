#include "types.h"

#include <syscalls.h>

void main(void) {
    err_t err;
    u64 arg_length;
    err = message_get_length(0, &arg_length);
    if (err)
        return;
    u64 arg;
    err = message_read(0, &arg);
    if (err)
        return;
    while (1) {
        channel_send(1, sizeof(arg), &arg);
    }
}
