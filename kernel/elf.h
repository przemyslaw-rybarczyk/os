#pragma once

#include "types.h"
#include "error.h"

err_t load_elf_file(const u8 *file, size_t file_length, u64 *entry);
