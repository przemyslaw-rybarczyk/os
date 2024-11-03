#include <zr/types.h>

#include "included_programs.h"

#include <ctype.h>
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
    ReceiveAttachedHandle drive_attached_handles[] = {{ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}, {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}};
    err = channel_call_read(drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(u32), &i}, 0, NULL}, &(ReceiveMessage){0, NULL, 2, drive_attached_handles}, NULL);
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
    handle_t file_open_in, file_open_out;
    err = channel_create(&file_open_in, &file_open_out);
    if (err)
        return;
    handle_t file_create_in, file_create_out;
    err = channel_create(&file_create_in, &file_create_out);
    if (err)
        return;
    handle_t file_delete_in, file_delete_out;
    err = channel_create(&file_delete_in, &file_delete_out);
    if (err)
        return;
    handle_t file_move_in, file_move_out;
    err = channel_create(&file_move_in, &file_move_out);
    if (err)
        return;
    ResourceName fs_resource_names[] = {
        resource_name("virt_drive/info"),
        resource_name("virt_drive/read"),
        resource_name("virt_drive/write"),
        resource_name("file/stat_r"),
        resource_name("file/list_r"),
        resource_name("file/open_r"),
        resource_name("file/create_r"),
        resource_name("file/delete_r"),
        resource_name("file/move_r"),
    };
    SendAttachedHandle fs_resource_handles[] = {
        {ATTACHED_HANDLE_FLAG_MOVE, drive_attached_handles[0].handle_i},
        {ATTACHED_HANDLE_FLAG_MOVE, drive_attached_handles[1].handle_i},
        {ATTACHED_HANDLE_FLAG_MOVE, file_stat_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_list_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_open_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_create_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_delete_out},
        {ATTACHED_HANDLE_FLAG_MOVE, file_move_out},
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
        ReceiveAttachedHandle received_handles[4];
        for (int i = 0; i < 4; i++)
            received_handles[i].type = ATTACHED_HANDLE_TYPE_CHANNEL_SEND;
        err = channel_call_read(file_open_in, &(SendMessage){
            1, (SendMessageData[]){
                {strlen(path_buf), path_buf}},
            0, NULL}, &(ReceiveMessage){0, NULL, 4, received_handles}, NULL);
        if (err) {
            printf("Error when opening: %zX\n", err);
            continue;
        }
        u64 file_size;
        err = channel_call_read(received_handles[2].handle_i, NULL, &(ReceiveMessage){sizeof(u64), &file_size, 0, NULL}, NULL);
        if (err) {
            printf("Error when getting file size: %zX\n", err);
            continue;
        }
        printf("File size: %zX\n", file_size);
    }
}
