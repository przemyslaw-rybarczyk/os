#include <zr/types.h>

#include <zr/error.h>
#include <zr/syscalls.h>

#include "included_programs.h"

void main(void) {
    err_t err;
    handle_t video_size_channel, video_data_channel, keyboard_data_channel, mouse_data_channel, process_spawn_channel, test_1_channel_in, test_1_channel_out;
    err = resource_get(&resource_name("video/size"), RESOURCE_TYPE_CHANNEL_SEND, &video_size_channel);
    if (err)
        return;
    err = resource_get(&resource_name("video/data"), RESOURCE_TYPE_CHANNEL_SEND, &video_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("keyboard/data"), RESOURCE_TYPE_CHANNEL_RECEIVE, &keyboard_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("mouse/data"), RESOURCE_TYPE_CHANNEL_RECEIVE, &mouse_data_channel);
    if (err)
        return;
    err = resource_get(&resource_name("process/spawn"), RESOURCE_TYPE_CHANNEL_SEND, &process_spawn_channel);
    if (err)
        return;
    err = channel_create(&test_1_channel_in, &test_1_channel_out);
    if (err)
        return;
    ResourceName program1_resource_names[] = {resource_name("video/size"), resource_name("keyboard/data"), resource_name("mouse/data"), resource_name("test/1")};
    SendAttachedHandle program1_resource_handles[] = {{0, video_size_channel}, {ATTACHED_HANDLE_FLAG_MOVE, keyboard_data_channel}, {ATTACHED_HANDLE_FLAG_MOVE, mouse_data_channel}, {0, test_1_channel_in}};
    err = channel_call(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program1_resource_names), program1_resource_names},
            {included_file_program1_end - included_file_program1, included_file_program1}},
        1, &(SendMessageHandles){sizeof(program1_resource_handles) / sizeof(program1_resource_handles[0]), program1_resource_handles}
    }, NULL);
    if (err)
        return;
    ResourceName program2_resource_names[] = {resource_name("video/data"), resource_name("test/1")};
    SendAttachedHandle program2_resource_handles[] = {{0, video_data_channel}, {ATTACHED_HANDLE_FLAG_MOVE, test_1_channel_out}};
    err = channel_call(process_spawn_channel, &(SendMessage){
        2, (SendMessageData[]){
            {sizeof(program2_resource_names), program2_resource_names},
            {included_file_program2_end - included_file_program2, included_file_program2}},
        1, &(SendMessageHandles){sizeof(program2_resource_handles) / sizeof(program2_resource_handles[0]), program2_resource_handles}
    }, NULL);
    if (err)
        return;
}
