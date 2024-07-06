#include <zr/types.h>

#include "included_programs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zr/drive.h>
#include <zr/error.h>
#include <zr/syscalls.h>
#include <zr/time.h>

static char path_buf[256];

void main(void) {
    err_t err;
    handle_t process_spawn_channel, drive_open_channel;
    err = resource_get(&resource_name("process/spawn"), RESOURCE_TYPE_CHANNEL_SEND, &process_spawn_channel);
    if (err)
        return;
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
    for (u32 i = 0; i < drive_num; i++)
        printf("%u: guid: %016lX%016lX, size: %016lX\n", i, drive_info[i].guid[1], drive_info[i].guid[0], drive_info[i].size);
    printf("Partition number:\n");
    u32 i;
    if (scanf("%u", &i) != 1)
        return;
    getchar();
    if (i >= drive_num)
        return;
    ReceiveAttachedHandle drive_read_attached_handle = {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0};
    err = channel_call_read(drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(u32), &i}, 0, NULL}, &(ReceiveMessage){0, NULL, 1, &drive_read_attached_handle}, NULL);
    if (err)
        return;
    handle_t file_stat_in, file_stat_out;
    err = channel_create(&file_stat_in, &file_stat_out);
    if (err)
        return;
    handle_t file_list_in, file_list_out;
    err = channel_create(&file_list_in, &file_list_out);
    if (err)
        return;
    ResourceName fs_resource_names[] = {
        resource_name("virt_drive/info"),
        resource_name("virt_drive/read"),
        resource_name("file/stat_r"),
        resource_name("file/list_r"),
    };
    SendAttachedHandle fs_resource_handles[] = {
        {ATTACHED_HANDLE_FLAG_MOVE, drive_read_attached_handle.handle_i},
        {ATTACHED_HANDLE_FLAG_MOVE, file_stat_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_list_out},
    };
    err = channel_call(process_spawn_channel, &(SendMessage){
        5, (SendMessageData[]){
            {sizeof(size_t), &(size_t){1}},
            {sizeof(fs_resource_names), fs_resource_names},
            {sizeof(size_t), &(size_t){sizeof(VirtDriveInfo)}},
            {sizeof(VirtDriveInfo), &drive_info[i]},
            {included_file_fat32_end - included_file_fat32, included_file_fat32}},
        1, &(SendMessageHandles){sizeof(fs_resource_handles) / sizeof(fs_resource_handles[0]), fs_resource_handles}
    }, NULL);
    if (err)
        return;
    while (1) {
        printf("Path: \n");
        if (scanf("%255[^\n]", path_buf) != 1)
            return;
        handle_t reply;
        err = channel_call(file_list_in, &(SendMessage){1, &(SendMessageData){strlen(path_buf), path_buf}, 0, NULL}, &reply);
        if (err == ERR_DOES_NOT_EXIST) {
            printf("Error: directory does not exist\n");
        } else if (err == ERR_NOT_DIR) {
            printf("Error: not a directory\n");
        } else if (err) {
            printf("Error: %zX\n", err);
        } else {
            MessageLength reply_size;
            message_get_length(reply, &reply_size);
            size_t reply_offset = 0;
            while (reply_offset < reply_size.data) {
                u32 name_length;
                err = message_read(reply, &(ReceiveMessage){sizeof(u32), &name_length, 0, NULL}, &(MessageLength){reply_offset, 0}, NULL, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
                if (err)
                    return;
                reply_offset += sizeof(u32);
                err = message_read(reply, &(ReceiveMessage){name_length <= 255 ? name_length : 255, path_buf, 0, NULL}, &(MessageLength){reply_offset, 0}, NULL, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
                if (err)
                    return;
                reply_offset += name_length;
                printf("%.*s\n", name_length, path_buf);
            }
        }
    }
}
