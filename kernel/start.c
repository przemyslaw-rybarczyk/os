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
    err_t err;
    framebuffer_init();
    _string_init();
    interrupt_init(bsp_prealloc.idt, &bsp_prealloc.idtr);
    percpu_init(&bsp_prealloc.percpu, stack);
    err = page_alloc_init();
    if (err)
        goto fail;
    err = _alloc_init();
    if (err)
        goto fail;
    err = gdt_init();
    if (err)
        goto fail;
    userspace_init();
    pic_disable();
    ps2_init();
    err = pci_init();
    if (err)
        goto fail;
    err = acpi_init();
    if (err)
        goto fail;
    ap_prealloc = malloc((cpu_num - 1) * sizeof(PerCPUPrealloc));
    if (cpu_num != 1 && ap_prealloc == NULL) {
        err = ERR_KERNEL_NO_MEMORY;
        goto fail;
    }
    err = stack_init();
    if (err)
        goto fail;
    time_init();
    apic_init(true);
    err = ahci_init();
    if (err)
        goto fail;
    err = set_double_fault_stack();
    if (err)
        goto fail;
    err = process_setup();
    if (err)
        goto fail;
    smp_init();
    smp_init_sync(true);
    remove_identity_mapping();
    sched_start();
fail:
    framebuffer_lock();
    if (err == ERR_KERNEL_NO_MEMORY) {
        print_string("Failed to intialize: out of memory\n");
    } else {
        print_string("Failed to intialize: error ");
        print_hex_u64(err);
        print_newline();
    }
    framebuffer_unlock();
    interrupt_disable();
    while (1)
        asm volatile ("hlt");
}

void kernel_start_ap(u64 ap_id, void *stack) {
    err_t err;
    interrupt_init(ap_prealloc[ap_id].idt, &ap_prealloc[ap_id].idtr);
    percpu_init(&ap_prealloc[ap_id].percpu, stack);
    err = gdt_init();
    if (err)
        goto fail;
    userspace_init();
    apic_init(false);
    err = set_double_fault_stack();
    if (err)
        goto fail;
    smp_init_sync(false);
    sched_start();
fail:
    framebuffer_lock();
    if (err == ERR_KERNEL_NO_MEMORY) {
        print_string("Failed to intialize AP: out of memory\n");
    } else {
        print_string("Failed to intialize AP: error ");
        print_hex_u64(err);
        print_newline();
    }
    framebuffer_unlock();
    interrupt_disable();
    while (1)
        asm volatile ("hlt");
}
