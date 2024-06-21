#include <zr/types.h>

#include <stdio.h>
#include <stdlib.h>

#include <zr/drive.h>
#include <zr/syscalls.h>

static u8 data_buf[1024];

void main(void) {
    err_t err;
    handle_t drive_open_channel;
    err = resource_get(&resource_name("virt_drive/open"), RESOURCE_TYPE_CHANNEL_SEND, &drive_open_channel);
    if (err)
        return;
    handle_t drive_info_msg;
    err = resource_get(&resource_name("virt_drive/info"), RESOURCE_TYPE_MESSAGE, &drive_info_msg);
    if (err)
        return;
    MessageLength drive_info_length;
    message_get_length(drive_info_msg, &drive_info_length);
    if (drive_info_length.data % sizeof(VirtDriveInfo) != 0)
        return;
    u32 drive_num = drive_info_length.data / sizeof(VirtDriveInfo);
    VirtDriveInfo *drive_info = malloc(drive_info_length.data);
    if (drive_info_length.data != 0 && drive_info == NULL)
        return;
    message_read(drive_info_msg, &(ReceiveMessage){drive_info_length.data, drive_info, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
    printf("Found %d partitions\n", drive_num);
    for (u32 i = 0; i < drive_num; i++) {
        printf("guid: %016lX%016lX, size: %016lX\n", drive_info[i].guid[1], drive_info[i].guid[0], drive_info[i].size);
        printf("Opening drive\n");
        ReceiveAttachedHandle drive_read_attached_handle = {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0};
        err = channel_call_read(drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(u32), &i}, 0, NULL}, &(ReceiveMessage){0, NULL, 1, &drive_read_attached_handle}, NULL);
        if (err) {
            printf("Got error %zX\n", err);
            return;
        }
        handle_t drive_read_handle = drive_read_attached_handle.handle_i;
        printf("Reading first 1K\n");
        err = channel_call_read(drive_read_handle, &(SendMessage){1, &(SendMessageData){sizeof(FileRange), &(FileRange){0, 1024}}, 0, NULL}, &(ReceiveMessage){1024, data_buf, 0, NULL}, NULL);
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
