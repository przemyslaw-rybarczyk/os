#include "types.h"
#include "framebuffer.h"

#include "page.h"
#include "spinlock.h"
#include "string.h"

#include "font.h"

#include <zr/video.h>

#define CPUID_SSSE3 (UINT64_C(1) << 9)

#define FB_PML4E UINT64_C(0x1FD)

typedef struct VBEModeInfo {
    u16 attrs;
    u8 win_a_attrs;
    u8 win_b_attrs;
    u16 win_granularity;
    u16 win_size;
    u16 win_a_segment;
    u16 win_b_segment;
    u32 win_func_ptr;
    u16 bytes_per_scan_line;
    u16 x_res;
    u16 y_res;
    u8 x_char_size;
    u8 y_char_size;
    u8 number_of_planes;
    u8 bits_per_pixel;
    u8 number_of_banks;
    u8 memory_model;
    u8 bank_size;
    u8 number_of_image_pages;
    u8 reserved1;
    u8 red_size;
    u8 red_pos;
    u8 green_size;
    u8 green_pos;
    u8 blue_size;
    u8 blue_pos;
    u8 rsvd_size;
    u8 rsvd_pos;
    u8 direct_color_mode_info;
    u32 phys_base_ptr;
    u32 off_screen_mem_offset;
    u16 off_screen_mem_size;
    u8 reserved2[206];
} __attribute__((packed)) VBEModeInfo;

extern VBEModeInfo vbe_mode_info;

// These variables contain constants used to draw to the framebuffer.
static u8 *framebuffer;
u16 fb_pitch;
u16 fb_width;
u16 fb_height;
static u8 fb_bytes_per_pixel;

// When assembing the pixel color value, each 8-bit color component
// is first shifted right by the `cut` value to truncate the lower bits,
// and then shifted left by the pos value to put it in place.
static u8 r_cut;
static u8 r_pos;
static u8 g_cut;
static u8 g_pos;
static u8 b_cut;
static u8 b_pos;

// Data used for fast copy
static bool fb_fast_copy;
u8 __attribute__((aligned(16))) fb_fast_copy_shuf_mask[16];

extern u64 pd_fb[PAGE_MAP_LEVEL_SIZE];

// Set variables based on VBE mode information received from bootloader
// Note that the original struct will become unusable after kernel initialization completes and the identity mapping is removed.
void framebuffer_init(void) {
    fb_pitch = vbe_mode_info.bytes_per_scan_line;
    fb_width = vbe_mode_info.x_res;
    fb_height = vbe_mode_info.y_res;
    fb_bytes_per_pixel = vbe_mode_info.bits_per_pixel / 8;
    r_cut = 8 - vbe_mode_info.red_size;
    r_pos = vbe_mode_info.red_pos;
    g_cut = 8 - vbe_mode_info.green_size;
    g_pos = vbe_mode_info.green_pos;
    b_cut = 8 - vbe_mode_info.blue_size;
    b_pos = vbe_mode_info.blue_pos;

    // Get ECX from result of CPUID EAX=1h
    u32 cpuid_1_ecx;
    asm ("mov eax, 1; cpuid" : "=c"(cpuid_1_ecx) : : "eax", "ebx", "edx");

    // Fast copy is only usable if the framebuffer uses a four bytes per pixel representation,
    // with each color channel corresponding to one byte.
    // Additionally, SSSE3 must be supported, since the fast copy function uses the PSHUFB instruction.
    fb_fast_copy =
        (cpuid_1_ecx & CPUID_SSSE3) != 0 &&
        fb_bytes_per_pixel == 4 &&
        r_cut == 0 && g_cut == 0 && b_cut == 0 &&
        r_pos % 8 == 0 && g_pos % 8 == 0 && b_pos % 8 == 0;
    // Create a mask for the PSHUFB instruction used for the fast copy
    if (fb_fast_copy) {
        memset(fb_fast_copy_shuf_mask, 0x80, 16);
        for (int i = 0; i < 4; i++) {
            fb_fast_copy_shuf_mask[4 * i + r_pos / 8] = 3 * i;
            fb_fast_copy_shuf_mask[4 * i + g_pos / 8] = 3 * i + 1;
            fb_fast_copy_shuf_mask[4 * i + b_pos / 8] = 3 * i + 2;
        }
    }

    // Map the framebuffer at the beginning of PML4E number FB_PML4E using large pages
    u32 fb_phys_addr = vbe_mode_info.phys_base_ptr;
    u64 fb_virt_addr = ASSEMBLE_ADDR_PDE(FB_PML4E, 0, 0, fb_phys_addr);
    framebuffer = (u8 *)fb_virt_addr;
    u64 first_page = fb_phys_addr >> 21;
    u64 last_page = (fb_phys_addr + fb_height * fb_pitch - 1) >> 21;
    u64 num_pages = last_page - first_page + 1;
    // Make sure mapping fits in 1 GiB, although the frambuffer shouldn't ever be this large
    if (num_pages > PAGE_MAP_LEVEL_SIZE)
        num_pages = PAGE_MAP_LEVEL_SIZE;
    for (u64 i = 0; i < num_pages; i++)
        pd_fb[i] = (first_page + i) << 21 | PAGE_NX | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
    // Clear frambuffer to black
    memset(framebuffer, 0x00, fb_height * fb_pitch);
}

u32 get_framebuffer_width(void) {
    return (u32)fb_width;
}

u32 get_framebuffer_height(void) {
    return (u32)fb_height;
}

static spinlock_t fb_lock;

void framebuffer_lock(void) {
    spinlock_acquire(&fb_lock);
}

void framebuffer_unlock(void) {
    spinlock_release(&fb_lock);
}

// X position in characters to print the next character at
// There is no Y position because characters are always printed at the bottom of the screen.
static u32 cursor_x = 0;

void print_newline(void) {
    // Scroll screen upwards by FONT_HEIGHT pixels
    for (u32 y = 0; y < fb_height - FONT_HEIGHT; y++)
        memcpy(framebuffer + y * fb_pitch, framebuffer + (y + FONT_HEIGHT) * fb_pitch, fb_width * fb_bytes_per_pixel);
    // Fill the new line with black
    memset(framebuffer + (fb_height - FONT_HEIGHT) * fb_pitch, 0x00, FONT_HEIGHT * fb_pitch);
    // Move the cursor to the start
    cursor_x = 0;
}

void print_char(char c) {
    if (c == '\n') {
        print_newline();
    } else {
        if (FONT_WIDTH * (cursor_x + 1) >= fb_width) {
            // If we're past the end of the line, move to a new one
            print_newline();
        }
        // If the character is valid, print it
        if (FONT_CHAR_LOWEST <= c && c <= FONT_CHAR_HIGHEST) {
            for (size_t y = 0; y < FONT_HEIGHT; y++) {
                for (size_t x = 0; x < FONT_WIDTH; x++) {
                    for (size_t i = 0; i < fb_bytes_per_pixel; i++) {
                        size_t fb_x = fb_height - FONT_HEIGHT + y;
                        size_t fb_y = (cursor_x * FONT_WIDTH + x) * fb_bytes_per_pixel + i;
                        u8 color_byte = (font_chars[c - FONT_CHAR_LOWEST][y] << x) & 0x80 ? 0xFF : 0x00;
                        framebuffer[fb_x * fb_pitch + fb_y] = color_byte;
                    }
                }
            }
        }
        // Move cursor into position for the next character
        cursor_x += 1;
    }
}

void print_string(const char *str) {
    for (const char *c = str; *c != '\0'; c++)
        print_char(*c);
}

// Print the last `digits` digits of a number in hexadecimal
static void print_hex(u64 n, u64 digits) {
    print_char('0');
    print_char('x');
    for (u64 i = 0; i < digits; i++) {
        u64 digit = (n >> (4 * (digits - 1 - i))) & 0xF;
        print_char(digit < 10 ? digit + '0' : digit - 10 + 'A');
    }
}

void print_hex_u64(u64 n) {
    print_hex(n, 16);
}

void print_hex_u32(u32 n) {
    print_hex((u64)n, 8);
}

void print_hex_u16(u16 n) {
    print_hex((u64)n, 4);
}

void print_hex_u8(u8 n) {
    print_hex((u64)n, 2);
}

Channel *framebuffer_data_channel;
Channel *framebuffer_size_channel;
MessageQueue *framebuffer_mqueue;

void framebuffer_fast_copy_32_bit(void *screen, const void *data);

_Noreturn void framebuffer_kernel_thread_main(void) {
    ScreenSize screen_size = {fb_width, fb_height};
    size_t i = 0;
    while (1) {
        i++;
        Message *message;
        // Get message from framebuffer meessage queue
        mqueue_receive(framebuffer_mqueue, &message, false);
        switch (message->tag.data[0]) {
        case FB_MQ_TAG_DATA: {
            // Check message size
            if (message->data_size != fb_height * fb_width * 3) {
                message_reply_error(message, ERR_INVALID_ARG);
                message_free(message);
                continue;
            }
            // Display the contents of the message
            // Use fast copy if it's available
            framebuffer_lock();
            if (fb_fast_copy) {
                framebuffer_fast_copy_32_bit(framebuffer, message->data);
            } else {
                for (size_t y = 0; y < fb_height; y++) {
                    for (size_t x = 0; x < fb_width; x++) {
                        u8 *pixel = &message->data[(y * fb_width + x) * 3];
                        u32 color = ((pixel[0] >> r_cut) << r_pos) | ((pixel[1] >> g_cut) << g_pos) | ((pixel[2] >> b_cut) << b_pos);
                        for (u8 i = 0; i < fb_bytes_per_pixel; i++)
                            framebuffer[y * fb_pitch + x * fb_bytes_per_pixel + i] = (u8)(color >> (8 * i));
                    }
                }
            }
            // Print the frame counter
            cursor_x = 0;
            print_hex_u64(i);
            framebuffer_unlock();
            message_free(message);
            break;
        }
        case FB_MQ_TAG_SIZE: {
            // Check message size
            if (message->data_size != 0) {
                message_reply_error(message, ERR_INVALID_ARG);
                message_free(message);
                continue;
            }
            // Request for screen size
            Message *reply = message_alloc_copy(sizeof(ScreenSize), &screen_size);
            if (reply == NULL) {
                message_reply_error(message, ERR_NO_MEMORY);
                message_free(message);
                continue;
            }
            message_reply(message, reply);
            message_free(message);
            break;
        }
        }
    }
}
