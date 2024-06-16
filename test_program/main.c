#include <zr/types.h>

#include <stdio.h>

#include <zr/syscalls.h>

static u8 data_buf[1024];

void main(void) {
    err_t err;
    handle_t drive_info_channel, drive_open_channel;
    err = resource_get(&resource_name("drive/info"), RESOURCE_TYPE_CHANNEL_SEND, &drive_info_channel);
    if (err)
        return;
    err = resource_get(&resource_name("drive/open"), RESOURCE_TYPE_CHANNEL_SEND, &drive_open_channel);
    if (err)
        return;
    printf("Getting drive information\n");
    handle_t drive_info_reply;
    err = channel_call(drive_info_channel, &(SendMessage){0, NULL, 0, NULL}, &drive_info_reply);
    if (err) {
        printf("Got error %zX\n", err);
        return;
    }
    MessageLength drive_info_reply_length;
    message_get_length(drive_info_reply, &drive_info_reply_length);
    printf("Number of drives: %zu\n", drive_info_reply_length.data / sizeof(size_t));
    for (size_t i = 0; sizeof(size_t) * i < drive_info_reply_length.data; i++) {
        size_t drive_size;
        message_read(drive_info_reply, &(ReceiveMessage){sizeof(size_t), &drive_size, 0, NULL}, &(MessageLength){sizeof(size_t) * i, 0}, NULL, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
        printf("Size of drive #%zu is %zu B\n", i, drive_size);
        printf("Opening drive\n");
        ReceiveAttachedHandle drive_read_attached_handle = {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0};
        err = channel_call_read(drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(size_t), &i}, 0, NULL}, &(ReceiveMessage){0, NULL, 1, &drive_read_attached_handle}, NULL);
        if (err) {
            printf("Got error %zX\n", err);
            return;
        }
        handle_t drive_read_handle = drive_read_attached_handle.handle_i;
        printf("Reading first 1K\n");
        err = channel_call_read(drive_read_handle, &(SendMessage){1, &(SendMessageData){2 * sizeof(u64), (u64[]){0, 1024}}, 0, NULL}, &(ReceiveMessage){1024, data_buf, 0, NULL}, NULL);
        if (err) {
            printf("Got error %zX\n", err);
            return;
        }
        printf("Received data\n");
        for (size_t y = 0; y < 32; y++) {
            for (size_t x = 0; x < 32; x++)
                printf("%02X ", data_buf[32 * y + x]);
            printf("\n");
        }
    }
}
