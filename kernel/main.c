#include "types.h"

#include "alloc.h"
#include "elf.h"
#include "framebuffer.h"
#include "included_programs.h"
#include "interrupt.h"
#include "keyboard.h"
#include "mouse.h"
#include "page.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "ps2.h"
#include "segment.h"

extern bool mouse_has_scroll_wheel;

void kernel_main(void) {
    framebuffer_init();
    gdt_init();
    userspace_init();
    interrupt_init();
    page_alloc_init();
    alloc_init();
    pic_init();
    pit_init();
    ps2_init();
    asm volatile ("sti");
    remove_identity_mapping();
    u32 fb_width = get_framebuffer_width();
    u32 fb_height = get_framebuffer_height();
    for (u32 y = 0; y < fb_height; y++)
        for (u32 x = 0; x < fb_width; x++)
            put_pixel(x, y, (u8)x, (u8)y, (u8)(x + y));
    print_newline();
    print_string("Loading ELF file\n");
    u64 program_entry;
    if (load_elf_file(included_file_program, included_file_program_end - included_file_program, &program_entry)) {
        print_string("Loaded ELF file\n");
        print_string("Jumping to process\n");
        spawn_process(program_entry);
    } else {
        print_string("Failed to load ELF file\n");
    }
}
