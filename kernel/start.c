#include "types.h"

#include "acpi.h"
#include "alloc.h"
#include "elf.h"
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
    page_alloc_init();
    if (!alloc_init()) {
        print_string("Failed to initialize memory allocator\n");
        return;
    }
    if (!interrupt_init()) {
        print_string("Failed to initialize interrupt handlers\n");
        return;
    }
    if (!percpu_init()) {
        print_string("Failed to initialize CPU-local storage\n");
        return;
    }
    if (!gdt_init()) {
        print_string("Failed to initialize GDT\n");
        return;
    }
    userspace_init();
    pic_disable();
    pit_init();
    ps2_init();
    if (!acpi_init()) {
        print_string("Failed to read ACPI tables\n");
        return;
    }
    if (!stack_init()) {
        print_string("Failed to initialize kernel stack manager\n");
        return;
    }
    apic_init();
    smp_init();
    framebuffer_lock();
    print_string("BSP finished initialization\n");
    framebuffer_unlock();
    smp_init_sync_1();
    remove_identity_mapping();
    framebuffer_lock();
    print_string("Loading ELF file\n");
    framebuffer_unlock();
    u64 program_entry;
    if (load_elf_file(included_file_program, included_file_program_end - included_file_program, &program_entry)) {
        framebuffer_lock();
        print_string("Loaded ELF file\n");
        framebuffer_unlock();
        spawn_process(program_entry, 'A');
        spawn_process(program_entry, 'B');
        spawn_process(program_entry, 'C');
        spawn_process(program_entry, 'D');
        spawn_process(program_entry, 'E');
        spawn_process(program_entry, 'F');
        spawn_process(program_entry, 'G');
        spawn_process(program_entry, 'H');
        smp_init_sync_2();
        sched_start();
    } else {
        framebuffer_lock();
        print_string("Failed to load ELF file\n");
        framebuffer_unlock();
    }
}

void kernel_start_ap(void) {
    if (!interrupt_init()) {
        framebuffer_lock();
        print_string("Failed to initialize interrupt handlers on AP\n");
        framebuffer_unlock();
        return;
    }
    if (!percpu_init()) {
        framebuffer_lock();
        print_string("Failed to initialize CPU-local storage on AP\n");
        framebuffer_unlock();
        return;
    }
    if (!gdt_init()) {
        framebuffer_lock();
        print_string("Failed to initialize GDT on AP\n");
        framebuffer_unlock();
        return;
    }
    userspace_init();
    apic_init();
    framebuffer_lock();
    print_string("AP finished initialization\n");
    framebuffer_unlock();
    smp_init_sync_1();
    smp_init_sync_2();
    sched_start();
}
