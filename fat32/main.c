#include <zr/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zr/drive.h>
#include <zr/error.h>
#include <zr/syscalls.h>
#include <zr/time.h>

#define FAT_BAD_CLUSTER UINT32_C(0x0FFFFFF7)
#define FAT_EOF_MIN UINT32_C(0x0FFFFFF8)
#define FAT_ENTRY_MASK UINT32_C(0x0FFFFFFF)

#define DIR_ENTRY_ATTR_READ_ONLY 0x01
#define DIR_ENTRY_ATTR_HIDDEN 0x02
#define DIR_ENTRY_ATTR_SYSTEM 0x04
#define DIR_ENTRY_ATTR_VOLUME_ID 0x08
#define DIR_ENTRY_ATTR_DIRECTORY 0x10
#define DIR_ENTRY_ATTR_ARCHIVE 0x20

#define LONG_NAME_ATTR 0x0F
#define LONG_NAME_ATTR_MASK 0x3F
#define LONG_NAME_ORD_MASK 0x3F
#define LONG_NAME_ORD_LAST 0x40

typedef struct BPB {
    u8 jump[3];
    u8 oem_name[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors_num;
    u8 fats_num;
    u16 root_entries_num;
    u16 total_sectors_16;
    u8 media;
    u16 fat_sector_num_16;
    u16 sectors_per_track;
    u16 heads_num;
    u32 hidden_sectors_num;
    u32 total_sectors_32;
    u32 fat_sector_num_32;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fsinfo_sector;
    u16 backup_boot_sector;
    u8 reserved1[12];
    u8 drive_number;
    u8 reserved2;
    u8 ext_boot_signature;
    u32 volume_number;
    u8 volume_label[11];
    u8 fs_type[8];
    u8 reserved3[420];
    u16 signature;
} __attribute__((packed)) BPB;

typedef struct DirEntry {
    u8 name[11];
    u8 attr;
    u8 reserved1;
    u8 creation_time_tens;
    u16 creation_time;
    u16 creation_date;
    u16 access_date;
    u16 first_cluster_high;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_low;
    u32 file_size;
} DirEntry;

typedef struct LongNameDirEntry {
    u8 ord;
    u16 name1[5];
    u8 attr;
    u8 type;
    u8 checksum;
    u16 name2[6];
    u16 reserved1;
    u16 name3[2];
} __attribute__((packed)) LongNameDirEntry;

// Dummy directory entry used to represent root directory
static DirEntry root_dir_entry = {.attr = DIR_ENTRY_ATTR_DIRECTORY};

// Get number of first cluster from directory entry
static u32 entry_first_cluster(const DirEntry *entry) {
    return ((u32)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

static handle_t drive_channel;

// Read data from drive
static err_t drive_read(u64 offset, u64 length, void *dest) {
    return channel_call_read(
        drive_channel,
        &(SendMessage){1, &(SendMessageData){sizeof(FileRange), &(FileRange){offset, length}}, 0, NULL},
        &(ReceiveMessage){length, dest, 0, NULL},
        NULL);
}

static u64 fat_offset;
static u64 data_offset;
static u32 fat_length;
static u32 root_cluster;
static u32 cluster_size;

// Parse and verify the BPB
static err_t parse_bpb(BPB *bpb, u64 drive_size) {
    if (!((bpb->jump[0] == 0xEB && bpb->jump[2] == 0x90) || bpb->jump[0] == 0xE9))
        return ERR_OTHER;
    if (bpb->bytes_per_sector < 512 || bpb->bytes_per_sector > 4096 || (bpb->bytes_per_sector & (bpb->bytes_per_sector - 1)) != 0)
        return ERR_OTHER;
    if (bpb->sectors_per_cluster == 0 || bpb->sectors_per_cluster > 128 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0)
        return ERR_OTHER;
    if (bpb->reserved_sectors_num == 0 || bpb->fats_num == 0)
        return ERR_OTHER;
    if (bpb->media != 0xF0 && bpb->media < 0xF8)
        return ERR_OTHER;
    if (bpb->root_entries_num != 0 || bpb->total_sectors_16 != 0 || bpb->fat_sector_num_16 != 0)
        return ERR_OTHER;
    if (bpb->version != 0)
        return ERR_OTHER;
    if (bpb->backup_boot_sector != 0 && bpb->backup_boot_sector != 6)
        return ERR_OTHER;
    if (bpb->ext_boot_signature == 0x29 && memcmp(bpb->fs_type, "FAT32   ", 8) != 0)
        return ERR_OTHER;
    if (bpb->signature != 0xAA55)
        return ERR_OTHER;
    if ((u64)bpb->total_sectors_32 * bpb->bytes_per_sector > drive_size)
        return ERR_OTHER;
    if (bpb->reserved_sectors_num + bpb->fats_num * (u64)bpb->fat_sector_num_32 > bpb->total_sectors_32)
        return ERR_OTHER;
    u32 data_sector_num = bpb->total_sectors_32 - bpb->reserved_sectors_num - bpb->fats_num * bpb->fat_sector_num_32;
    // If there are not enough data clusters, the file system is not FAT32
    if (data_sector_num / bpb->sectors_per_cluster < 65525)
        return ERR_OTHER;
    if ((u64)bpb->fat_sector_num_32 * (bpb->bytes_per_sector / sizeof(u32)) < data_sector_num)
        return ERR_OTHER;
    fat_offset = (u64)bpb->reserved_sectors_num * bpb->bytes_per_sector;
    data_offset = (u64)(bpb->reserved_sectors_num + bpb->fats_num * bpb->fat_sector_num_32) * bpb->bytes_per_sector;
    fat_length = data_sector_num + 2;
    if (bpb->root_cluster < 2 || bpb->root_cluster >= fat_length)
        return ERR_OTHER;
    root_cluster = bpb->root_cluster;
    root_dir_entry.first_cluster_low = (u16)root_cluster;
    root_dir_entry.first_cluster_high = (u16)(root_cluster >> 16);
    cluster_size = bpb->sectors_per_cluster * bpb->bytes_per_sector;
    return 0;
}

// Read an entry for a given cluster from the FAT
static err_t fat_read_entry(u32 cluster, u32 *entry_ptr) {
    err_t err;
    err = drive_read(fat_offset + sizeof(u32) * cluster, sizeof(u32), entry_ptr);
    if (err)
        return err;
    *entry_ptr &= FAT_ENTRY_MASK;
    return 0;
}

// Same as fat_read_entry(), but returns an ERR_IO_INTERNAL if the cluster is not allocated
// and ERR_EOF if it's the last one in a file.
static err_t fat_read_entry_expect_allocated_or_eof(u32 cluster, u32 *entry_ptr) {
    err_t err;
    u32 entry;
    err = fat_read_entry(cluster, &entry);
    if (err)
        return err;
    if (entry == FAT_BAD_CLUSTER)
        return ERR_IO_INTERNAL;
    else if (entry >= FAT_EOF_MIN)
        return ERR_EOF;
    else if (entry >= fat_length || entry < 2)
        return ERR_IO_INTERNAL;
    *entry_ptr = entry;
    return 0;
}

// Get the offset of a cluster corresponding to the given FAT entry number
static u64 fat_cluster_offset(u32 cluster) {
    return data_offset + (u64)(cluster - 2) * cluster_size;
}

// Table of characters allowed in short and long file names - bit is set if character allowed
static u8 short_name_allowed_char_table[16] = {0x00, 0x00, 0x00, 0x00, 0xFB, 0x23, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xC7, 0x01, 0x00, 0x00, 0x68};
static u8 long_name_allowed_char_table[16] = {0x00, 0x00, 0x00, 0x00, 0xFB, 0x7B, 0xFF, 0x0B, 0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0x6F};

static bool char_allowed_in_short_name(u8 c) {
    return c > 0x7F || (bool)((short_name_allowed_char_table[c >> 3] >> (c & 7)) & 1);
}

static bool char_allowed_in_long_name(u8 c) {
    return c > 0x7F || (bool)((long_name_allowed_char_table[c >> 3] >> (c & 7)) & 1);
}

// Copy part of a name from a long name entry into a buffer
// If the entry is the last one, *buf_length is set to the length of the buffer.
// Does not support Unicode characters above 0xFF - returns an error for those.
static err_t copy_name_from_long_name_entry(LongNameDirEntry *lne, u8 *buf, size_t *buf_length) {
    // Get list of characters from name
    u16 chars[13] = {
        lne->name1[0], lne->name1[1], lne->name1[2], lne->name1[3], lne->name1[4],
        lne->name2[0], lne->name2[1], lne->name2[2], lne->name2[3], lne->name2[4], lne->name2[5],
        lne->name3[0], lne->name3[1],
    };
    // Copy data to the appropriate offset
    int offset = ((lne->ord & LONG_NAME_ORD_MASK) - 1) * 13;
    if (offset > 255)
        return ERR_OTHER;
    for (int i = 0; i < 13; i++) {
        if ((lne->ord & LONG_NAME_ORD_LAST) && chars[i] == 0) {
            *buf_length = offset + i;
            return 0;
        }
        if (offset + i >= 255)
            return ERR_OTHER;
        if (chars[i] > 0xFF || !char_allowed_in_long_name(chars[i]))
            return ERR_OTHER;
        buf[offset + i] = (u8)chars[i];
    }
    if (lne->ord & LONG_NAME_ORD_LAST)
        *buf_length = offset + 13;
    return 0;
}

static u8 name_buf[255];

// Get a directory entry for a file, given the first cluster of its containing folder and its name
static err_t find_entry_in_dir(u32 dir_first_cluster, const u8 *target_name, size_t target_name_length, DirEntry *entry_ptr) {
    err_t err = 0;
    // Return early if there are no clusters
    u32 cluster = dir_first_cluster;
    if (cluster >= FAT_EOF_MIN)
        return ERR_DOES_NOT_EXIST;
    DirEntry *cluster_entries = malloc(cluster_size);
    if (cluster_entries == 0)
        return ERR_NO_MEMORY;
    bool reading_long_name = false;
    u8 next_long_name_ord;
    u8 long_name_checksum;
    size_t name_buf_length;
    // Go through each entry in the directory
    for (u32 entry_i = 0;; entry_i++) {
        // If we are past the last entry in a cluster, move to the next one
        if (entry_i >= cluster_size / sizeof(DirEntry)) {
            err = fat_read_entry_expect_allocated_or_eof(cluster, &cluster);
            if (err) {
                if (err == ERR_EOF)
                    err = ERR_DOES_NOT_EXIST;
                goto exit;
            }
            entry_i = 0;
        }
        // If this is the first entry in a cluster, load it
        if (entry_i == 0) {
            err = drive_read(fat_cluster_offset(cluster), cluster_size, cluster_entries);
            if (err)
                goto exit;
        }
        DirEntry *entry = &cluster_entries[entry_i];
        // Check if the entry is a long name entry
        if ((entry->attr & LONG_NAME_ATTR_MASK) == LONG_NAME_ATTR) {
            LongNameDirEntry *lne = (LongNameDirEntry *)entry;
            // Don't recognize the long name entry if type is not 0
            if (lne->type != 0) {
                reading_long_name = false;
                continue;
            }
            if ((lne->ord & LONG_NAME_ORD_LAST) && (lne->ord & LONG_NAME_ORD_MASK) != 0) {
                // If the long name entry is marked as the last one, copy its contents into the name buffer
                // and expect next entry to continue it
                err = copy_name_from_long_name_entry(lne, name_buf, &name_buf_length);
                if (!err) {
                    reading_long_name = true;
                    next_long_name_ord = (lne->ord & LONG_NAME_ORD_MASK) - 1;
                    long_name_checksum = lne->checksum;
                }
            } else if (!(lne->ord & LONG_NAME_ORD_LAST) && reading_long_name && (lne->ord & LONG_NAME_ORD_MASK) == next_long_name_ord && lne->checksum == long_name_checksum) {
                // If we're already reading a long name and the next one continues the sequence,
                // copy its contents into the buffer
                err = copy_name_from_long_name_entry(lne, name_buf, NULL);
                if (err)
                    reading_long_name = false;
                else
                    next_long_name_ord -= 1;
            } else {
                // If the long name entry does not start or continue a sequence, stop the sequence if there was one
                reading_long_name = false;
            }
        } else {
            // If the entry was preceded by a valid sequence of long name entries then it has a long name
            bool has_long_name = reading_long_name && next_long_name_ord == 0;
            reading_long_name = false;
            if (entry->name[0] == 0xE5 || entry->name[0] == ' ')
                continue;
            if (entry->name[0] == 0x00) {
                err = ERR_DOES_NOT_EXIST;
                goto exit;
            }
            // Verify checksum and ignore long name entry if it's not correct
            if (has_long_name) {
                u8 checksum = 0;
                for (int i = 0; i < 11; i++)
                    checksum = ((checksum << 7) | (checksum >> 1)) + entry->name[i];
                if (long_name_checksum != checksum)
                    has_long_name = false;
            }
            // If there is no long name, get the short name from the entry
            if (!has_long_name) {
                // Read the main part of the name
                int main_name_chars = 8;
                while (main_name_chars > 0 && entry->name[main_name_chars - 1] == ' ')
                    main_name_chars--;
                for (int i = 0; i < main_name_chars; i++) {
                    if (!char_allowed_in_short_name(entry->name[i]))
                        goto skip_entry;
                    name_buf[i] = entry->name[i];
                }
                // Read the extension
                int extension_chars = 3;
                while (extension_chars > 0 && entry->name[7 + extension_chars] == ' ')
                    extension_chars--;
                if (extension_chars > 0)
                    name_buf[main_name_chars] = '.';
                for (int i = 0; i < extension_chars; i++) {
                    if (!char_allowed_in_short_name(entry->name[8 + i]))
                        goto skip_entry;
                    name_buf[main_name_chars + 1 + i] = entry->name[8 + i];
                }
                name_buf_length = main_name_chars + extension_chars + (extension_chars > 0);
            }
            // If the name is equal to the one requested, return the entry
            if (target_name_length == name_buf_length && memcmp(target_name, name_buf, target_name_length) == 0) {
                *entry_ptr = *entry;
                err = 0;
                goto exit;
            }
        }
skip_entry:
        ;
    }
exit:
    free(cluster_entries);
    return err;
}

// Get a directory entry for a file given its path
static err_t entry_from_path(const u8 *path, size_t path_length, DirEntry *entry_ptr) {
    err_t err;
    // Start at root directory
    DirEntry entry = root_dir_entry;
    size_t name_start = 0;
    while (1) {
        // Verify current entry is a folder
        if (!(entry.attr & DIR_ENTRY_ATTR_DIRECTORY))
            return ERR_DOES_NOT_EXIST;
        // Next name goes until next separator or end of path
        void *next_separator = memchr(path + name_start, '/', path_length - name_start);
        size_t name_end = next_separator ? (const u8 *)next_separator - path : path_length;
        // Get entry for the name
        err = find_entry_in_dir(entry_first_cluster(&entry), path + name_start, name_end - name_start, &entry);
        if (err)
            return err;
        // Exit if we're at the end of the path, otherwise start the next component after the separator
        if (name_end >= path_length)
            break;
        name_start = name_end + 1;
    };
    *entry_ptr = entry;
    return 0;
}

// Convert timestamp from format used in directory entries to system format
static i64 timestamp_from_fat_format(u16 date, u16 time, u8 time_tens) {
    struct tm tm = (struct tm){
        .tm_sec = (time & 0x1F) * 2,
        .tm_min = (time >> 5) & 0x3F,
        .tm_hour = time >> 11,
        .tm_mday = date & 0x1F,
        .tm_mon = ((date >> 5) & 0x0F) - 1,
        .tm_year = (date >> 9) + 80,
        .tm_isdst = -1,
    };
    time_t t = (i64)mktime_gmt(&tm) * TICKS_PER_SEC + time_tens * (TICKS_PER_SEC / 100);
    return t;
}

// Get FileMetadata struct for a directory entry
static err_t stat_from_entry(const DirEntry *entry, FileMetadata *stat) {
    memset(stat, 0, sizeof(FileMetadata));
    stat->size = entry->file_size;
    stat->create_time = timestamp_from_fat_format(entry->creation_date, entry->creation_time, entry->creation_time_tens);
    stat->modify_time = timestamp_from_fat_format(entry->write_date, entry->write_time, 0);
    stat->access_time = timestamp_from_fat_format(entry->access_date, 0, 0);
    return 0;
}

static BPB bpb_buf;

void main(void) {
    err_t err;
    err = resource_get(&resource_name("virt_drive/read"), RESOURCE_TYPE_CHANNEL_SEND, &drive_channel);
    if (err)
        return;
    handle_t drive_info_msg;
    err = resource_get(&resource_name("virt_drive/info"), RESOURCE_TYPE_MESSAGE, &drive_info_msg);
    if (err)
        return;
    VirtDriveInfo drive_info;
    err = message_read(drive_info_msg, &(ReceiveMessage){sizeof(VirtDriveInfo), &drive_info, 0, NULL}, NULL, NULL, 0, FLAG_FREE_MESSAGE);
    if (err)
        return;
    err = drive_read(0, sizeof(BPB), &bpb_buf);
    if (err)
        return;
    err = parse_bpb(&bpb_buf, drive_info.size);
    if (err)
        return;
    handle_t mqueue;
    err = mqueue_create(&mqueue);
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/stat_r"), (MessageTag){0, 0});
    if (err)
        return;
    while (1) {
        handle_t msg;
        mqueue_receive(mqueue, NULL, &msg, TIMEOUT_NONE, 0);
        MessageLength msg_length;
        message_get_length(msg, &msg_length);
        u8 *path = malloc(msg_length.data);
        if (msg_length.data != 0 && path == NULL) {
            err = ERR_NO_MEMORY;
            goto loop_fail;
        }
        err = message_read(msg, &(ReceiveMessage){msg_length.data, path, 0, NULL}, NULL, NULL, 0, 0);
        if (err)
            goto loop_fail;
        DirEntry entry;
        err = entry_from_path(path, msg_length.data, &entry);
        if (err)
            goto loop_fail;
        FileMetadata stat;
        stat_from_entry(&entry, &stat);
        message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(FileMetadata), &stat}, 0, NULL}, FLAG_FREE_MESSAGE);
        continue;
loop_fail:
        message_reply_error(msg, user_error_code(err), FLAG_FREE_MESSAGE);
    }
}
