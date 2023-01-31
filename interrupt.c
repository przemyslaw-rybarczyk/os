#include "types.h"
#include "interrupt.h"

#include "framebuffer.h"
#include "segment.h"

#define IDT_GATE_PRESENT 0x80
#define IDT_GATE_INTERRUPT 0x0E

#define IDT_ENTRIES_NUM 0x20

extern u64 interrupt_handlers[IDT_ENTRIES_NUM];

typedef struct IDTEntry {
    u16 addr1;
    u16 segment;
    u8 ist;
    u8 gate_type;
    u16 addr2;
    u32 addr3;
    u32 reserved1;
} __attribute__((packed)) IDTEntry;

static IDTEntry idt[IDT_ENTRIES_NUM];

typedef struct IDTR {
    u16 size;
    u64 offset;
} __attribute__((packed)) IDTR;

static const IDTR idtr = (IDTR){sizeof(idt) - 1, (u64)&idt};

static void idt_set_entry(u64 i, u64 addr) {
    idt[i] = (IDTEntry){
        .addr1 = (u16)addr,
        .segment = SEGMENT_KERNEL_CODE,
        .ist = 0,
        .gate_type = IDT_GATE_PRESENT | IDT_GATE_INTERRUPT,
        .addr2 = (u16)(addr >> 16),
        .addr3 = (u32)(addr >> 32),
        .reserved1 = 0,
    };
}

void interrupt_init(void) {
    // Fill the IDT entries with the handlers defined in `interrupt.s`
    for (u64 i = 0; i < IDT_ENTRIES_NUM; i++)
        idt_set_entry(i, interrupt_handlers[i]);
    // Load the IDT Descriptor
    asm volatile ("lidt [%0]" : : "i"(&idtr));
}

// This is a default handler used for exceptions that don't have a specific handler assigned to them.
// It's called by the wrapper in `interrupt.s`.
// It prints the exception number and halts.
void general_exception_handler(u8 interrupt_number, __attribute__((unused)) void *sp) {
    print_string("An exception has occurred.\n");
    print_string("Exception number: ");
    print_hex(interrupt_number, 2);
    print_newline();
    while (1)
        asm volatile("hlt");
}
