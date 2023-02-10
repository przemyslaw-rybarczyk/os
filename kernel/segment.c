#include "types.h"
#include "segment.h"

#include "process.h"

#define GDT_RW (1 << 1)
#define GDT_EXECUTABLE (1 << 3)
#define GDT_S (1 << 4)
#define GDT_RING_3 (3 << 5)
#define GDT_PRESENT (1 << 7)

#define GDT_TSS_TYPE_64_BIT_AVAILABLE 0x09

#define GDT_LONG_CODE (1 << 5)
#define GDT_DB (1 << 6)
#define GDT_GRANULAR (1 << 7)

typedef union GDTEntry {
    // Normal segment descriptor
    struct {
        u16 limit1; // limit bits 0-15
        u16 base1; // base bits 0-15
        u8 base2; // base bits 16-23
        u8 access; // access byte
        u8 flags_limit2; // flags and limit bits 16-19
        u8 base3; // base bits 24-31
    };
    // Second half of TSS descriptor
    struct {
        u32 base4; // base bits 32-63
        u32 reserved1;
    };
} __attribute__((packed)) GDTEntry;

typedef struct GDTR {
    u16 size;
    u64 offset;
} __attribute__((packed)) GDTR;

static GDTEntry gdt[] = {
    // Entry 0x00 - unused
    (GDTEntry){},
    // Entry 0x08 - kernel code
    (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_S | GDT_EXECUTABLE | GDT_RW,
        .flags_limit2 = GDT_LONG_CODE | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    },
    // Entry 0x10 - kernel data
    (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_S | GDT_RW,
        .flags_limit2 = GDT_DB | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    },
    // Entry 0x18 - user code
    (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_RING_3 | GDT_S | GDT_EXECUTABLE | GDT_RW,
        .flags_limit2 = GDT_LONG_CODE | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    },
    // Entry 0x20 - user data
    (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_RING_3 | GDT_S | GDT_RW,
        .flags_limit2 = GDT_DB | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    },
    // Entry 0x28 - TSS descriptor
    // Filled by the initialization function.
    (GDTEntry){},
    (GDTEntry){},
};

static const GDTR gdtr = (GDTR){sizeof(gdt) - 1, (u64)&gdt};

void gdt_init(void) {
    // Fill the TSS descriptor
    gdt[TSS_DESCRIPTOR / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = (u16)(tss_end - tss),
        .base1 = (u16)(u64)tss,
        .base2 = (u8)((u64)tss >> 16),
        .access = GDT_PRESENT | GDT_RING_3 | GDT_TSS_TYPE_64_BIT_AVAILABLE,
        .flags_limit2 = (u8)((u64)(tss_end - tss) >> 16),
        .base3 = (u8)((u64)tss >> 24),
    };
    gdt[TSS_DESCRIPTOR / sizeof(GDTEntry) + 1] = (GDTEntry){
        .base4 = (u32)((u64)tss >> 32),
        .reserved1 = 0,
    };
    // Load the GDT
    asm volatile ("lgdt [%0]" : : "i"(&gdtr));
}
