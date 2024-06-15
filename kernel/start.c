#include "types.h"

#include "acpi.h"
#include "ahci.h"
#include "alloc.h"
#include "channel.h"
#include "framebuffer.h"
#include "interrupt.h"
#include "page.h"
#include "pci.h"
#include "percpu.h"
#include "pic.h"
#include "process.h"
#include "ps2.h"
#include "segment.h"
#include "smp.h"
#include "stack.h"
#include "time.h"

// There are two things that we need to initialize before being able to use the memory allocator:
// - the IDT, so that any errors that occur during further initialization won't cause a triple fault,
// - the per-CPU data, since the memory allocator modifies it when using spinlocks.
// Since we don't have access to the heap yet, we need to allocate the necessary memory earlier.
// The memory for early BSP initialization is allocated statically.
// For the APs, it's allocated on the heap by the BSP before starting them.
typedef struct PerCPUPrealloc {
    IDTEntry idt[IDT_ENTRIES_NUM];
    IDTR idtr;
    PerCPU percpu;
} PerCPUPrealloc;

PerCPUPrealloc bsp_prealloc;
PerCPUPrealloc *ap_prealloc;

void _string_init(void);

void kernel_start(void *stack) {
    framebuffer_init();
    _string_init();
    interrupt_init(bsp_prealloc.idt, &bsp_prealloc.idtr);
    percpu_init(&bsp_prealloc.percpu, stack);
    if (page_alloc_init() != 0) {
        print_string("Failed to initialize paging structures\n");
        goto halt;
    }
    if (_alloc_init() != 0) {
        print_string("Failed to initialize memory allocator\n");
        goto halt;
    }
    if (gdt_init() != 0) {
        print_string("Failed to initialize GDT\n");
        goto halt;
    }
    userspace_init();
    pic_disable();
    ps2_init();
    if (pci_init() != 0) {
        print_string("Failed to detect required PCI devices\n");
        goto halt;
    }
    if (acpi_init() != 0) {
        print_string("Failed to read ACPI tables\n");
        goto halt;
    }
    ap_prealloc = malloc((cpu_num - 1) * sizeof(PerCPUPrealloc));
    if (ap_prealloc == NULL) {
        print_string("Failed to allocate ap_prealloc\n");
        goto halt;
    }
    if (stack_init() != 0) {
        print_string("Failed to initialize kernel stack manager\n");
        goto halt;
    }
    time_init();
    apic_init(true);
    smp_init();
    smp_init_sync(true);
    if (ahci_init() != 0) {
        print_string("Failed to initialize AHCI controller\n");
        goto halt;
    }
    if (set_double_fault_stack() != 0) {
        framebuffer_lock();
        print_string("Failed to initialize double fault stack\n");
        framebuffer_unlock();
        goto halt;
    }
    remove_identity_mapping();
    process_setup();
    sched_start();
halt:
    interrupt_disable();
    while (1)
        asm volatile ("hlt");
}

void kernel_start_ap(void *stack) {
    u64 ap_id = ((u64)stack - PAGE_SIZE - KERNEL_INIT_STACK) / (2 * PAGE_SIZE) - 1;
    interrupt_init(ap_prealloc[ap_id].idt, &ap_prealloc[ap_id].idtr);
    percpu_init(&ap_prealloc[ap_id].percpu, stack);
    if (gdt_init() != 0) {
        framebuffer_lock();
        print_string("Failed to initialize GDT on AP\n");
        framebuffer_unlock();
        goto halt;
    }
    userspace_init();
    apic_init(false);
    smp_init_sync(false);
    if (set_double_fault_stack() != 0) {
        framebuffer_lock();
        print_string("Failed to initialize double fault stack\n");
        framebuffer_unlock();
        goto halt;
    }
    sched_start();
halt:
    interrupt_disable();
    while (1)
        asm volatile ("hlt");
}
