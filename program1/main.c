#include "types.h"

#include <syscalls.h>

void main(void) {
    err_t err;
    u64 arg_length;
    err = message_get_length(0, &arg_length);
    if (err)
        return;
    u64 c;
    err = message_read(0, &c);
    if (err)
        return;
    while (1) {
        size_t reply_msg;
        err = channel_call(1, sizeof(c), &c, &reply_msg);
        if (err)
            continue;
        size_t reply_length;
        err = message_get_length(reply_msg, &reply_length);
        if (err || reply_length != sizeof(u64))
            continue;
        u64 reply;
        err = message_read(reply_msg, &reply);
        if (err)
            continue;
        c = reply;
    }
}
