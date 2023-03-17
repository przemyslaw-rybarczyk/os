#include "types.h"
#include "interrupt.h"

#include "alloc.h"
#include "framebuffer.h"
#include "process.h"
#include "segment.h"
#include "smp.h"

#define IDT_GATE_PRESENT 0x80
#define IDT_GATE_INTERRUPT 0x0E

#define IDT_ENTRIES_NUM 0x30

#define INT_PAGE_FAULT 0x0E

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

typedef struct IDTR {
    u16 size;
    u64 offset;
} __attribute__((packed)) IDTR;

static void idt_set_entry(IDTEntry *entry, u64 addr) {
    *entry = (IDTEntry){
        .addr1 = (u16)addr,
        .segment = SEGMENT_KERNEL_CODE,
        .ist = 0,
        .gate_type = IDT_GATE_PRESENT | IDT_GATE_INTERRUPT,
        .addr2 = (u16)(addr >> 16),
        .addr3 = (u32)(addr >> 32),
        .reserved1 = 0,
    };
}

IDTEntry idt_bsp[IDT_ENTRIES_NUM];
IDTR idtr_bsp;

// Initialize the IDT
// If `bsp` is set, the IDT and IDTR are set from the statically allocated variables `idt_bsp` and `idtr_bsp`, and not allocated dynamically.
// This option so that interrupts can be initialized on the BSP before initializing the page allocator and memory allocator.
// This way, if there's an issue with either of those it will produce an error message rather than a triple fault.
err_t interrupt_init(bool bsp) {
    // Allocate the IDT and IDTR
    size_t idt_size = IDT_ENTRIES_NUM * sizeof(IDTEntry);
    IDTEntry *idt = bsp ? &idt_bsp : malloc(idt_size);
    if (idt == NULL)
        return ERR_NO_MEMORY;
    IDTR *idtr = bsp ? &idtr_bsp : malloc(sizeof(IDTR));
    if (idtr == NULL) {
        free(idt);
        return ERR_NO_MEMORY;
    }
    *idtr = (IDTR){idt_size - 1, (u64)idt};
    // Fill the IDT entries with the handlers defined in `interrupt.s`
    // Interrupts with handler address given as 0 don't have a handler.
    for (u64 i = 0; i < IDT_ENTRIES_NUM; i++) {
        if (interrupt_handlers[i] != 0)
            idt_set_entry(&idt[i], interrupt_handlers[i]);
    }
    // Load the IDT Descriptor
    asm volatile ("lidt [%0]" : : "r"(idtr));
    return 0;
}

typedef struct InterruptFrame {
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} __attribute__((aligned)) InterruptFrame;

static bool interrupt_pushes_error_code(u8 i) {
    return i == 0x08 || i == 0x0A || i == 0x0B || i == 0x0C || i == 0x0D || i == 0x0E || i == 0x11 || i == 0x15 || i == 0x1D || i == 0x1E;
}

// This is a default handler used for exceptions that don't have a specific handler assigned to them.
// It's called by the wrapper in `interrupt.s`.
// It prints the exception information and halts.
void general_exception_handler(u8 interrupt_number, InterruptFrame *interrupt_frame, u64 error_code) {
    // If the exception occurred in user mode, kill the currently running process
    if ((interrupt_frame->cs & 3) != 0)
        process_exit();
    // Stop all other cores
    send_halt_ipi();
    // We do not lock the framebuffer before printing, as it may be held by whatever code caused the exception to occur.
    // Not locking won't be a problem anyway, as all other cores have already been stopped by send_halt_ipi().
    print_string("An exception has occurred.\n");
    print_string("Exception number: ");
    print_hex_u8(interrupt_number);
    print_newline();
    print_string("RIP:    ");
    print_hex_u64(interrupt_frame->rip);
    print_newline();
    print_string("CS:     ");
    print_hex_u64(interrupt_frame->cs);
    print_newline();
    print_string("RFLAGS: ");
    print_hex_u64(interrupt_frame->rflags);
    print_newline();
    print_string("RSP:    ");
    print_hex_u64(interrupt_frame->rsp);
    print_newline();
    print_string("SS:     ");
    print_hex_u64(interrupt_frame->ss);
    print_newline();
    if (interrupt_pushes_error_code(interrupt_number)) {
        print_string("Error code: ");
        print_hex_u64(error_code);
        print_newline();
    }
    if (interrupt_number == INT_PAGE_FAULT) {
        u64 page_fault_address;
        asm volatile ("mov %0, cr2" : "=r"(page_fault_address));
        print_string("Page fault address: ");
        print_hex_u64(page_fault_address);
        print_newline();
    }
    while (1)
        asm volatile("hlt");
}
