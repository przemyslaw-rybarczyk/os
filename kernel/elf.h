#pragma once

#include "types.h"

bool load_elf_file(u8 *file, size_t file_length, u64 *entry);
