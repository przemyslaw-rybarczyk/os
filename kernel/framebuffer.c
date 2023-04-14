#include "types.h"
#include "framebuffer.h"

#include "page.h"
#include "spinlock.h"
#include "string.h"

#include "font.h"

#define FB_PML4E 0x1FDull

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
static u16 fb_pitch;
static u16 fb_width;
static u16 fb_height;
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

// Set the color of pixel at (x,y) to (r,g,b)
void put_pixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (x >= fb_width || y >= fb_height)
        return;
    u32 color = ((r >> r_cut) << r_pos) | ((g >> g_cut) << g_pos) | ((b >> b_cut) << b_pos);
    for (u8 i = 0; i < fb_bytes_per_pixel; i++)
        framebuffer[y * fb_pitch + x * fb_bytes_per_pixel + i] = (u8)(color >> (8 * i));
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

Channel *stdout_channel;

// This thread reads messages from the stdout channel and prints their contents
_Noreturn void stdout_kernel_thread_main(void) {
    err_t err;
    while (1) {
        Message *message;
        // Get message from stdout channel
        err = channel_receive(stdout_channel, &message);
        if (err)
            continue;
        // Print the contents of the message
        framebuffer_lock();
        for (size_t i = 0; i < message->data_size; i++)
            print_char(message->data[i]);
        framebuffer_unlock();
        // Send an empty reply
        Message *reply = message_alloc(0, NULL);
        if (reply == NULL)
            continue;
        message_reply(message, reply);
    }
}
