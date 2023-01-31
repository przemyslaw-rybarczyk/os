#include "types.h"

#include "framebuffer.h"
#include "interrupt.h"

void kernel_main(void) {
    framebuffer_init();
    interrupt_init();
    u32 fb_width = get_framebuffer_width();
    u32 fb_height = get_framebuffer_height();
    for (u32 y = 0; y < fb_height; y++)
        for (u32 x = 0; x < fb_width; x++)
            put_pixel(x, y, (u8)x, (u8)y, (u8)(x + y));
    print_char('\n');
    print_string("Hello, world!\n");
    print_char('\n');
    print_string("Font test:\n");
    for (char c = ' '; c <= '~'; c++)
        print_char(c);
    print_char('\n');
    asm volatile ("int 0x1F");
    while (1)
        asm volatile("hlt");
}
