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
