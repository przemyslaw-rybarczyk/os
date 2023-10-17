#include "types.h"
#include "segment.h"

#include "alloc.h"
#include "percpu.h"
#include "process.h"
#include "stack.h"

#define GDT_RW (1 << 1)
#define GDT_EXECUTABLE (1 << 3)
#define GDT_S (1 << 4)
#define GDT_RING_3 (3 << 5)
#define GDT_PRESENT (1 << 7)

#define GDT_TSS_TYPE_64_BIT_AVAILABLE 0x09

#define GDT_LONG_CODE (1 << 5)
#define GDT_DB (1 << 6)
#define GDT_GRANULAR (1 << 7)

#define GDT_ENTRIES_NUM 7

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

typedef struct TSS {
    u32 reserved1;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved2;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved3;
    u16 reserved4;
    u16 iopb;
} __attribute__((packed)) TSS;

err_t gdt_init(void) {
    // Allocate the GDT, GDTR, and TSS
    size_t gdt_size = GDT_ENTRIES_NUM * sizeof(GDTEntry);
    GDTEntry *gdt = malloc(gdt_size);
    if (gdt == NULL)
        return ERR_KERNEL_NO_MEMORY;
    GDTR *gdtr = malloc(sizeof(GDTR));
    if (gdtr == NULL) {
        free(gdt);
        return ERR_KERNEL_NO_MEMORY;
    }
    *gdtr = (GDTR){gdt_size - 1, (u64)gdt};
    TSS *tss = malloc(sizeof(TSS));
    if (tss == NULL) {
        free(gdtr);
        free(gdt);
        return ERR_KERNEL_NO_MEMORY;
    }
    // Fill the GDT
    // The layout of the GDT is to a degree forced by the design of the SYSCALL instruction.
    // It requires the kernel data selector to come after the kernel code selector,
    // and the user code selector to come after the user data selector.
    // Entry 0 is unused.
    gdt[0] = (GDTEntry){};
    gdt[SEGMENT_KERNEL_CODE / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_S | GDT_EXECUTABLE | GDT_RW,
        .flags_limit2 = GDT_LONG_CODE | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    };
    gdt[SEGMENT_KERNEL_DATA / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_S | GDT_RW,
        .flags_limit2 = GDT_DB | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    };
    gdt[SEGMENT_USER_DATA / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_RING_3 | GDT_S | GDT_RW,
        .flags_limit2 = GDT_DB | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    };
    gdt[SEGMENT_USER_CODE / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = 0xFFFF,
        .base1 = 0x0000,
        .base2 = 0x00,
        .access = GDT_PRESENT | GDT_RING_3 | GDT_S | GDT_EXECUTABLE | GDT_RW,
        .flags_limit2 = GDT_LONG_CODE | GDT_GRANULAR | 0xF,
        .base3 = 0x00,
    };
    gdt[TSS_DESCRIPTOR / sizeof(GDTEntry)] = (GDTEntry){
        .limit1 = (u16)sizeof(TSS),
        .base1 = (u16)(u64)tss,
        .base2 = (u8)((u64)tss >> 16),
        .access = GDT_PRESENT | GDT_TSS_TYPE_64_BIT_AVAILABLE,
        .flags_limit2 = (u8)(sizeof(TSS) >> 16),
        .base3 = (u8)((u64)tss >> 24),
    };
    gdt[TSS_DESCRIPTOR / sizeof(GDTEntry) + 1] = (GDTEntry){
        .base4 = (u32)((u64)tss >> 32),
        .reserved1 = 0,
    };
    // Fill the TSS
    *tss = (TSS){.iopb = sizeof(TSS)};
    cpu_local->tss = tss;
    // Load the GDT
    asm volatile ("lgdt [%0]" : : "r"(gdtr));
    return 0;
}

// Set stack for use by double fault handler
err_t set_double_fault_stack(void) {
    void *double_fault_stack = stack_alloc();
    if (double_fault_stack == NULL)
        return ERR_NO_MEMORY;
    cpu_local->tss->ist1 = (u64)double_fault_stack;
    return 0;
}
