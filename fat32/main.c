#include <zr/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zr/drive.h>
#include <zr/error.h>
#include <zr/syscalls.h>
#include <zr/time.h>

#define FAT_FREE UINT32_C(0)
#define FAT_BAD_CLUSTER UINT32_C(0x0FFFFFF7)
#define FAT_EOF_MIN UINT32_C(0x0FFFFFF8)
#define FAT_EOF UINT32_C(0x0FFFFFFF)
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

#define NAME_0_FREE_ENTRY 0xE5
#define NAME_0_END_OF_DIR 0x00

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
static u32 entry_get_first_cluster(const DirEntry *entry) {
    return ((u32)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

// Set number of first cluster in directory entry
static void entry_set_first_cluster(DirEntry *entry, u32 first_cluster) {
    entry->first_cluster_high = first_cluster >> 16;
    entry->first_cluster_low = (u16)first_cluster;
}

static handle_t drive_read_channel;
static handle_t drive_write_channel;

// Read data from drive
static err_t drive_read(u64 offset, u64 length, void *dest) {
    return channel_call_read(
        drive_read_channel,
        &(SendMessage){1, &(SendMessageData){sizeof(FileRange), &(FileRange){offset, length}}, 0, NULL},
        &(ReceiveMessage){length, dest, 0, NULL},
        NULL);
}

// Write data to drive
static err_t drive_write(u64 offset, u64 length, const void *src) {
    return channel_call(
        drive_write_channel,
        &(SendMessage){2, (SendMessageData[]){{sizeof(u64), &offset}, {length, src}}, 0, NULL},
        NULL);
}

static u64 fat_offset;
static u64 data_offset;
static u32 fat_length;
static u32 root_cluster;
static u32 cluster_size;

// Blank cluster to be used for write messages to clear regions of memory
static u8* blank_cluster;

// The maximum number of directory entries a file can occupy
// Since the maximum long name length is 255 and each long name entry holds 13 characters,
// at most 20 long name entries can be needed, plus one short name entry.
#define MAX_FILE_DIR_ENTRY_COUNT 21

// Array of empty directory entries used for deleting files
static DirEntry empty_dir_entries[MAX_FILE_DIR_ENTRY_COUNT];

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
    if ((u64)bpb->fat_sector_num_32 * (bpb->bytes_per_sector / sizeof(u32)) * bpb->sectors_per_cluster < data_sector_num)
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
    blank_cluster = malloc(cluster_size);
    if (blank_cluster == NULL)
        return ERR_NO_MEMORY;
    memset(blank_cluster, 0, cluster_size);
    for (int i = 0; i < MAX_FILE_DIR_ENTRY_COUNT; i++)
        empty_dir_entries[i].name[0] = NAME_0_FREE_ENTRY;
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

// Same as fat_read_entry(), but returns an ERR_IO_INTERNAL if the cluster is not allocated.
static err_t fat_read_entry_expect_allocated(u32 cluster, u32 *entry_ptr) {
    err_t err;
    u32 entry;
    err = fat_read_entry(cluster, &entry);
    if (err)
        return err;
    if (entry == FAT_BAD_CLUSTER)
        return ERR_IO_INTERNAL;
    else if (entry >= FAT_EOF_MIN)
        return ERR_IO_INTERNAL;
    else if (entry >= fat_length || entry < 2)
        return ERR_IO_INTERNAL;
    *entry_ptr = entry;
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

// Modify an entry for a given cluster in the FAT
static err_t fat_write_entry(u32 cluster, u32 entry) {
    return drive_write(fat_offset + sizeof(u32) * cluster, sizeof(u32), &entry);
}

// Get the offset of a cluster corresponding to the given FAT entry number
static u64 fat_cluster_offset(u32 cluster) {
    return data_offset + (u64)(cluster - 2) * cluster_size;
}

// Read or write data from the drive
static err_t drive_read_write(u64 offset, u64 length, void *data, bool write) {
    return write ? drive_write(offset, length, data) : drive_read(offset, length, data);
}

// Read or write a range of bytes from a file
static err_t read_write_file(u32 first_cluster, u64 offset, u64 length, void *data, bool write) {
    err_t err;
    u32 cluster = first_cluster;
    u32 src_offset = 0;
    // Seek until first cluster within requested range
    while (src_offset + cluster_size <= offset) {
        err = fat_read_entry_expect_allocated(cluster, &cluster);
        if (err)
            return err;
        src_offset += cluster_size;
    }
    // If this is the only cluster, return now
    if (offset + length <= src_offset + cluster_size)
        return drive_read_write(fat_cluster_offset(cluster) + offset - src_offset, length, data, write);
    // Read or write data from first cluster
    err = drive_read_write(fat_cluster_offset(cluster) + offset - src_offset, cluster_size - (offset - src_offset), data, write);
    if (err)
        return err;
    // Read or write data from clusters in the middle
    u32 dest_offset = cluster_size - (offset - src_offset);
    src_offset += cluster_size;
    err = fat_read_entry_expect_allocated(cluster, &cluster);
    if (err)
        return err;
    while (src_offset + cluster_size < offset + length) {
        err = drive_read_write(fat_cluster_offset(cluster), cluster_size, data + dest_offset, write);
        if (err)
            return err;
        err = fat_read_entry_expect_allocated(cluster, &cluster);
        if (err)
            return err;
        src_offset += cluster_size;
        dest_offset += cluster_size;
    }
    // Read or write data from final cluster
    return drive_read_write(fat_cluster_offset(cluster), length - dest_offset, data + dest_offset, write);
}

// Read a range of bytes from a file
static err_t read_file(u32 first_cluster, u64 offset, u64 length, const void *data) {
    return read_write_file(first_cluster, offset, length, (void *)data, false);
}

// Write a range of bytes to a file
static err_t write_file(u32 first_cluster, u64 offset, u64 length, void *data) {
    return read_write_file(first_cluster, offset, length, data, true);
}

// Free a chain of clusters
static err_t free_clusters(u32 first_cluster) {
    err_t err;
    u32 cluster = first_cluster;
    while (1) {
        u32 next_cluster;
        err = fat_read_entry_expect_allocated_or_eof(cluster, &next_cluster);
        if (err == ERR_EOF)
            return 0;
        else if (err)
            return err;
        err = fat_write_entry(cluster, FAT_FREE);
        if (err)
            return err;
        cluster = next_cluster;
    }
}

#define FAT_BUFFER_LENGTH 1024

// Buffer used to load parts of the FAT when searching for free clusters
static u32 fat_buffer[FAT_BUFFER_LENGTH];

// Allocate a chain containing the given number of clusters and return the address of the first one
// If `clear` is set, the clusters' contents will be zeroed out.
static err_t allocate_clusters(u32 target_count, u32 *first_cluster_ptr, bool clear) {
    err_t err;
    u32 current_count = 0;
    u32 first_cluster;
    u32 last_cluster;
    // Load FAT buffer with first block of FAT entries
    err = drive_read(fat_offset, sizeof(fat_buffer), fat_buffer);
    if (err)
        return err;
    // Go through clusters on drive in order
    for (u32 cluster = 2; cluster < fat_length; cluster++) {
        // Refresh FAT buffer if we've reached its end
        if (cluster % FAT_BUFFER_LENGTH == 0) {
            err = drive_read(fat_offset + sizeof(u32) * cluster, sizeof(fat_buffer), fat_buffer);
            if (err)
                return err;
        }
        u32 entry = fat_buffer[cluster % FAT_BUFFER_LENGTH] & FAT_ENTRY_MASK;
        // If cluster is free, attach it to the chain
        if (entry == FAT_FREE) {
            if (current_count == 0) {
                first_cluster = cluster;
                last_cluster = cluster;
            } else {
                err = fat_write_entry(last_cluster, cluster);
                if (err)
                    return err;
                last_cluster = cluster;
            }
            // Zero out cluster
            if (clear) {
                err = drive_write(fat_cluster_offset(cluster), cluster_size, blank_cluster);
                if (err)
                    return err;
            }
            // If we have reached the target number of clusters, return
            current_count++;
            if (current_count >= target_count) {
                err = fat_write_entry(last_cluster, FAT_EOF);
                if (err)
                    return err;
                *first_cluster_ptr = first_cluster;
                return 0;
            }
        }
    }
    // If there are not enough free clusters, free the allocated chain
    err = fat_write_entry(last_cluster, FAT_EOF);
    if (err)
        return err;
    err = free_clusters(first_cluster);
    if (err)
        return err;
    return ERR_NO_SPACE;
}

// Resize a file to a given size
// If `clear` is set, the data added at the end will be zeroed out.
static err_t resize_file(DirEntry *entry, u64 entry_offset, u32 new_size, bool clear) {
    err_t err;
    u32 first_cluster = entry_get_first_cluster(entry);
    u32 old_size = entry->file_size;
    u32 new_cluster_count = (new_size + cluster_size - 1) & (cluster_size - 1);
    entry->file_size = new_size;
    // If resizing already empty file to zero, nothing needs to be done
    if (new_size == 0 && first_cluster == 0)
        goto end;
    // If shrinking a nonempty file to zero, free all the clusters and set first cluster to 0
    if (new_size == 0) {
        err = free_clusters(first_cluster);
        if (err)
            return err;
        entry_set_first_cluster(entry, 0);
        goto end;
    }
    // If expanding empty file, create new chain and set first cluster to its start
    if (first_cluster == 0) {
        u32 new_first_cluster;
        err = allocate_clusters(new_cluster_count, &new_first_cluster, clear);
        if (err)
            return err;
        entry_set_first_cluster(entry, new_first_cluster);
        goto end;
    }
    u32 cluster = first_cluster;
    for (u32 i = 0; ; i++) {
        u32 next_cluster;
        // Clear part of cluster after the old end of the file
        if (clear) {
            if (i * cluster_size > old_size) {
                drive_write(fat_cluster_offset(cluster), cluster_size, blank_cluster);
            } else if ((i + 1) * cluster_size > old_size) {
                u64 bytes_to_clear = (i + 1) * cluster_size - old_size;
                drive_write(fat_cluster_offset(cluster) + (cluster_size - bytes_to_clear), bytes_to_clear, blank_cluster);
            }
        }
        // Get next cluster
        err = fat_read_entry_expect_allocated_or_eof(cluster, &next_cluster);
        // If this is the final cluster, allocate the remaining clusters and attach at the end
        if (err == ERR_EOF) {
            if (i == new_cluster_count - 1)
                goto end;
            err = allocate_clusters(new_cluster_count - i - 1, &next_cluster, clear);
            if (err)
                return err;
            err = fat_write_entry(cluster, next_cluster);
            if (err)
                return err;
            goto end;
        } else if (err) {
            return err;
        }
        // If there are more clusters than we need, mark new final cluster as end-of-file and free the remaining clusters
        if (i == new_cluster_count - 1) {
            err = fat_write_entry(cluster, FAT_EOF);
            if (err)
                return err;
            err = free_clusters(next_cluster);
            if (err)
                return err;
            goto end;
        }
        // Move to next cluster
        cluster = next_cluster;
    }
end:
    // Write back new directory entry
    return drive_write(entry_offset, sizeof(DirEntry), entry);
}

// Table of characters allowed in short and long file names - bit is set if character allowed
static u8 short_name_allowed_char_table[16] = {0x00, 0x00, 0x00, 0x00, 0xFA, 0x23, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xC7, 0x01, 0x00, 0x00, 0x68};
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
static err_t copy_name_from_long_name_entry(LongNameDirEntry *lne, u8 *buf, u32 *buf_length) {
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

// Convert short name from directory entry format to string
static err_t convert_from_short_name(const u8 *entry_name, u8 *name_buf, u32 *name_length_ptr) {
    // Read the main part of the short name
    int main_name_chars = 8;
    while (main_name_chars > 0 && entry_name[main_name_chars - 1] == ' ')
        main_name_chars--;
    for (int i = 0; i < main_name_chars; i++) {
        if (!char_allowed_in_short_name(entry_name[i]))
            return ERR_OTHER;
        name_buf[i] = entry_name[i];
    }
    // Read the extension of the short name
    int extension_chars = 3;
    while (extension_chars > 0 && entry_name[7 + extension_chars] == ' ')
        extension_chars--;
    if (extension_chars > 0)
        name_buf[main_name_chars] = '.';
    for (int i = 0; i < extension_chars; i++) {
        if (!char_allowed_in_short_name(entry_name[8 + i]))
            return ERR_OTHER;
        name_buf[main_name_chars + 1 + i] = entry_name[8 + i];
    }
    *name_length_ptr = main_name_chars + extension_chars + (extension_chars > 0);
    return 0;
}

// Get the checksum of a short name
static u8 get_short_name_checksum(const u8 *name) {
    u8 checksum = 0;
    for (int i = 0; i < 11; i++)
        checksum = ((checksum << 7) | (checksum >> 1)) + name[i];
    return checksum;
}

typedef struct DirReadState {
    u32 cluster;
    u32 entry_i;
    DirEntry *cluster_entries;
} DirReadState;

typedef struct DirEntryLocation {
    u64 main_entry_offset;
    u32 first_entry_cluster;
    u32 first_entry_index;
    u32 entry_count;
} DirEntryLocation;

// Initialize a DirReadState struct
static err_t dir_read_state_init(DirReadState *state, u32 dir_first_cluster) {
    DirEntry *cluster_entries = malloc(cluster_size);
    if (cluster_entries == NULL)
        return ERR_NO_MEMORY;
    *state = (DirReadState){dir_first_cluster, 0, cluster_entries};
    return 0;
}

// Get the entry (not necessary an allocated one) from a directory pointed to by DirReadState
static err_t get_next_dir_entry(DirReadState *state, DirEntry **entry) {
    err_t err;
    // If we are past the last entry in a cluster, move to the next one
    if (state->entry_i >= cluster_size / sizeof(DirEntry)) {
        err = fat_read_entry_expect_allocated_or_eof(state->cluster, &state->cluster);
        if (err == ERR_EOF)
            return ERR_DOES_NOT_EXIST;
        else if (err)
            return err;
        state->entry_i = 0;
    }
    // If this is the first entry in a cluster, load it
    if (state->entry_i == 0) {
        err = drive_read(fat_cluster_offset(state->cluster), cluster_size, state->cluster_entries);
        if (err)
            return err;
    }
    *entry = &state->cluster_entries[state->entry_i];
    return 0;
}

// Get the next entry representing a file in a directory along with its long and short names and location
// If no long name is found, *long_name_length_ptr is set to 0.
// After a successful return, the `state` is updated so that it can be used to get the next entry.
static err_t get_next_full_dir_entry(DirReadState *state, u8 *long_name_buf, u32 *long_name_length_ptr, u8 *short_name_buf, u32 *short_name_length_ptr, DirEntry *entry_ptr, DirEntryLocation *location_ptr) {
    err_t err;
    bool reading_long_name = false;
    u8 next_long_name_ord;
    u8 long_name_checksum;
    u32 long_name_length;
    DirEntryLocation location;
    // Go through each entry in the directory starting from the current one
    while (1) {
        DirEntry *entry;
        err = get_next_dir_entry(state, &entry);
        if (err)
            return err;
        // Check first character of short name for byte marking a free entry or end of directory
        if (entry->name[0] == NAME_0_FREE_ENTRY)
            goto skip_entry;
        if (entry->name[0] == NAME_0_END_OF_DIR)
            return ERR_DOES_NOT_EXIST;
        // Check if the entry is a long name entry
        if ((entry->attr & LONG_NAME_ATTR_MASK) == LONG_NAME_ATTR) {
            LongNameDirEntry *lne = (LongNameDirEntry *)entry;
            // Don't recognize the long name entry if type is not 0
            if (lne->type != 0) {
                reading_long_name = false;
                goto skip_entry;
            }
            if ((lne->ord & LONG_NAME_ORD_LAST) && (lne->ord & LONG_NAME_ORD_MASK) != 0) {
                // If the long name entry is marked as the last one, copy its contents into the name buffer
                // and expect next entry to continue it
                err = copy_name_from_long_name_entry(lne, long_name_buf, &long_name_length);
                if (!err) {
                    reading_long_name = true;
                    next_long_name_ord = (lne->ord & LONG_NAME_ORD_MASK) - 1;
                    long_name_checksum = lne->checksum;
                    location.first_entry_cluster = state->cluster;
                    location.first_entry_index = state->entry_i;
                    location.entry_count = (lne->ord & LONG_NAME_ORD_MASK) + 1;
                }
            } else if (!(lne->ord & LONG_NAME_ORD_LAST) && reading_long_name && (lne->ord & LONG_NAME_ORD_MASK) == next_long_name_ord && next_long_name_ord != 0 && lne->checksum == long_name_checksum) {
                // If we're already reading a long name and the next one continues the sequence,
                // copy its contents into the buffer
                err = copy_name_from_long_name_entry(lne, long_name_buf, NULL);
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
            // If the short name starts with a space then it's invalid
            if (entry->name[0] == ' ')
                goto skip_entry;
            if (has_long_name) {
                // Verify checksum and ignore long name entry if it's not correct
                if (long_name_checksum != get_short_name_checksum(entry->name))
                    has_long_name = false;
                // Ignore long name entry if it has zero length or has leading or trailing spaces or trailing periods
                if (long_name_length == 0 || long_name_buf[0] == ' ' || long_name_buf[long_name_length - 1] == ' ' || long_name_buf[long_name_length - 1] == '.')
                    has_long_name = false;
            }
            err = convert_from_short_name(entry->name, short_name_buf, short_name_length_ptr);
            if (err)
                goto skip_entry;
            // If there is no long name, set entry location to just the short name entry and set long name length to zero
            if (!has_long_name) {
                location.first_entry_cluster = state->cluster;
                location.first_entry_index = state->entry_i;
                location.entry_count = 1;
                long_name_length = 0;
            }
            location.main_entry_offset = fat_cluster_offset(state->cluster) + state->entry_i * sizeof(DirEntry);
            // Return the entry
            *long_name_length_ptr = long_name_length;
            if (entry_ptr != NULL)
                *entry_ptr = *entry;
            if (location_ptr != NULL)
                *location_ptr = location;
            state->entry_i++;
            return 0;
        }
skip_entry:
        state->entry_i++;
    }
}

// Find a free chain of entries of a given length in a directory
// May extend the directory by allocating additional clusters.
static err_t find_free_entry_chain(u32 dir_first_cluster, u32 needed_length, u32 *first_entry_cluster_ptr, u32 *first_entry_index_ptr) {
    err_t err;
    // Initialize directory reader state
    DirReadState state;
    err = dir_read_state_init(&state, dir_first_cluster);
    if (err)
        return err;
    // Go through each entry in the directory
    u32 current_chain_start_cluster;
    u32 current_chain_start_index;
    u32 current_chain_length = 0;
    bool skip = false;
    while (1) {
        DirEntry *entry;
        err = get_next_dir_entry(&state, &entry);
        if (err == ERR_DOES_NOT_EXIST) {
            // If we have not found a chain of sufficient length, extend the directory with new clusters
            u32 cluster_alloc_count = (needed_length - current_chain_length + cluster_size / sizeof(DirEntry) - 1) / (cluster_size / sizeof(DirEntry));
            u32 first_new_cluster;
            err = allocate_clusters(cluster_alloc_count, &first_new_cluster, true);
            if (err)
                goto exit;
            // Return the chain
            if (current_chain_length != 0) {
                *first_entry_cluster_ptr = current_chain_start_cluster;
                *first_entry_index_ptr = current_chain_start_index;
            } else {
                *first_entry_cluster_ptr = first_new_cluster;
                *first_entry_index_ptr = 0;
            }
            goto success;
        } else if (err) {
            goto exit;
        }
        // Check if entry is free
        if (skip || entry->name[0] == NAME_0_FREE_ENTRY || entry->name[0] == NAME_0_END_OF_DIR) {
            // If entry marked as end of directory, treat all further entries as free as well
            if (entry->name[0] == NAME_0_END_OF_DIR)
                skip = true;
            // Initialize chain if this is the start
            if (current_chain_length == 0) {
                current_chain_start_cluster = state.cluster;
                current_chain_start_index = state.entry_i;
            }
            // Increment chain length
            current_chain_length++;
            // If we've found a chain of sufficient length, return it
            if (current_chain_length >= needed_length) {
                *first_entry_cluster_ptr = current_chain_start_cluster;
                *first_entry_index_ptr = current_chain_start_index;
                goto success;
            }
        } else {
            // Reset chain
            current_chain_length = 0;
        }
        state.entry_i++;
    }
success:
    // If one of the entries was marked as end of directory, we need to put the marking back after the allocated space
    if (skip) {
        if (state.entry_i >= cluster_size / sizeof(DirEntry)) {
            err = fat_read_entry_expect_allocated_or_eof(state.cluster, &state.cluster);
            if (err) {
                if (err == ERR_EOF)
                    err = 0;
                goto exit;
            }
            state.entry_i = 0;
        }
        err = drive_write(fat_cluster_offset(state.cluster) + state.entry_i * sizeof(DirEntry), 1, &(u8){NAME_0_END_OF_DIR});
        if (err)
            return err;
    }
    err = 0;
exit:
    free(state.cluster_entries);
    return err;
}

static u8 long_name_buf[255];
static u8 short_name_buf[12];

// Compare two strings ignoring case
static bool equal_case_insensitive(const u8 *s1, size_t n1, const u8 *s2, size_t n2) {
    if (n1 != n2)
        return false;
    for (size_t i = 0; i < n1; i++) {
        u8 c1 = s1[i];
        if ('a' <= c1 && c1 <= 'z')
            c1 -= 'a' - 'A';
        u8 c2 = s2[i];
        if ('a' <= c2 && c2 <= 'z')
            c2 -= 'a' - 'A';
        if (c1 != c2)
            return false;
    }
    return true;
}

// Strip leading and trailing spaces and trailing periods from filename
static void strip_filename(const u8 **name_ptr, size_t *name_length_ptr) {
    const u8 *name = *name_ptr;
    size_t name_length = *name_length_ptr;
    // Strip leading spaces
    while (name_length != 0 && name[0] == ' ') {
        name++;
        name_length--;
    }
    // Strip trailing spaces and periods
    while (name_length != 0 && (name[name_length - 1] == ' ' || name[name_length - 1] == '.'))
        name_length--;
    *name_ptr = name;
    *name_length_ptr = name_length;
}

// Get a directory entry for a file, given the first cluster of its containing folder and its name
static err_t find_entry_in_dir(u32 dir_first_cluster, const u8 *target_name, size_t target_name_length, DirEntry *entry_ptr, DirEntryLocation *location_ptr) {
    err_t err = 0;
    strip_filename(&target_name, &target_name_length);
    // Initialize directory reader state
    DirReadState state;
    err = dir_read_state_init(&state, dir_first_cluster);
    if (err)
        return err;
    while (1) {
        DirEntry entry;
        DirEntryLocation location;
        u32 long_name_length;
        u32 short_name_buf_length;
        // Get the next directory entry
        err = get_next_full_dir_entry(&state, long_name_buf, &long_name_length, short_name_buf, &short_name_buf_length, &entry, &location);
        if (err)
            goto exit;
        // If long or short name is equal to the one requested, return the entry
        if ((long_name_length != 0 && equal_case_insensitive(target_name, target_name_length, long_name_buf, long_name_length))
                || equal_case_insensitive(target_name, target_name_length, short_name_buf, short_name_buf_length)) {
            if (entry_ptr != NULL)
                *entry_ptr = entry;
            if (location_ptr != NULL)
                *location_ptr = location;
            err = 0;
            goto exit;
        }
    }
exit:
    free(state.cluster_entries);
    return err;
}

// Tells how much information was lost when converting a long name to a short name by convert_to_short_name()
typedef enum ShortNameConvLoss {
    // The short name had to be truncated or have some characters replaced or removed.
    // It should be mangled and must be stored together with long name entries.
    SHORT_NAME_CONV_LOSSY,
    // The short name is the same as the long name when ignoring case.
    // The name must not be mangled, but will need to be stored together with long name entries.
    SHORT_NAME_CONV_RECASED,
    // The short name is exactly the same as the long name. No long name entries are needed.
    SHORT_NAME_CONV_EXACT,
} ShortNameConvLoss;

// Convert a long name to a short name in the directory entry format (11 characters, implicit period, padded with spaces)
// Assumes a long name already has trailing periods and leading and trailing spaces removed.
// Return value informs how much information was lost in the conversion.
static ShortNameConvLoss convert_to_short_name(const u8 *long_name, size_t long_name_length, u8 *short_name) {
    bool lossy = false;
    bool recased = false;
    // Remove leading periods to avoid what's after them being interpreted as extension
    while (long_name_length > 0 && long_name[0] == '.') {
        lossy = true;
        long_name++;
        long_name_length--;
    }
    // Locate last period in name - if not present, set position to past the end of name
    size_t last_period_pos = long_name_length;
    for (size_t i = long_name_length; i-- > 0;) {
        if (long_name[i] == '.') {
            last_period_pos = i;
            break;
        }
    }
    // Copy characters from long name to short name
    size_t short_name_i = 0;
    for (size_t i = 0; i < long_name_length; i++) {
        // After reaching the last period, pad the rest of main part of name with spaces
        if (i == last_period_pos) {
            for (; short_name_i < 8; short_name_i++)
                short_name[short_name_i] = ' ';
            continue;
        }
        // Don't copy any more characters if we're at the end
        if ((i < last_period_pos && short_name_i >= 8) || short_name_i >= 11) {
            lossy = true;
            continue;
        }
        // Skip periods and spaces
        if (long_name[i] == '.' || long_name[i] == ' ') {
            lossy = true;
            continue;
        }
        // Copy uppercased character to short name, replacing it with an underscore if it's not allowed there
        if ('a' <= long_name[i] && long_name[i] <= 'z') {
            recased = true;
            short_name[short_name_i] = long_name[i] - ('a' - 'A');
        } else if (char_allowed_in_short_name(long_name[i])) {
            short_name[short_name_i] = long_name[i];
        } else {
            lossy = true;
            short_name[short_name_i] = '_';
        }
        short_name_i++;
    }
    // Pad the rest of the name with spaces
    for (; short_name_i < 11; short_name_i++)
        short_name[short_name_i] = ' ';
    return lossy ? SHORT_NAME_CONV_LOSSY : recased ? SHORT_NAME_CONV_RECASED : SHORT_NAME_CONV_EXACT;
}

// Create an empty file or directory with a given name in a directory
// The metadata and first cluster will be taken from the provided entry.
static err_t create_dir_entry(u32 parent_first_cluster, const u8 *name, size_t name_length, DirEntry *entry, u64 src_entry_offset) {
    err_t err;
    strip_filename(&name, &name_length);
    // Fail if a different file with this name already exists
    DirEntryLocation found_entry_location;
    err = find_entry_in_dir(parent_first_cluster, name, name_length, NULL, &found_entry_location);
    if (err == 0 && found_entry_location.main_entry_offset != src_entry_offset)
        return ERR_FILE_EXISTS;
    if (err != 0 && err != ERR_DOES_NOT_EXIST)
        return err;
    // Check if name is a valid long file name
    if (name_length > 255)
        return ERR_FILENAME_INVALID;
    for (size_t i = 0; i < name_length; i++)
        if (!char_allowed_in_long_name(name[i]))
            return ERR_FILENAME_INVALID;
    u8 entry_short_name[11];
    u8 string_short_name[12];
    // Convert the name to a short name
    ShortNameConvLoss loss = convert_to_short_name(name, name_length, entry_short_name);
    // If the conversion was lossy, mangle the short name
    if (loss == SHORT_NAME_CONV_LOSSY) {
        // Get length of main part of short name
        size_t main_name_length = 8;
        while (main_name_length > 0 && entry_short_name[main_name_length - 1] == ' ')
            main_name_length--;
        // Try attaching a numeric tail ranging from ~1 to ~99999
        for (size_t digit_count = 1, range_start = 1; digit_count < 5; digit_count++, range_start *= 10) {
            // Get position to start placing tail at
            size_t tail_start_pos = main_name_length < 7 - digit_count ? main_name_length : 7 - digit_count;
            // Place tilde
            entry_short_name[tail_start_pos] = '~';
            for (size_t n = range_start; n < 10 * range_start; n++) {
                // Set numeric part of tail
                for (size_t i = 0, m = n; i < digit_count; i++, m /= 10)
                    entry_short_name[tail_start_pos + digit_count - i] = m % 10 + '0';
                u32 short_name_length;
                convert_from_short_name(entry_short_name, string_short_name, &short_name_length);
                err = find_entry_in_dir(parent_first_cluster, string_short_name, short_name_length, NULL, NULL);
                if (err == ERR_DOES_NOT_EXIST)
                    goto numeric_tail_found;
                else if (err)
                    return err;
            }
        }
        return ERR_IO_INTERNAL;
numeric_tail_found:
        ;
    }
    // Allocate the necessary number of entries
    u32 num_long_name_entries = loss == SHORT_NAME_CONV_EXACT ? 0 : (name_length + 12) / 13;
    u32 cluster;
    u32 index;
    err = find_free_entry_chain(parent_first_cluster, num_long_name_entries + 1, &cluster, &index);
    if (err)
        return err;
    // Write the entries to disk
    LongNameDirEntry long_name_entry;
    long_name_entry.attr = LONG_NAME_ATTR;
    long_name_entry.type = 0;
    long_name_entry.checksum = get_short_name_checksum(entry_short_name);
    long_name_entry.reserved1 = 0;
    // Write the long name entries
    for (u32 ord = num_long_name_entries; ord > 0; ord--) {
        size_t lne_base = 13 * (ord - 1);
        // Fill the long name entry
        long_name_entry.ord = ord | (ord == num_long_name_entries ? LONG_NAME_ORD_LAST : 0);
        for (int lne_offset = 0; lne_offset < 13; lne_offset++) {
            u16 c;
            if (lne_base + lne_offset < name_length)
                c = name[lne_base + lne_offset];
            else if (lne_base + lne_offset == name_length)
                c = 0;
            else
                c = 0xFFFF;
            if (lne_offset < 5)
                long_name_entry.name1[lne_offset] = c;
            else if (lne_offset < 11)
                long_name_entry.name2[lne_offset - 5] = c;
            else
                long_name_entry.name3[lne_offset - 11] = c;
        }
        // Write the entry to disk
        drive_write(fat_cluster_offset(cluster) + index * sizeof(DirEntry), sizeof(DirEntry), &long_name_entry);
        // Move to the next entry
        index++;
        if (index >= cluster_size / sizeof(DirEntry)) {
            err = fat_read_entry_expect_allocated(cluster, &cluster);
            if (err)
                return err;
            index = 0;
        }
    }
    // Fill the name of the short name entry
    memcpy(entry->name, entry_short_name, 11);
    entry->reserved1 = 0;
    // Write the entry to disk
    return drive_write(fat_cluster_offset(cluster) + index * sizeof(DirEntry), sizeof(DirEntry), entry);
}

// Allocate the initial first cluster for a directory along with . and .. entries
static err_t allocate_first_dir_cluster(u32 *dir_first_cluster_ptr, u32 parent_first_cluster) {
    err_t err;
    // Allocate first cluster
    u32 dir_first_cluster;
    err = allocate_clusters(1, &dir_first_cluster, true);
    if (err)
        return err;
    // Insert . and .. entries at the beginning
    DirEntry entries[2];
    memset(&entries[0], 0, sizeof(DirEntry));
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = DIR_ENTRY_ATTR_DIRECTORY;
    memcpy(&entries[1], &entries[0], sizeof(DirEntry));
    entries[1].name[1] = '.';
    entry_set_first_cluster(&entries[0], dir_first_cluster);
    entry_set_first_cluster(&entries[1], parent_first_cluster == root_cluster ? 0 : parent_first_cluster);
    err = drive_write(fat_cluster_offset(dir_first_cluster), 2 * sizeof(DirEntry), entries);
    if (err)
        return err;
    *dir_first_cluster_ptr = dir_first_cluster;
    return 0;
}

#define DIR_LIST_INIT_CAPACITY 64

// Get list of files contained in a directory starting at a given cluster
static err_t get_dir_list(u32 dir_first_cluster, u8 **list_ptr, size_t *len_ptr) {
    err_t err = 0;
    // Allocate list
    size_t list_len = 0;
    size_t list_capacity = DIR_LIST_INIT_CAPACITY;
    u8 *list = malloc(list_capacity);
    if (list == NULL)
        return ERR_NO_MEMORY;
    // Initialize directory reader state
    DirReadState state;
    err = dir_read_state_init(&state, dir_first_cluster);
    if (err) {
        free(list);
        return err;
    }
    while (1) {
        DirEntry entry;
        u32 long_name_length;
        u32 short_name_length;
        // Get the next directory entry
        err = get_next_full_dir_entry(&state, long_name_buf, &long_name_length, short_name_buf, &short_name_length, &entry, NULL);
        if (err) {
            if (err == ERR_DOES_NOT_EXIST) {
                *list_ptr = list;
                *len_ptr = list_len;
                err = 0;
            }
            goto exit;
        }
        // Use long name if one exists, short name otherwise
        u32 name_buf_length = long_name_length != 0 ? long_name_length : short_name_length;
        u8 *name_buf = long_name_length != 0 ? long_name_buf : short_name_buf;
        // Expand list if necessary
        if (list_capacity - list_len < name_buf_length + sizeof(u32)) {
            while (list_capacity - list_len < name_buf_length + sizeof(u32))
                list_capacity *= 2;
            list = realloc(list, list_capacity);
            if (list == NULL) {
                err = ERR_NO_MEMORY;
                goto exit;
            }
        }
        // Append entry to list
        *(u32 *)(list + list_len) = name_buf_length;
        list_len += sizeof(u32);
        memcpy(list + list_len, name_buf, name_buf_length);
        list_len += name_buf_length;
    }
exit:
    free(state.cluster_entries);
    if (err)
        free(list);
    return err;
}

// Delete a file entry at a given location, replacing its entries with unallocated ones
static err_t delete_file_entry(const DirEntryLocation *location) {
    err_t err;
    u32 cluster = location->first_entry_cluster;
    // If all entries fit in one cluster clear them with one call
    if (location->first_entry_index + location->entry_count <= cluster_size / sizeof(DirEntry))
        return drive_write(fat_cluster_offset(cluster) + location->first_entry_index * sizeof(DirEntry), location->entry_count * sizeof(DirEntry), empty_dir_entries);
    // Clear entries from first cluster
    err = drive_write(fat_cluster_offset(cluster) + location->first_entry_index * sizeof(DirEntry), cluster_size - location->first_entry_index * sizeof(DirEntry), empty_dir_entries);
    if (err)
        return err;
    // Clear remaining clusters
    u32 entries_cleared = cluster_size / sizeof(DirEntry) - location->first_entry_index;
    while (1) {
        err = fat_read_entry_expect_allocated(cluster, &cluster);
        if (err)
            return err;
        // If this is the final cluster, clear its beginning and return
        if (entries_cleared + cluster_size / sizeof(DirEntry) >= location->entry_count)
            return drive_write(fat_cluster_offset(cluster), (location->entry_count - entries_cleared) * sizeof(DirEntry), empty_dir_entries);
        // Otherwise, clear entire cluster
        err = drive_write(fat_cluster_offset(cluster), cluster_size, empty_dir_entries);
        if (err)
            return err;
        entries_cleared += cluster_size / sizeof(DirEntry);
    }
}

// Get a directory entry for a file given its path
// If the requested directory is the root directory, a dummy entry will be returned.
// If any directory along the path is the blocked directory, ERR_MOVE_INTO_ITSELF is returned.
// This property is used to block an operation that would move a directory into itself.
static err_t entry_from_path(const u8 *path, size_t path_length, DirEntry *entry_ptr, DirEntryLocation *location_ptr, u32 blocked_directory) {
    // Return root directory if string is empty
    if (path_length == 0) {
        *entry_ptr = root_dir_entry;
        if (location_ptr != NULL)
            *location_ptr = (DirEntryLocation){
                .main_entry_offset = UINT64_MAX,
                .first_entry_cluster = UINT32_MAX,
                .first_entry_index = 0,
                .entry_count = 0,
            };
        return 0;
    }
    err_t err;
    // Start at root directory
    DirEntry entry = root_dir_entry;
    DirEntryLocation location;
    size_t name_start = 0;
    while (1) {
        // Verify current entry is a folder
        if (!(entry.attr & DIR_ENTRY_ATTR_DIRECTORY))
            return ERR_DOES_NOT_EXIST;
        // Next name goes until next separator or end of path
        void *next_separator = memchr(path + name_start, '/', path_length - name_start);
        size_t name_end = next_separator ? (const u8 *)next_separator - path : path_length;
        // Get entry for the name
        err = find_entry_in_dir(entry_get_first_cluster(&entry), path + name_start, name_end - name_start, &entry, &location);
        if (err)
            return err;
        if ((entry.attr & DIR_ENTRY_ATTR_DIRECTORY) && entry_get_first_cluster(&entry) == blocked_directory)
            return ERR_MOVE_INTO_ITSELF;
        // Exit if we're at the end of the path, otherwise start the next component after the separator
        if (name_end >= path_length)
            break;
        name_start = name_end + 1;
    };
    if (entry_ptr != NULL)
        *entry_ptr = entry;
    if (location_ptr != NULL)
        *location_ptr = location;
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

typedef enum RequestTag {
    TAG_STAT,
    TAG_LIST,
    TAG_DELETE,
    TAG_CREATE,
    TAG_MOVE,
    TAG_OPEN,
    TAG_READ,
    TAG_WRITE,
    TAG_RESIZE,
} RequestTag;

static err_t get_message_data(handle_t msg, u8 **data_ptr, size_t *path_length_ptr) {
    err_t err;
    MessageLength msg_length;
    message_get_length(msg, &msg_length);
    u8 *data = malloc(msg_length.data);
    if (msg_length.data != 0 && data == NULL)
        return ERR_NO_MEMORY;
    err = message_read(msg, &(ReceiveMessage){msg_length.data, data, 0, NULL}, NULL, NULL, 0, 0);
    if (err) {
        free(data);
        return err;
    }
    *data_ptr = data;
    *path_length_ptr = msg_length.data;
    return 0;
}

// Split a path into a filename and the entry of the directory containing it
// Returns ERR_FILE_EXISTS if provided path points to root.
static err_t split_destination(const u8 *path, size_t path_length, DirEntry *parent_entry_ptr, size_t *filename_start_ptr, u32 blocked_directory) {
    err_t err;
    // Fail if path points to root
    if (path_length == 0)
        return ERR_FILE_EXISTS;
    // Find last slash in path
    size_t parent_path_length = path_length;
    while (parent_path_length > 0) {
        parent_path_length--;
        if (path[parent_path_length] == '/')
            break;
    }
    // Get entry of parent
    err = entry_from_path(path, parent_path_length, parent_entry_ptr, NULL, blocked_directory);
    if (err)
        return err;
    *filename_start_ptr = parent_path_length == 0 && path[0] != '/' ? 0 : parent_path_length + 1;
    return 0;
}

static err_t entry_from_path_msg(handle_t msg, DirEntry *entry, DirEntryLocation *location) {
    err_t err;
    u8 *path;
    size_t path_length;
    err = get_message_data(msg, &path, &path_length);
    if (err)
        return err;
    err = entry_from_path(path, path_length, entry, location, 0);
    free(path);
    return err;
}

typedef struct OpenFile {
    DirEntry entry;
    u64 entry_offset;
} OpenFile;

void main(void) {
    err_t err;
    err = resource_get(&resource_name("virt_drive/read"), RESOURCE_TYPE_CHANNEL_SEND, &drive_read_channel);
    if (err)
        return;
    err = resource_get(&resource_name("virt_drive/write"), RESOURCE_TYPE_CHANNEL_SEND, &drive_write_channel);
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
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/stat_r"), (MessageTag){TAG_STAT, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/list_r"), (MessageTag){TAG_LIST, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/delete_r"), (MessageTag){TAG_DELETE, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/create_r"), (MessageTag){TAG_CREATE, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/move_r"), (MessageTag){TAG_MOVE, 0});
    if (err)
        return;
    err = mqueue_add_channel_resource(mqueue, &resource_name("file/open_r"), (MessageTag){TAG_OPEN, 0});
    if (err)
        return;
    while (1) {
        handle_t msg;
        MessageTag tag;
        mqueue_receive(mqueue, &tag, &msg, TIMEOUT_NONE, 0);
        switch (tag.data[0]) {
        case TAG_STAT: {
            DirEntry entry;
            err = entry_from_path_msg(msg, &entry, NULL);
            if (err)
                goto loop_fail;
            FileMetadata stat;
            stat_from_entry(&entry, &stat);
            message_reply(msg, &(SendMessage){1, &(SendMessageData){sizeof(FileMetadata), &stat}, 0, NULL}, FLAG_FREE_MESSAGE);
            break;
        }
        case TAG_LIST: {
            DirEntry entry;
            err = entry_from_path_msg(msg, &entry, NULL);
            if (err)
                goto loop_fail;
            size_t file_list_length;
            u8 *file_list;
            if (!(entry.attr & DIR_ENTRY_ATTR_DIRECTORY)) {
                err = ERR_NOT_DIR;
                goto loop_fail;
            }
            err = get_dir_list(entry_get_first_cluster(&entry), &file_list, &file_list_length);
            if (err)
                goto loop_fail;
            message_reply(msg, &(SendMessage){1, &(SendMessageData){file_list_length, file_list}, 0, NULL}, FLAG_FREE_MESSAGE);
            free(file_list);
            break;
        }
        case TAG_CREATE: {
            // Read message
            u8 *msg_data;
            size_t msg_data_length;
            err = get_message_data(msg, &msg_data, &msg_data_length);
            if (err)
                goto loop_fail;
            // Get flags
            if (msg_data_length < sizeof(u64)) {
                err = ERR_INVALID_ARG;
                goto create_fail;
            }
            u64 flags = *(u64 *)msg_data;
            if (flags & ~FLAG_CREATE_DIR) {
                err = ERR_INVALID_ARG;
                goto create_fail;
            }
            bool directory = flags & FLAG_CREATE_DIR;
            // Get path from message
            u8 *path = msg_data + sizeof(u64);
            size_t path_length = msg_data_length - sizeof(u64);
            // Split destination
            DirEntry parent_entry;
            size_t filename_start;
            err = split_destination(path, path_length, &parent_entry, &filename_start, 0);
            if (err)
                goto create_fail;
            // Create the file
            DirEntry entry;
            memset(&entry, 0, sizeof(DirEntry));
            if (directory) {
                u32 dir_first_cluster;
                err = allocate_first_dir_cluster(&dir_first_cluster, entry_get_first_cluster(&parent_entry));
                if (err)
                    goto create_fail;
                entry_set_first_cluster(&entry, dir_first_cluster);
                entry.attr = DIR_ENTRY_ATTR_DIRECTORY;
            }
            err = create_dir_entry(entry_get_first_cluster(&parent_entry), path + filename_start, path_length - filename_start, &entry, 0);
            if (err) {
                if (directory)
                    free_clusters(entry_get_first_cluster(&entry));
                goto create_fail;
            }
            message_reply(msg, NULL, FLAG_FREE_MESSAGE);
            free(msg_data);
            break;
create_fail:
            free(msg_data);
            goto loop_fail;
        }
        case TAG_DELETE: {
            DirEntry entry;
            DirEntryLocation location;
            err = entry_from_path_msg(msg, &entry, &location);
            if (err)
                goto loop_fail;
            err = delete_file_entry(&location);
            if (err)
                goto loop_fail;
            err = free_clusters(entry_get_first_cluster(&entry));
            if (err)
                goto loop_fail;
            message_reply(msg, NULL, FLAG_FREE_MESSAGE);
            break;
        }
        case TAG_MOVE: {
            // Read message
            u8 *msg_data;
            size_t msg_data_length;
            err = get_message_data(msg, &msg_data, &msg_data_length);
            if (err)
                goto loop_fail;
            // Get length of source path
            if (msg_data_length < sizeof(size_t)) {
                err = ERR_INVALID_ARG;
                goto move_fail;
            }
            size_t src_path_length = *(size_t *)msg_data;
            if (src_path_length > msg_data_length - sizeof(size_t)) {
                err = ERR_INVALID_ARG;
                goto move_fail;
            }
            // Get source and destination path from message
            u8 *src_path = msg_data + sizeof(size_t);
            u8 *dest_path = src_path + src_path_length;
            size_t dest_path_length = msg_data_length - sizeof(size_t) - src_path_length;
            // Get source entry
            DirEntry src_entry;
            DirEntryLocation src_location;
            err = entry_from_path(src_path, src_path_length, &src_entry, &src_location, 0);
            if (err)
                goto move_fail;
            // Split destination
            DirEntry dest_parent_entry;
            size_t dest_filename_start;
            err = split_destination(dest_path, dest_path_length, &dest_parent_entry, &dest_filename_start, entry_get_first_cluster(&src_entry));
            if (err)
                goto move_fail;
            // Create new entry in destination folder
            err = create_dir_entry(entry_get_first_cluster(&dest_parent_entry), dest_path + dest_filename_start, dest_path_length - dest_filename_start, &src_entry, src_location.main_entry_offset);
            if (err)
                goto move_fail;
            // Remove source entry
            err = delete_file_entry(&src_location);
            if (err)
                goto move_fail;
            // If the moved file is a directory, change its .. entry to point at the new parent
            if (src_entry.attr & DIR_ENTRY_ATTR_DIRECTORY) {
                // Read second entry of the directory, which should be the .. entry
                DirEntry dotdot_entry;
                err = drive_read(fat_cluster_offset(entry_get_first_cluster(&src_entry)) + sizeof(DirEntry), sizeof(DirEntry), &dotdot_entry);
                if (err)
                    goto move_fail;
                // Verify entry is actually the .. entry
                if (memcmp(&dotdot_entry.name, "..         ", 11) == 0) {
                    // Change the first cluster of the entry
                    u32 dest_parent_first_cluster = entry_get_first_cluster(&dest_parent_entry);
                    entry_set_first_cluster(&dotdot_entry, dest_parent_first_cluster == root_cluster ? 0 : dest_parent_first_cluster);
                    err = drive_write(fat_cluster_offset(entry_get_first_cluster(&src_entry)) + sizeof(DirEntry), sizeof(DirEntry), &dotdot_entry);
                    if (err)
                        goto move_fail;
                }
            }
            free(msg_data);
            message_reply(msg, NULL, FLAG_FREE_MESSAGE);
            break;
move_fail:
            free(msg_data);
            goto loop_fail;
        }
        case TAG_OPEN: {
            DirEntry entry;
            DirEntryLocation location;
            err = entry_from_path_msg(msg, &entry, &location);
            if (err)
                goto loop_fail;
            OpenFile *open_file = malloc(sizeof(OpenFile));
            if (open_file == NULL) {
                err = ERR_NO_MEMORY;
                goto loop_fail;
            }
            handle_t file_read_in, file_read_out;
            err = channel_create(&file_read_in, &file_read_out);
            if (err)
                goto file_read_alloc_fail;
            handle_t file_write_in, file_write_out;
            err = channel_create(&file_write_in, &file_write_out);
            if (err)
                goto file_write_alloc_fail;
            handle_t file_resize_in, file_resize_out;
            err = channel_create(&file_resize_in, &file_resize_out);
            if (err)
                goto file_resize_alloc_fail;
            open_file->entry = entry;
            open_file->entry_offset = location.main_entry_offset;
            mqueue_add_channel(mqueue, file_read_out, (MessageTag){TAG_READ, (uintptr_t)open_file});
            mqueue_add_channel(mqueue, file_write_out, (MessageTag){TAG_WRITE, (uintptr_t)open_file});
            mqueue_add_channel(mqueue, file_resize_out, (MessageTag){TAG_RESIZE, (uintptr_t)open_file});
            message_reply(msg, &(SendMessage){0, NULL, 1, &(SendMessageHandles){3, (SendAttachedHandle[]){{0, file_read_in}, {0, file_write_in}, {0, file_resize_in}}}}, FLAG_FREE_MESSAGE);
            break;
file_resize_alloc_fail:
            handle_free(file_write_in);
            handle_free(file_write_out);
file_write_alloc_fail:
            handle_free(file_read_in);
            handle_free(file_read_out);
file_read_alloc_fail:
            free(open_file);
            goto loop_fail;
        }
        case TAG_READ: {
            OpenFile *open_file = (OpenFile *)tag.data[1];
            FileRange range;
            err = message_read(msg, &(ReceiveMessage){sizeof(FileRange), &range, 0, NULL}, NULL, NULL, 0, 0);
            if (err)
                goto loop_fail;
            // Verify range falls within file size
            if (range.offset + range.length < range.offset || range.offset + range.length > open_file->entry.file_size) {
                err = ERR_EOF;
                goto loop_fail;
            }
            // Special case for zero length
            if (range.length == 0) {
                err = message_reply(msg, NULL, FLAG_FREE_MESSAGE);
                if (err)
                    goto loop_fail;
                break;
            }
            u8 *data_buf = malloc(range.length);
            if (data_buf == NULL) {
                err = ERR_NO_MEMORY;
                goto loop_fail;
            }
            err = read_file(entry_get_first_cluster(&open_file->entry), range.offset, range.length, data_buf);
            if (err)
                goto loop_fail;
            err = message_reply(msg, &(SendMessage){1, &(SendMessageData){range.length, data_buf}, 0, NULL}, FLAG_FREE_MESSAGE);
            if (err)
                goto loop_fail;
            break;
        }
        case TAG_WRITE: {
            OpenFile *open_file = (OpenFile *)tag.data[1];
            u64 offset;
            err = message_read(msg, &(ReceiveMessage){sizeof(u64), &offset, 0, NULL}, NULL, NULL, 0, FLAG_ALLOW_PARTIAL_DATA_READ);
            if (err)
                goto loop_fail;
            MessageLength msg_length;
            message_get_length(msg, &msg_length);
            u64 length = msg_length.data - sizeof(u64);
            // Verify range falls within u32 range and offset falls within file size
            if (offset + length < offset || offset + length > UINT32_MAX || offset > open_file->entry.file_size) {
                err = ERR_EOF;
                goto loop_fail;
            }
            // Special case for zero length
            if (length == 0) {
                err = message_reply(msg, NULL, FLAG_FREE_MESSAGE);
                if (err)
                    goto loop_fail;
                break;
            }
            u8 *data_buf = malloc(length);
            if (data_buf == NULL) {
                err = ERR_NO_MEMORY;
                goto loop_fail;
            }
            // Resize if writing past end
            if (offset + length > open_file->entry.file_size) {
                err = resize_file(&open_file->entry, open_file->entry_offset, offset + length, false);
                if (err)
                    goto loop_fail;
            }
            message_read(msg, &(ReceiveMessage){length, data_buf, 0, NULL}, &(MessageLength){sizeof(u64), 0}, NULL, 0, 0);
            err = write_file(entry_get_first_cluster(&open_file->entry), offset, length, data_buf);
            if (err)
                goto loop_fail;
            err = message_reply(msg, NULL, FLAG_FREE_MESSAGE);
            if (err)
                goto loop_fail;
            break;
        }
        case TAG_RESIZE: {
            OpenFile *open_file = (OpenFile *)tag.data[1];
            u64 new_size;
            err = message_read(msg, &(ReceiveMessage){sizeof(u64), &new_size, 0, NULL}, NULL, NULL, 0, 0);
            if (err)
                goto loop_fail;
            if (new_size > UINT32_MAX) {
                err = ERR_NO_SPACE;
                goto loop_fail;
            }
            err = resize_file(&open_file->entry, open_file->entry_offset, new_size, true);
            if (err)
                goto loop_fail;
            err = message_reply(msg, NULL, FLAG_FREE_MESSAGE);
            if (err)
                goto loop_fail;
        }
        }
        continue;
loop_fail:
        message_reply_error(msg, user_error_code(err), FLAG_FREE_MESSAGE);
    }
}
