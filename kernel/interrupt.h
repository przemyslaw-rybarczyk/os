#pragma once

#include "error.h"

#define IDT_ENTRIES_NUM 0x30

typedef struct IDTEntry {
    u16 addr1;
    u16 segment;
    u8 ist;
    u8 gate_type;
    u16 addr2;
    u32 addr3;
    u32 reserved1;
} IDTEntry;

typedef struct IDTR {
    u16 size;
    u64 offset;
} __attribute__((packed)) IDTR;

void interrupt_init(IDTEntry *idt, IDTR *idtr);
void interrupt_disable(void);
void interrupt_enable(void);
void panic(const char *str);
