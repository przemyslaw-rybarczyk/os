#include <zr/types.h>

#include <zr/syscalls.h>

void main(void) {
    err_t err;
    handle_t video_data_channel;
    err = channel_get("video/data", &video_data_channel);
    if (err)
        return;
    channel_call(3, &(SendMessage){{0, 1}, NULL, &(SendAttachedHandle){0, video_data_channel}}, NULL);
}
