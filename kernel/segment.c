#include "types.h"
#include "segment.h"

#define GDT_RW (1 << 1)
#define GDT_EXECUTABLE (1 << 3)
#define GDT_S (1 << 4)
#define GDT_RING_3 (3 << 5)
#define GDT_PRESENT (1 << 7)

#define GDT_LONG_CODE (1 << 5)
#define GDT_DB (1 << 6)
#define GDT_GRANULAR (1 << 7)

typedef struct GDTEntry {
    u16 limit1; // limit bits 0-15
    u16 base1; // base bits 0-15
    u8 base2; // base bits 16-23
    u8 access; // access byte
    u8 flags_limit2; // flags and limit bits 16-19
    u8 base3; // base bits 24-31
} __attribute__((packed)) GDTEntry;

typedef struct GDTR {
    u16 size;
    u64 offset;
} __attribute__((packed)) GDTR;

static const GDTEntry gdt[] = {
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
};

static const GDTR gdtr = (GDTR){sizeof(gdt) - 1, (u64)&gdt};

void gdt_init(void) {
    asm volatile ("lgdt [%0]" : : "i"(&gdtr));
}
