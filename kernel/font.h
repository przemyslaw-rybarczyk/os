#include <zr/types.h>

// This file contains data for the system font.
// The font used is misc-fixed 7x13 (https://www.cl.cam.ac.uk/~mgk25/ucs-fonts.html).
// The font is in the public domain.
// Each character between FONT_CHAR_LOWEST and FONT_CHAR_HIGHEST inclusive is respresented as an array of bytes.
// The pixel at coordinates (x, y) is represented as the x-th highest bit of the y-th byte.
// The lowest bit is unused.

#define FONT_HEIGHT 13
#define FONT_WIDTH 7

#define FONT_CHAR_LOWEST ' '
#define FONT_CHAR_HIGHEST '~'

static const u8 font_chars[FONT_CHAR_HIGHEST - FONT_CHAR_LOWEST + 1][FONT_HEIGHT] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00, 0x00},
    {0x00, 0x00, 0x28, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x28, 0x28, 0x7C, 0x28, 0x7C, 0x28, 0x28, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x10, 0x3C, 0x50, 0x38, 0x14, 0x78, 0x10, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x44, 0xA4, 0x48, 0x10, 0x10, 0x20, 0x48, 0x94, 0x88, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x60, 0x90, 0x90, 0x60, 0x94, 0x88, 0x74, 0x00, 0x00},
    {0x00, 0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x08, 0x10, 0x10, 0x20, 0x20, 0x20, 0x10, 0x10, 0x08, 0x00, 0x00},
    {0x00, 0x00, 0x20, 0x10, 0x10, 0x08, 0x08, 0x08, 0x10, 0x10, 0x20, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x48, 0x30, 0xFC, 0x30, 0x48, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x30, 0x40, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x10, 0x00},
    {0x00, 0x00, 0x04, 0x04, 0x08, 0x08, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00, 0x00},
    {0x00, 0x00, 0x30, 0x48, 0x84, 0x84, 0x84, 0x84, 0x84, 0x48, 0x30, 0x00, 0x00},
    {0x00, 0x00, 0x10, 0x30, 0x50, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x04, 0x08, 0x30, 0x40, 0x80, 0xFC, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x04, 0x08, 0x10, 0x38, 0x04, 0x04, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x08, 0x18, 0x28, 0x48, 0x88, 0x88, 0xFC, 0x08, 0x08, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x80, 0x80, 0xB8, 0xC4, 0x04, 0x04, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x38, 0x40, 0x80, 0x80, 0xB8, 0xC4, 0x84, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x04, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x78, 0x84, 0x84, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x8C, 0x74, 0x04, 0x04, 0x08, 0x70, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x10, 0x00, 0x00, 0x10, 0x38, 0x10, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x10, 0x00, 0x00, 0x38, 0x30, 0x40, 0x00},
    {0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x04, 0x08, 0x10, 0x10, 0x00, 0x10, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x9C, 0xA4, 0xAC, 0x94, 0x80, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x30, 0x48, 0x84, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0xF8, 0x44, 0x44, 0x44, 0x78, 0x44, 0x44, 0x44, 0xF8, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x80, 0x80, 0x80, 0x80, 0x80, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0xF8, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0xF8, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x80, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80, 0xFC, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x80, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x80, 0x80, 0x80, 0x9C, 0x84, 0x8C, 0x74, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00},
    {0x00, 0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x88, 0x70, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFC, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0xCC, 0xCC, 0xB4, 0xB4, 0x84, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0xC4, 0xA4, 0x94, 0x8C, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0xF8, 0x84, 0x84, 0x84, 0xF8, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x84, 0x84, 0xA4, 0x94, 0x78, 0x04, 0x00},
    {0x00, 0x00, 0xF8, 0x84, 0x84, 0x84, 0xF8, 0xA0, 0x90, 0x88, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x78, 0x84, 0x80, 0x80, 0x78, 0x04, 0x04, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0x84, 0x48, 0x48, 0x48, 0x30, 0x30, 0x30, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0x84, 0x84, 0xB4, 0xB4, 0xCC, 0xCC, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x84, 0x84, 0x48, 0x48, 0x30, 0x48, 0x48, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x44, 0x44, 0x28, 0x28, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00},
    {0x00, 0x00, 0xFC, 0x04, 0x08, 0x10, 0x30, 0x20, 0x40, 0x80, 0xFC, 0x00, 0x00},
    {0x00, 0x78, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x78, 0x00},
    {0x00, 0x00, 0x40, 0x40, 0x20, 0x20, 0x10, 0x08, 0x08, 0x04, 0x04, 0x00, 0x00},
    {0x00, 0x78, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x78, 0x00},
    {0x00, 0x00, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00},
    {0x00, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x04, 0x7C, 0x84, 0x8C, 0x74, 0x00, 0x00},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0xB8, 0xC4, 0x84, 0x84, 0xC4, 0xB8, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x84, 0x80, 0x80, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x04, 0x04, 0x04, 0x74, 0x8C, 0x84, 0x84, 0x8C, 0x74, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x84, 0xFC, 0x80, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x38, 0x44, 0x40, 0x40, 0xF0, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x88, 0x88, 0x70, 0x80, 0x78, 0x84, 0x78},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0xB8, 0xC4, 0x84, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38},
    {0x00, 0x00, 0x80, 0x80, 0x80, 0x88, 0x90, 0xE0, 0x90, 0x88, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x54, 0x54, 0x54, 0x54, 0x44, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xB8, 0xC4, 0x84, 0x84, 0x84, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xB8, 0xC4, 0x84, 0xC4, 0xB8, 0x80, 0x80, 0x80},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x8C, 0x84, 0x8C, 0x74, 0x04, 0x04, 0x04},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x44, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x84, 0x60, 0x18, 0x84, 0x78, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x40, 0x40, 0xF0, 0x40, 0x40, 0x40, 0x44, 0x38, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x84, 0x84, 0x84, 0x8C, 0x74, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x44, 0x28, 0x28, 0x10, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x54, 0x54, 0x54, 0x28, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x48, 0x30, 0x30, 0x48, 0x84, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x84, 0x84, 0x8C, 0x74, 0x04, 0x84, 0x78},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x08, 0x10, 0x20, 0x40, 0xFC, 0x00, 0x00},
    {0x00, 0x1C, 0x20, 0x20, 0x20, 0x10, 0x60, 0x10, 0x20, 0x20, 0x20, 0x1C, 0x00},
    {0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00},
    {0x00, 0x70, 0x08, 0x08, 0x08, 0x10, 0x0C, 0x10, 0x08, 0x08, 0x08, 0x70, 0x00},
    {0x00, 0x00, 0x24, 0x54, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static const u8 font_char_unknown[FONT_HEIGHT] =
    {0x00, 0x00, 0xFC, 0x84, 0xB4, 0x8C, 0x94, 0x84, 0x94, 0x84, 0xFC, 0x00, 0x00};

#ifndef _KERNEL

static void draw_font_char(u8 c, size_t x, size_t y, const u8 *color, size_t width, size_t height, u8 *screen) {
    // Get the font glyph for the character
    const u8 *font_char;
    if (FONT_CHAR_LOWEST <= c && c <= FONT_CHAR_HIGHEST)
        font_char = font_chars[c - FONT_CHAR_LOWEST];
    else
        font_char = font_char_unknown;
    // Draw the character
    for (u32 cy = 0; cy < FONT_HEIGHT; cy++) {
        if (y + cy >= height)
            break;
        for (u32 cx = 0; cx < FONT_WIDTH; cx++) {
            if (x + cx >= width)
                break;
            if ((font_char[cy] << cx) & 0x80) {
                u8 *pixel = &screen[((y + cy) * width + x + cx) * 3];
                pixel[0] = color[0];
                pixel[1] = color[1];
                pixel[2] = color[2];
            }
        }
    }
}

#endif
