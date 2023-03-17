#include "types.h"

#include "acpi.h"
#include "alloc.h"
#include "framebuffer.h"
#include "included_programs.h"
#include "interrupt.h"
#include "keyboard.h"
#include "mouse.h"
#include "page.h"
#include "percpu.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "ps2.h"
#include "segment.h"
#include "smp.h"
#include "stack.h"

void kernel_start(void) {
    framebuffer_init();
    if (interrupt_init(true) != 0) {
        print_string("Failed to initialize interrupt handlers\n");
        goto halt;
    }
    if (page_alloc_init() != 0) {
        print_string("Failed to initialize paging structures\n");
        goto halt;
    }
    if (alloc_init() != 0) {
        print_string("Failed to initialize memory allocator\n");
        goto halt;
    }
    if (percpu_init() != 0) {
        print_string("Failed to initialize CPU-local storage\n");
        goto halt;
    }
    if (gdt_init() != 0) {
        print_string("Failed to initialize GDT\n");
        goto halt;
    }
    userspace_init();
    pic_disable();
    ps2_init();
    if (acpi_init() != 0) {
        print_string("Failed to read ACPI tables\n");
        goto halt;
    }
    if (stack_init() != 0) {
        print_string("Failed to initialize kernel stack manager\n");
        goto halt;
    }
    apic_init();
    smp_init();
    pit_init();
    framebuffer_lock();
    print_string("BSP finished initialization\n");
    framebuffer_unlock();
    smp_init_sync();
    for (u64 arg = 'A'; arg <= 'H'; arg++)
        process_spawn(included_file_program1, included_file_program1_end - included_file_program1, arg);
    for (u64 arg = 'a'; arg <= 'h'; arg++)
        process_spawn(included_file_program2, included_file_program2_end - included_file_program2, arg);
    sched_start();
halt:
    asm volatile ("cli");
    while (1)
        asm volatile ("hlt");
}

void kernel_start_ap(void) {
    if (interrupt_init(false) != 0) {
        framebuffer_lock();
        print_string("Failed to initialize interrupt handlers on AP\n");
        framebuffer_unlock();
        goto halt;
    }
    if (percpu_init() != 0) {
        framebuffer_lock();
        print_string("Failed to initialize CPU-local storage on AP\n");
        framebuffer_unlock();
        goto halt;
    }
    if (gdt_init() != 0) {
        framebuffer_lock();
        print_string("Failed to initialize GDT on AP\n");
        framebuffer_unlock();
        goto halt;
    }
    userspace_init();
    apic_init();
    framebuffer_lock();
    print_string("AP finished initialization\n");
    framebuffer_unlock();
    smp_init_sync();
    sched_start();
halt:
    asm volatile ("cli");
    while (1)
        asm volatile ("hlt");
}
