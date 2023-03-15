#include "types.h"
#include "elf.h"

#include "page.h"
#include "string.h"

#define ELF_CLASS_64_BIT 2
#define ELF_ENDIAN_LITTLE 1
#define ELF_HEADER_VERSION_CURRENT 1
#define ELF_ABI_SYSV 0
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 0x3E
#define ELF_VERSION_CURRENT 1

#define ELF_PT_TYPE_LOAD 1
#define ELF_PT_FLAGS_X 1
#define ELF_PT_FLAGS_W 2
#define ELF_PT_FLAGS_R 4

#define ELF_MAGIC_SIZE 4
static const u8 elf_magic[ELF_MAGIC_SIZE] = {0x7F, 0x45, 0x4C, 0x46};

typedef struct ELFHeader {
    u8 magic[ELF_MAGIC_SIZE];
    u8 class;
    u8 endianness;
    u8 header_version;
    u8 abi;
    u8 abi_version;
    u8 reserved1[7];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 pht_offset;
    u64 sht_offset;
    u32 flags;
    u16 header_size;
    u16 pht_entry_size;
    u16 pht_entries_num;
    u16 sht_entry_size;
    u16 sht_entries_num;
    u16 sht_string_table_index;
} __attribute__((packed)) ELFHeader;

typedef struct ELFProgramHeader {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} __attribute__((packed)) ELFProgramHeader;

// Loads an ELF file stored in a buffer into memory.
// On success `*entry` is set to the entry point.
err_t load_elf_file(const u8 *file, size_t file_length, u64 *entry) {
    err_t err;
    // Verify the ELF header
    if (sizeof(ELFHeader) > file_length)
        return ERR_INVALID_ARG;
    ELFHeader *header = (ELFHeader *)file;
    if (memcmp(header->magic, elf_magic, ELF_MAGIC_SIZE) != 0)
        return ERR_INVALID_ARG;
    if (header->class != ELF_CLASS_64_BIT)
        return ERR_INVALID_ARG;
    if (header->endianness != ELF_ENDIAN_LITTLE)
        return ERR_INVALID_ARG;
    if (header->header_version != ELF_HEADER_VERSION_CURRENT)
        return ERR_INVALID_ARG;
    if (header->abi != ELF_ABI_SYSV)
        return ERR_INVALID_ARG;
    if (header->type != ELF_TYPE_EXEC)
        return ERR_INVALID_ARG;
    if (header->machine != ELF_MACHINE_X86_64)
        return ERR_INVALID_ARG;
    if (header->version != ELF_VERSION_CURRENT)
        return ERR_INVALID_ARG;
    if (header->pht_entry_size < sizeof(ELFProgramHeader))
        return ERR_INVALID_ARG;
    if (header->pht_offset + header->pht_entry_size * header->pht_entries_num < header->pht_offset)
        return ERR_INVALID_ARG;
    if (header->pht_offset + header->pht_entry_size * header->pht_entries_num > file_length)
        return ERR_INVALID_ARG;
    // Load the program segments into memory
    for (u16 i = 0; i < header->pht_entries_num; i++) {
        ELFProgramHeader *program_header = (ELFProgramHeader *)(file + header->pht_offset + header->pht_entry_size * i);
        if (program_header->type == ELF_PT_TYPE_LOAD) {
            // Verify the program header
            if (program_header->offset + program_header->file_size < program_header->offset)
                return ERR_INVALID_ARG;
            if (program_header->offset + program_header->file_size > file_length)
                return ERR_INVALID_ARG;
            if (program_header->file_size > program_header->memory_size)
                return ERR_INVALID_ARG;
            if (program_header->vaddr + program_header->offset < program_header->vaddr)
                return ERR_INVALID_ARG;
            if (program_header->vaddr + program_header->offset > USER_MAX_ADDR)
                return ERR_INVALID_ARG;
            // Calculate the first and last page to map
            u64 start_page = program_header->vaddr / PAGE_SIZE * PAGE_SIZE;
            u64 end_page = (program_header->vaddr + program_header->memory_size + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
            // Map the memory range
            err = map_user_pages(
                start_page,
                end_page - start_page,
                (program_header->flags & ELF_PT_FLAGS_W) != 0,
                (program_header->flags & ELF_PT_FLAGS_X) != 0
            );
            if (err)
                return err;
            // Copy segment data from file to memory
            memcpy((void *)program_header->vaddr, file + program_header->offset, program_header->file_size);
            // Fill in the memory that's mapped but not copied to with zeroes
            // This includes both memory that's not filled due to segments not being page-aligned,
            // as well as memory that's specified as zeroed by the ELF file (in the difference between memory_size and file_size).
            u64 loaded_end_byte = program_header->vaddr + program_header->file_size;
            memset((void *)start_page, 0, program_header->vaddr - start_page);
            memset((void *)loaded_end_byte, 0, end_page - loaded_end_byte);
        }
    }
    *entry = header->entry;
    return 0;
}
