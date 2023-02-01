#include "types.h"

#include "framebuffer.h"
#include "interrupt.h"
#include "keyboard.h"
#include "pic.h"
#include "ps2.h"

void kernel_main(void) {
    framebuffer_init();
    interrupt_init();
    pic_init();
    ps2_init();
    asm volatile ("sti");
    u32 fb_width = get_framebuffer_width();
    u32 fb_height = get_framebuffer_height();
    for (u32 y = 0; y < fb_height; y++)
        for (u32 x = 0; x < fb_width; x++)
            put_pixel(x, y, (u8)x, (u8)y, (u8)(x + y));
    print_newline();
    print_string("Hello, world!\n");
    print_newline();
    print_string("Font test:\n");
    for (char c = ' '; c <= '~'; c++)
        print_char(c);
    print_newline();
    while (1) {
        KeyEvent event = keyboard_read();
        print_string("Key ");
        print_hex(event.keycode, 2);
        print_string(event.pressed ? " was pressed\n" : " was released\n");
    }
}
