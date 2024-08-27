#include <zr/types.h>

#include <stdio.h>
#include <stdlib.h>

#include <zr/drive.h>
#include <zr/error.h>
#include <zr/syscalls.h>

#include "included_programs.h"

#define MBR_TABLE_OFFSET 440

#define MBR_PART_TYPE_NONE 0x00
#define MBR_PART_TYPE_GPT 0xEE

#define GPT_SIGNATURE UINT64_C(0x5452415020494645)

#define INIT_PARTITIONS_CAP 8

typedef struct MBRPartition {
    u8 boot_indicator;
    u8 start_chs[3];
    u8 type;
    u8 end_chs[3];
    u32 start_lba;
    u32 size_lba;
} MBRPartition;

typedef struct MBRTable {
    u32 disk_id;
    u16 reserved1;
    MBRPartition part[4];
    u16 boot_signature;
} __attribute__((packed)) MBRTable;

typedef struct GPTHeader {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved1;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u64 disk_guid[2];
    u64 partition_entry_lba;
    u32 partition_entry_num;
    u32 partition_entry_size;
    u32 partition_entry_crc32;
} __attribute__((packed)) GPTHeader;

typedef struct GPTPartition {
    u64 type[2];
    u64 guid[2];
    u64 start_lba;
    u64 end_lba;
    u64 attrs;
    u8 name[72];
} GPTPartition;

static MBRTable mbr_table;
static GPTHeader gpt_header;

typedef struct Partition {
    u64 guid[2];
    u32 drive_i;
    u64 sector_start;
    u64 sector_count;
} Partition;

static Partition *partitions;
static size_t partitions_count = 0;
static size_t partitions_capacity = INIT_PARTITIONS_CAP;

static PhysDriveInfo *drive_info;

// Add a partition to the partition list
static err_t add_partition(Partition *partition) {
    // Extend partition list if not large enough
    if (partitions_count >= partitions_capacity) {
        Partition *new_partitions = realloc(partitions, 2 * partitions_capacity);
        if (new_partitions == NULL)
            return ERR_NO_MEMORY;
        partitions = new_partitions;
        partitions_capacity *= 2;
    }
    partitions[partitions_count++] = *partition;
    return 0;
}

static err_t drive_read(handle_t channel, u64 offset, u64 length, void *dest) {
    return channel_call_read(
        channel,
        &(SendMessage){1, &(SendMessageData){sizeof(FileRange), &(FileRange){offset, length}}, 0, NULL},
        &(ReceiveMessage){length, dest, 0, NULL},
        NULL);
}

// Compute a CRC32 checksum of the given data
static u32 crc32(const u8 *data, size_t length) {
    u32 poly = UINT32_C(0xEDB88320); // Reflected CRC-32 polynomial
    u32 rem = UINT32_C(-1);
    for (size_t i = 0; i < length; i++) {
        rem ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (rem & 1)
                rem = (rem >> 1) ^ poly;
            else
                rem >>= 1;
        }
    }
    return ~rem;
}

// Parse a GPT header and partition table and partitions from it to the list
static err_t read_gpt(size_t drive_i, handle_t drive_read_handle, u64 header_sector) {
    err_t err;
    // Read GPT header
    err = drive_read(drive_read_handle, header_sector * drive_info[drive_i].sector_size, sizeof(GPTHeader), &gpt_header);
    if (err)
        return err;
    // Verify if signature and myLBA field are correct and if header size field has sensible value
    if (gpt_header.signature != GPT_SIGNATURE || gpt_header.my_lba != header_sector || gpt_header.header_size > 4096)
        return ERR_OTHER;
    // Read full header for purpose of checksum verification
    void *full_gpt_header = malloc(gpt_header.header_size);
    if (full_gpt_header == NULL)
        return ERR_NO_MEMORY;
    err = drive_read(drive_read_handle, header_sector * drive_info[drive_i].sector_size, gpt_header.header_size, full_gpt_header);
    if (err)
        return err;
    // Verify checksum of header
    ((GPTHeader *)full_gpt_header)->header_crc32 = 0;
    if (crc32(full_gpt_header, gpt_header.header_size) != gpt_header.header_crc32)
        return ERR_OTHER;
    free(full_gpt_header);
    // Read partition table
    size_t gpt_parts_size = (size_t)gpt_header.partition_entry_num * (size_t)gpt_header.partition_entry_size;
    void *gpt_parts = malloc(gpt_parts_size);
    if (gpt_parts == NULL)
        return ERR_NO_MEMORY;
    err = drive_read(drive_read_handle, gpt_header.partition_entry_lba * drive_info[drive_i].sector_size, gpt_parts_size, gpt_parts);
    if (err)
        return err;
    // Verify checksum of partition table
    if (crc32(gpt_parts, gpt_parts_size) != gpt_header.partition_entry_crc32)
        return ERR_OTHER;
    // Add existing partitions from table to list
    for (size_t part_i = 0; part_i < gpt_header.partition_entry_num; part_i++) {
        GPTPartition *gpt_part = gpt_parts + part_i * (size_t)gpt_header.partition_entry_size;
        if (gpt_part->type[0] == 0 && gpt_part->type[1] == 0)
            continue;
        err = add_partition(&(Partition){
            {gpt_part->guid[0], gpt_part->guid[1]},
            drive_i, gpt_part->start_lba, gpt_part->end_lba - gpt_part->start_lba + 1
        });
        if (err)
            return err;
    }
    return 0;
}

void main(void) {
    err_t err;
    handle_t phys_drive_open_channel, process_spawn_channel, video_redraw_channel, keyboard_key_channel, mouse_button_channel, mouse_move_channel, mouse_scroll_channel;
    err = resource_get(&resource_name("phys_drive/open"), RESOURCE_TYPE_CHANNEL_SEND, &phys_drive_open_channel);
    if (err)
        return;
    err = resource_get(&resource_name("process/spawn"), RESOURCE_TYPE_CHANNEL_SEND, &process_spawn_channel);
    if (err)
        return;
    err = resource_get(&resource_name("video/redraw"), RESOURCE_TYPE_CHANNEL_RECEIVE, &video_redraw_channel);
    if (err)
        return;
    err = resource_get(&resource_name("keyboard/key"), RESOURCE_TYPE_CHANNEL_RECEIVE, &keyboard_key_channel);
    if (err)
        return;
    err = resource_get(&resource_name("mouse/button"), RESOURCE_TYPE_CHANNEL_RECEIVE, &mouse_button_channel);
    if (err)
        return;
    err = resource_get(&resource_name("mouse/move"), RESOURCE_TYPE_CHANNEL_RECEIVE, &mouse_move_channel);
    if (err)
        return;
    err = resource_get(&resource_name("mouse/scroll"), RESOURCE_TYPE_CHANNEL_RECEIVE, &mouse_scroll_channel);
    if (err)
        return;
    // Get physical drive info
    handle_t drive_info_msg;
    err = resource_get(&resource_name("phys_drive/info"), RESOURCE_TYPE_MESSAGE, &drive_info_msg);
    if (err)
        return;
    MessageLength drive_info_length;
    message_get_length(drive_info_msg, &drive_info_length);
    if (drive_info_length.data % sizeof(PhysDriveInfo) != 0)
        return;
    u32 drive_num = drive_info_length.data / sizeof(PhysDriveInfo);
    drive_info = malloc(drive_info_length.data);
    if (drive_info == NULL)
        return;
    message_read(drive_info_msg, &(ReceiveMessage){drive_info_length.data, drive_info, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
    // Allocate partitions array
    partitions = malloc(partitions_capacity * sizeof(Partition));
    if (partitions == NULL)
        return;
    // Detect partitions
    for (size_t drive_i = 0; drive_i < drive_num; drive_i++) {
        ReceiveAttachedHandle drive_attached_handles[] = {{ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}, {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}};
        err = channel_call_read(phys_drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(PhysDriveOpenArgs), &(PhysDriveOpenArgs){drive_i, 0, UINT64_MAX}}, 0, NULL}, &(ReceiveMessage){0, NULL, 2, drive_attached_handles}, NULL);
        if (err)
            return;
        handle_t drive_read_handle = drive_attached_handles[0].handle_i;
        err = drive_read(drive_read_handle, MBR_TABLE_OFFSET, sizeof(MBRTable), &mbr_table);
        if (err)
            return;
        if (mbr_table.boot_signature != 0xAA55)
            goto part_read_fail;
        // Go through MBR partition table and count how many partitions are empty
        // and check if there exists a GPT protective partition
        int empty_parts = 0;
        int gpt_prot_part = -1;
        for (int part_i = 0; part_i < 4; part_i++) {
            if (mbr_table.part[part_i].type == MBR_PART_TYPE_NONE)
                empty_parts++;
            else if (mbr_table.part[part_i].type == MBR_PART_TYPE_GPT && mbr_table.part[part_i].start_lba == 1)
                gpt_prot_part = part_i;
        }
        if (empty_parts == 3 && gpt_prot_part != -1) {
            // If there is a GPT protective partition and all others are empty, the partition table is GPT
            err = read_gpt(drive_i, drive_read_handle, 1);
            if (err == ERR_NO_MEMORY)
                return;
            else if (err) {
                err = read_gpt(drive_i, drive_read_handle, drive_info[drive_i].sector_count - 1);
                if (err == ERR_NO_MEMORY)
                    return;
            }
        } else {
            // Otherwise, partition table is MBR
            for (int part_i = 0; part_i < 4; part_i++) {
                if (mbr_table.part[part_i].type == MBR_PART_TYPE_NONE)
                    goto part_read_fail;
                err = add_partition(&(Partition){
                    {(u64)mbr_table.disk_id << 8 | (u64)(part_i + 1), 0},
                    drive_i, mbr_table.part[part_i].start_lba, mbr_table.part[part_i].size_lba
                });
                if (err)
                    return;
            }
        }
part_read_fail:
        handle_free(drive_read_handle);
    }
    // Allocate drive information structure to pass to process
    VirtDriveInfo *virt_drive_info = malloc(partitions_count * sizeof(VirtDriveInfo));
    if (partitions_count != 0 && virt_drive_info == NULL)
        return;
    for (size_t part_i = 0; part_i < partitions_count; part_i++) {
        virt_drive_info[part_i] = (VirtDriveInfo){
            {partitions[part_i].guid[0], partitions[part_i].guid[1]},
            partitions[part_i].sector_count * drive_info[partitions[part_i].drive_i].sector_size
        };
    }
    // Create mqueue and channel for opening partitions
    handle_t mqueue;
    err = mqueue_create(&mqueue);
    if (err)
        return;
    handle_t virt_drive_open_in, virt_drive_open_out;
    err = channel_create(&virt_drive_open_in, &virt_drive_open_out);
    if (err)
        return;
    mqueue_add_channel(mqueue, virt_drive_open_out, (MessageTag){0, 0});
    // Spawn process
    ResourceName window_resource_names[] = {
        resource_name("virt_drive/info"),
        resource_name("video/redraw"),
        resource_name("keyboard/key"),
        resource_name("mouse/button"),
        resource_name("mouse/move"),
        resource_name("mouse/scroll"),
        resource_name("process/spawn"),
        resource_name("virt_drive/open"),
    };
    SendAttachedHandle window_resource_handles[] = {
        {ATTACHED_HANDLE_FLAG_MOVE, video_redraw_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, keyboard_key_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_button_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_move_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, mouse_scroll_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, process_spawn_channel},
        {ATTACHED_HANDLE_FLAG_MOVE, virt_drive_open_in},
    };
    err = channel_call(process_spawn_channel, &(SendMessage){
        5, (SendMessageData[]){
            {sizeof(size_t), &(size_t){1}},
            {sizeof(window_resource_names), window_resource_names},
            {sizeof(size_t), &(size_t){partitions_count * sizeof(VirtDriveInfo)}},
            {partitions_count * sizeof(VirtDriveInfo), virt_drive_info},
            {included_file_window_end - included_file_window, included_file_window}},
        1, &(SendMessageHandles){sizeof(window_resource_handles) / sizeof(window_resource_handles[0]), window_resource_handles}
    }, NULL);
    if (err)
        return;
    free(virt_drive_info);
    while (1) {
        // Wait for a message
        handle_t msg;
        mqueue_receive(mqueue, NULL, &msg, TIMEOUT_NONE, 0);
        u32 part_i;
        err = message_read(msg, &(ReceiveMessage){sizeof(u32), &part_i, 0, NULL}, NULL, NULL, 0, 0);
        if (err)
            goto loop_fail;
        // Check if partition number is valid
        if (part_i > partitions_count) {
            err = ERR_DOES_NOT_EXIST;
            goto loop_fail;
        }
        // Create handle by calling physical drive
        ReceiveAttachedHandle drive_attached_handles[] = {{ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}, {ATTACHED_HANDLE_TYPE_CHANNEL_SEND, 0}};
        u64 sector_size = drive_info[partitions[part_i].drive_i].sector_size;
        PhysDriveOpenArgs drive_open_args = {partitions[part_i].drive_i, partitions[part_i].sector_start * sector_size, partitions[part_i].sector_count * sector_size};
        err = channel_call_read(phys_drive_open_channel, &(SendMessage){1, &(SendMessageData){sizeof(PhysDriveOpenArgs), &drive_open_args}, 0, NULL}, &(ReceiveMessage){0, NULL, 2, drive_attached_handles}, NULL);
        if (err)
            goto loop_fail;
        // Reply to the message
        message_reply(msg, &(SendMessage){0, NULL, 1, &(SendMessageHandles){2, (SendAttachedHandle[]){{ATTACHED_HANDLE_FLAG_MOVE, drive_attached_handles[0].handle_i}, {ATTACHED_HANDLE_FLAG_MOVE, drive_attached_handles[1].handle_i}}}}, FLAG_FREE_MESSAGE);
        continue;
loop_fail:
        message_reply_error(msg, user_error_code(err), FLAG_FREE_MESSAGE);
    }
}
