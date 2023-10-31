#include "types.h"

#include "acpi.h"
#include "alloc.h"
#include "channel.h"
#include "framebuffer.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "ps2.h"
#include "segment.h"
#include "smp.h"
#include "stack.h"
#include "time.h"

void _string_init(void);

void kernel_start(void *stack) {
    framebuffer_init();
    _string_init();
    if (interrupt_init(true) != 0) {
        print_string("Failed to initialize interrupt handlers\n");
        goto halt;
    }
    if (page_alloc_init() != 0) {
        print_string("Failed to initialize paging structures\n");
        goto halt;
    }
    if (_alloc_init() != 0) {
        print_string("Failed to initialize memory allocator\n");
        goto halt;
    }
    if (percpu_init(stack) != 0) {
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
    time_init();
    apic_init(true);
    smp_init();
    pit_init();
    smp_init_sync(true);
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
    if (interrupt_init(false) != 0) {
        framebuffer_lock();
        print_string("Failed to initialize interrupt handlers on AP\n");
        framebuffer_unlock();
        goto halt;
    }
    if (percpu_init(stack) != 0) {
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
