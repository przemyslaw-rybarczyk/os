#pragma once

#include <zr/types.h>

typedef struct PhysDriveInfo {
    u32 sector_size;
    u32 sector_count;
} PhysDriveInfo;

typedef struct VirtDriveInfo {
    u64 guid[2];
    u64 size;
} VirtDriveInfo;

typedef struct PhysDriveOpenArgs {
    u32 drive_id;
    u64 offset;
    u64 length;
} PhysDriveOpenArgs;

typedef struct FileRange {
    u64 offset;
    u64 length;
} FileRange;

typedef struct FileMetadata {
    bool is_dir;
    u8 reserved1[7];
    u64 size;
    i64 create_time;
    i64 modify_time;
    i64 access_time;
} FileMetadata;

#define FLAG_CREATE_DIR (UINT64_C(1) << 0)
