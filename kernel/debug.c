#include "debug.h"

// Send byte to serial port COM1
static void byte_to_com1(char c) {
    asm ("mov dx, 0x3F8; out dx, al" : : "a"(c) : "dx");
}

void debug_print_char(char c) {
    if (c == '\n')
        byte_to_com1('\r');
    byte_to_com1(c);
}

void debug_print_string(const char *str) {
    for (const char *c = str; *c != '\0'; c++)
        debug_print_char(*c);
}

// Print the last `digits` digits of a number in hexadecimal
static void debug_print_hex(u64 n, u64 digits) {
    debug_print_char('0');
    debug_print_char('x');
    for (u64 i = 0; i < digits; i++) {
        u64 digit = (n >> (4 * (digits - 1 - i))) & 0xF;
        debug_print_char(digit < 10 ? digit + '0' : digit - 10 + 'A');
    }
}

void debug_print_hex_u64(u64 n) {
    debug_print_hex(n, 16);
}

void debug_print_hex_u32(u32 n) {
    debug_print_hex((u64)n, 8);
}

void debug_print_hex_u16(u16 n) {
    debug_print_hex((u64)n, 4);
}

void debug_print_hex_u8(u8 n) {
    debug_print_hex((u64)n, 2);
}

