#pragma once

#include "types.h"
#include "channel.h"

extern Channel *stdout_channel;

void framebuffer_init(void);
u32 get_framebuffer_width(void);
u32 get_framebuffer_height(void);
void framebuffer_lock(void);
void framebuffer_unlock(void);
void put_pixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void print_newline(void);
void print_char(char c);
void print_string(const char *str);
void print_hex_u64(u64 n);
void print_hex_u32(u32 n);
void print_hex_u16(u16 n);
void print_hex_u8(u8 n);
_Noreturn void stdout_kernel_thread_main(void);
