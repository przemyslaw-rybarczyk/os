#include <types.h>

#include <stdlib.h>
#include <string.h>
#include <syscalls.h>

typedef struct ScreenSize {
    size_t width;
    size_t height;
} ScreenSize;

void main(void) {
    err_t err;
    handle_t screen_size_msg;
    err = channel_call(2, 0, NULL, &screen_size_msg);
    if (err)
        return;
    size_t screen_size_msg_size;
    err = message_get_length(screen_size_msg, &screen_size_msg_size);
    if (err || screen_size_msg_size != sizeof(ScreenSize))
        return;
    ScreenSize screen_size;
    err = message_read(screen_size_msg, &screen_size);
    if (err)
        return;
    size_t screen_bytes = screen_size.height * screen_size.width * 3;
    u8 *screen = malloc(screen_bytes);
    if (screen == NULL)
        return;
    size_t i = 0;
    while (1) {
        for (size_t y = 0; y < screen_size.height; y++) {
            for (size_t x = 0; x < screen_size.width; x++) {
                u8 *pixel = &screen[(y * screen_size.width + x) * 3];
                double f = (double)i / 50.0;
                pixel[0] = (u8)(size_t)(2 * x - 8 * i);
                pixel[1] = (u8)(size_t)(x + f * y);
                pixel[2] = (u8)(size_t)(x - f * y);
            }
        }
        channel_call(1, screen_bytes, screen, NULL);
        i++;
    }
}
