#include "types.h"

#include "framebuffer.h"

void kernel_main(void) {
    framebuffer_init();
    u32 fb_width = get_framebuffer_width();
    u32 fb_height = get_framebuffer_height();
    for (u32 y = 0; y < fb_height; y++)
        for (u32 x = 0; x < fb_width; x++)
            put_pixel(x, y, (u8)x, (u8)y, (u8)(x + y));
    while (1)
        asm volatile("hlt");
}
