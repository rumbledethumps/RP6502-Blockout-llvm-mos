// ---------------------------------------------------------------------------
// bitmap_graphics_db.h
//
// Multi-plane graphics library for RP6502 picocomputer
// Supports independent graphics planes with different positions and sizes
// ---------------------------------------------------------------------------

#ifndef BITMAP_GRAPHICS_DB_H
#define BITMAP_GRAPHICS_DB_H

#include <stdbool.h>
#include <stdint.h>

#define swap(a, b) { int16_t t = a; a = b; b = t; }

// For writing text
#define TABSPACE 4

// For accessing the font library
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))

// ---------------------------------------------------------------------------
// NEW: Multi-plane initialization
// Allows positioning and sizing a graphics plane anywhere on screen
// ---------------------------------------------------------------------------
void init_graphics_plane(uint16_t canvas_struct_address,
                         uint16_t canvas_data_address,
                         uint8_t  canvas_plane,
                         uint16_t x_position,
                         uint16_t y_position,
                         uint16_t canvas_width,
                         uint16_t canvas_height,
                         uint8_t  bits_per_pixel);

// ---------------------------------------------------------------------------
// Backward compatible: Original full-screen initialization
// ---------------------------------------------------------------------------
void init_bitmap_graphics(uint16_t canvas_struct_address,
                          uint16_t canvas_data_address,
                          uint8_t  canvas_plane,
                          uint8_t  canvas_type,
                          uint16_t canvas_width,
                          uint16_t canvas_height,
                          uint8_t  bits_per_pixel);

// ---------------------------------------------------------------------------
// Buffer management
// ---------------------------------------------------------------------------
void switch_buffer(uint16_t buffer_data_address);
void switch_buffer_plane(uint16_t canvas_struct_address, uint16_t buffer_data_address);

void erase_buffer(uint16_t buffer_data_address);
void erase_buffer_sized(uint16_t buffer_data_address, uint16_t width, uint16_t height, uint8_t bpp);

// ---------------------------------------------------------------------------
// Drawing primitives (auto-detect plane from buffer address)
// ---------------------------------------------------------------------------
void draw_pixel2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_data_address);
void draw_line2buffer(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_data_address);
void draw_vline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t h, uint16_t buffer_data_address);
void draw_hline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t buffer_data_address);
void draw_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t buffer_data_address);
void fill_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t buffer_data_address);
void draw_circle2buffer(uint16_t color, uint16_t x0, uint16_t y0, uint16_t r, uint16_t buffer_data_address);
void fill_circle2buffer(uint16_t color, uint16_t x0, uint16_t y0, uint16_t r, uint16_t buffer_data_address);
void draw_rounded_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t buffer_data_address);
void fill_rounded_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t buffer_data_address);

// ---------------------------------------------------------------------------
// Explicit plane-aware drawing (for better control)
// ---------------------------------------------------------------------------
void draw_pixel2plane(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_data_address, uint8_t plane_num);
void draw_line2plane(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_data_address, uint8_t plane_num);

// ---------------------------------------------------------------------------
// Text rendering
// ---------------------------------------------------------------------------
void set_cursor(uint16_t x, uint16_t y);
void set_text_multiplier(uint8_t mult);
void set_text_color(uint16_t color);
void set_text_colors(uint16_t color, uint16_t background);
void set_text_wrap(bool w);
void draw_char2buffer(char chr, uint16_t x, uint16_t y, uint16_t buffer_data_address);
void draw_string2buffer(const char *str, uint16_t buffer_data_address);

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
uint16_t random(uint16_t low_limit, uint16_t high_limit);
uint16_t canvas_width(void);
uint16_t canvas_height(void);
uint8_t bits_per_pixel(void);

#endif // BITMAP_GRAPHICS_DB_H