#include "types.h"
#include "interrupt.h"

#include "alloc.h"
#include "framebuffer.h"
#include "page.h"
#include "process.h"
#include "segment.h"
#include "smp.h"
#include "string.h"

#define IDT_GATE_PRESENT 0x80
#define IDT_GATE_INTERRUPT 0x0E

#define INT_DOUBLE_FAULT 0x08
#define INT_PAGE_FAULT 0x0E

extern u64 interrupt_handlers[IDT_ENTRIES_NUM];

static void idt_set_entry(IDTEntry *entry, u64 addr, u8 ist) {
    *entry = (IDTEntry){
        .addr1 = (u16)addr,
        .segment = SEGMENT_KERNEL_CODE,
        .ist = ist,
        .gate_type = IDT_GATE_PRESENT | IDT_GATE_INTERRUPT,
        .addr2 = (u16)(addr >> 16),
        .addr3 = (u32)(addr >> 32),
        .reserved1 = 0,
    };
}

IDTEntry idt_bsp[IDT_ENTRIES_NUM];
IDTR idtr_bsp;

// Initialize the IDT
void interrupt_init(IDTEntry *idt, IDTR *idtr) {
    size_t idt_size = IDT_ENTRIES_NUM * sizeof(IDTEntry);
    // Clear the IDT
    memset(idt, 0, idt_size);
    // Set the IDTR
    *idtr = (IDTR){idt_size - 1, (u64)idt};
    // Fill the IDT entries with the handlers defined in `interrupt.s`
    // Interrupts with handler address given as 0 don't have a handler.
    for (u64 i = 0; i < IDT_ENTRIES_NUM; i++) {
        if (interrupt_handlers[i] != 0)
            idt_set_entry(&idt[i], interrupt_handlers[i], i == INT_DOUBLE_FAULT ? 1 : 0);
    }
    // Load the IDT Descriptor
    asm volatile ("lidt [%0]" : : "r"(idtr));
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
// If the interrupt occurred in kernel code, it prints the exception information and halts.
void general_exception_handler(u8 interrupt_number, InterruptFrame *interrupt_frame, u64 error_code) {
    // If the exception occurred in user mode, kill the currently running process
    if ((interrupt_frame->cs & 3) != 0) {
        interrupt_enable();
        process_exit();
    }
    u64 page_fault_address;
    if (interrupt_number == INT_PAGE_FAULT) {
        // If the interrupt is a page fault, get the page fault address from CR2
        asm ("mov %0, cr2" : "=r"(page_fault_address));
    }
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
        print_string("Page fault address: ");
        print_hex_u64(page_fault_address);
        print_newline();
    }
    while (1)
        asm volatile ("hlt");
}

// Function called when the kernel enters a state that should be impossible to reach.
// It prints a given message and halts.
void panic(const char *str) {
    // Stop all other cores
    send_halt_ipi();
    // Print the message and halt
    print_string("Kernel panic: ");
    print_string(str);
    while (1)
        asm volatile ("hlt");
}
