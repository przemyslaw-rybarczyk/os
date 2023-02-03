#include "types.h"

#include "framebuffer.h"
#include "interrupt.h"
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "ps2.h"

extern bool mouse_has_scroll_wheel;

void kernel_main(void) {
    framebuffer_init();
    interrupt_init();
    pic_init();
    pit_init();
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
    if (mouse_has_scroll_wheel)
        print_string("Mouse has a scroll wheel");
    else
        print_string("Mouse has no scroll wheel");
    print_newline();
    print_string("Starting keyboard test.\nPress the F12 key to enter the mouse test.\n");
    while (1) {
        KeyEvent event = keyboard_read();
        print_string("Key ");
        print_hex(event.keycode, 2);
        print_string(event.pressed ? " was pressed\n" : " was released\n");
        if (event.keycode == KEY_F12 && event.pressed == false)
            break;
    }
    print_string("Starting mouse test.\n");
    i32 mouse_x = fb_width / 2;
    i32 mouse_y = fb_height / 2;
    i32 scroll_y = fb_height / 2;
    while (1) {
        MouseUpdate update = mouse_get_update();
        mouse_x += update.diff_x;
        mouse_y += update.diff_y;
        scroll_y += update.diff_scroll;
        if (mouse_x < 0)
            mouse_x = 0;
        if (mouse_x >= fb_width)
            mouse_x = fb_width - 1;
        if (mouse_y < 0)
            mouse_y = 0;
        if (mouse_y >= fb_height)
            mouse_y = fb_height - 1;
        if (scroll_y < 0)
            scroll_y = 0;
        if (scroll_y >= fb_height)
            scroll_y = fb_height - 1;
        for (u32 y = 0; y < fb_height; y++) {
            if (y == scroll_y)
                put_pixel(fb_width - 1, y, 255, 255, 255);
            else
                put_pixel(fb_width - 1, y, 0, 0, 0);
        }
        put_pixel(mouse_x, mouse_y, update.left_button_pressed * 255, update.middle_button_pressed * 255, update.right_button_pressed * 255);
    }
}
