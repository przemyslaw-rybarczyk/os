#include "types.h"
#include "framebuffer.h"

#include "page.h"

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
    u64 fb_virt_addr = (0xFFFFull << 48) | (FB_PML4E << 39) | (fb_phys_addr & 0x1FFFFFF);
    framebuffer = (u8 *)fb_virt_addr;
    u64 *pde_fb = PDE_PTR(fb_virt_addr);
    u64 first_page = fb_phys_addr >> 21;
    u64 last_page = (fb_phys_addr + fb_pitch * fb_height - 1) >> 21;
    u64 num_pages = last_page - first_page + 1;
    // Make sure mapping fits in 1 GiB, although the frambuffer shouldn't ever be this large
    if (num_pages > 0x200)
        num_pages = 0x200;
    for (u64 i = 0; i < num_pages; i++)
        pde_fb[i] = (first_page + i) << 21 | PAGE_NX | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
}

u32 get_framebuffer_width(void) {
    return (u32)fb_width;
}

u32 get_framebuffer_height(void) {
    return (u32)fb_height;
}

// Set the color of pixel at (x,y) to (r,g,b)
void put_pixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (x >= fb_width || y >= fb_height)
        return;
    u32 color = ((r >> r_cut) << r_pos) | ((g >> g_cut) << g_pos) | ((b >> b_cut) << b_pos);
    for (u8 i = 0; i < fb_bytes_per_pixel; i++)
        framebuffer[y * fb_pitch + x * fb_bytes_per_pixel + i] = (u8)(color >> (8 * i));
}
