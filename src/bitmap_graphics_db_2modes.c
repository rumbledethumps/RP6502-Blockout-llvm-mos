// Optimize for size to fit in Release builds
#pragma GCC optimize ("Os")

#include <rp6502.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "font5x7.h"
#include "bitmap_graphics_db.h"

// ---------------------------------------------------------------------------
// Multi-plane support: Track up to 3 independent graphics planes
// Each plane can have different position, size, and buffer
// ---------------------------------------------------------------------------
#define MAX_PLANES 3

typedef struct {
    uint16_t struct_addr;
    uint16_t width;
    uint16_t height;
    uint16_t x_pos;
    uint16_t y_pos;
    uint8_t bpp_mode;
    uint16_t bytes_per_row;
    uint16_t row_offsets[480];  // Support up to 480 rows
    bool initialized;
} PlaneConfig;

static PlaneConfig planes[MAX_PLANES];

// Text rendering state (shared across all planes)
static uint8_t textmultiplier = 1;
static uint16_t textcolor = 15;
static uint16_t textbgcolor = 15;
static bool wrap = true;
static uint16_t cursor_x = 0;
static uint16_t cursor_y = 0;

// Helper to find plane by struct address
static PlaneConfig* get_plane_by_struct(uint16_t struct_addr) {
    for (int i = 0; i < MAX_PLANES; i++) {
        if (planes[i].initialized && planes[i].struct_addr == struct_addr) {
            return &planes[i];
        }
    }
    return NULL;
}

// Helper to precompute row offsets for a specific plane
static void precompute_plane_offsets(PlaneConfig* plane) {
    uint16_t addr = 0;
    
    // Calculate bytes per row based on width and bpp_mode
    if (plane->bpp_mode == 0) {        // 1bpp
        plane->bytes_per_row = plane->width >> 3;
    } else if (plane->bpp_mode == 2) { // 4bpp
        plane->bytes_per_row = plane->width >> 1;
    } else if (plane->bpp_mode == 3) { // 8bpp
        plane->bytes_per_row = plane->width;
    } else {
        plane->bytes_per_row = (plane->width * (1 << plane->bpp_mode)) >> 3;
    }
    
    for(int i = 0; i < 480; i++) {
        plane->row_offsets[i] = addr;
        addr += plane->bytes_per_row; 
    }
}

uint16_t random(uint16_t low_limit, uint16_t high_limit)
{
    if (low_limit > high_limit) {
        swap(low_limit, high_limit);
    }
    return (uint16_t)((rand() % (high_limit-low_limit)) + low_limit);
}

// ---------------------------------------------------------------------------
// Initialize a graphics plane with position and size
// ---------------------------------------------------------------------------
void init_graphics_plane(uint16_t canvas_struct_address,
                         uint16_t canvas_data_address,
                         uint8_t  canvas_plane,
                         uint16_t x_position,
                         uint16_t y_position,
                         uint16_t canvas_width,
                         uint16_t canvas_height,
                         uint8_t  bits_per_pixel)
{
    if (canvas_plane >= MAX_PLANES) return;
    
    PlaneConfig* plane = &planes[canvas_plane];
    
    plane->struct_addr = canvas_struct_address;
    plane->width = canvas_width;
    plane->height = canvas_height;
    plane->x_pos = x_position;
    plane->y_pos = y_position;
    
    // Convert bits_per_pixel to mode
    if (bits_per_pixel == 1) plane->bpp_mode = 0;
    else if (bits_per_pixel == 2) plane->bpp_mode = 1;
    else if (bits_per_pixel == 4) plane->bpp_mode = 2;
    else if (bits_per_pixel == 8) plane->bpp_mode = 3;
    else plane->bpp_mode = 4; // 16 bit

    // Precompute row offsets for this plane
    precompute_plane_offsets(plane);
    plane->initialized = true;

    // Initialize Video Hardware
    // Note: Canvas type is determined by the display mode, typically:
    // Type 1 = 320x240, Type 2 = 320x180, Type 4 = 640x360
    // Only set canvas type once (use the first initialized plane's resolution)
    static bool canvas_type_set = false;
    if (!canvas_type_set) {
        uint8_t canvas_type = 2; // Default to 320x180
        if (canvas_width == 320 && canvas_height == 240) canvas_type = 1;
        else if (canvas_width == 640 && canvas_height == 360) canvas_type = 4;
        xregn(1, 0, 0, 1, canvas_type);
        canvas_type_set = true;
    }

    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, x_wrap, false);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, y_wrap, false);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, x_pos_px, x_position);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, y_pos_px, y_position);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, width_px, canvas_width);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, height_px, canvas_height);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, xram_data_ptr, canvas_data_address);
    xram0_struct_set(plane->struct_addr, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    // Set Video Mode for this plane
    xregn(1, 0, 1, 4, 3, plane->bpp_mode, plane->struct_addr, canvas_plane);
}

// Backward compatibility: original init function
void init_bitmap_graphics(uint16_t canvas_struct_address,
                          uint16_t canvas_data_address,
                          uint8_t  canvas_plane,
                          uint8_t  canvas_type,
                          uint16_t canvas_width,
                          uint16_t canvas_height,
                          uint8_t  bits_per_pixel)
{
    init_graphics_plane(canvas_struct_address, canvas_data_address, 
                       canvas_plane, 0, 0, canvas_width, canvas_height, bits_per_pixel);
}

// ---------------------------------------------------------------------------
// Switch buffer for a specific plane
// ---------------------------------------------------------------------------
void switch_buffer_plane(uint16_t canvas_struct_address, uint16_t buffer_data_address)
{
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, xram_data_ptr, buffer_data_address);
}

// Backward compatibility
void switch_buffer(uint16_t buffer_data_address)
{
    if (planes[0].initialized) {
        switch_buffer_plane(planes[0].struct_addr, buffer_data_address);
    }
}

// ---------------------------------------------------------------------------
// Helper to infer plane from buffer address
// Uses simple heuristic: buffers are typically grouped by plane
// ---------------------------------------------------------------------------
static PlaneConfig* infer_plane_from_buffer(uint16_t buffer_addr) {
    // Strategy: Check if buffer address is "close" to a known plane's initial buffer
    // Plane 1 (static) typically uses low addresses like 0x0000
    // Plane 0 (viewport) typically uses higher addresses like 0x7200+
    
    // First check if we have multiple planes initialized
    int initialized_count = 0;
    for (int i = 0; i < MAX_PLANES; i++) {
        if (planes[i].initialized) initialized_count++;
    }
    
    // If only one plane, use it
    if (initialized_count == 1) {
        for (int i = 0; i < MAX_PLANES; i++) {
            if (planes[i].initialized) return &planes[i];
        }
    }
    
    // Multiple planes: use heuristic based on buffer address ranges
    // Plane 1 (background): 0x0000 - 0x7000
    // Plane 0 (viewport):   0x7000+
    if (buffer_addr < 0x7000) {
        if (planes[0].initialized) return &planes[0];
    } else {
        if (planes[1].initialized) return &planes[1];
    }
    
    // Fallback
    for (int i = 0; i < MAX_PLANES; i++) {
        if (planes[i].initialized) return &planes[i];
    }
    return NULL;
}

void erase_buffer(uint16_t buffer_data_address) __attribute__((noinline));
void erase_buffer_sized(uint16_t buffer_data_address, uint16_t width, uint16_t height, uint8_t bpp) __attribute__((noinline));
void draw_pixel2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_data_address) __attribute__((noinline));
void draw_line2buffer(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_data_address) __attribute__((noinline));

// ---------------------------------------------------------------------------
// Erase buffer with explicit size (for viewports)
// ---------------------------------------------------------------------------
void erase_buffer_sized(uint16_t buffer_data_address, uint16_t width, uint16_t height, uint8_t bpp)
{
    uint32_t total_bytes;
    uint16_t bytes_per_row_local;
    
    // Calculate bytes per row
    if (bpp == 1) bytes_per_row_local = width >> 3;
    else if (bpp == 4) bytes_per_row_local = width >> 1;
    else if (bpp == 8) bytes_per_row_local = width;
    else bytes_per_row_local = (width * bpp) >> 3;
    
    total_bytes = (uint32_t)bytes_per_row_local * height;
    
    RIA.addr0 = buffer_data_address;
    RIA.step0 = 1;

    uint16_t chunks = total_bytes >> 5;
    for (uint16_t i = 0; i < chunks; i++) {
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
    }
    
    uint16_t remaining = total_bytes & 31;
    for (uint16_t i = 0; i < remaining; i++) {
        RIA.rw0 = 0;
    }
}

// Backward compatibility: erase using plane 0 settings
void erase_buffer(uint16_t buffer_data_address)
{
    if (planes[0].initialized) {
        uint8_t bpp = (planes[0].bpp_mode == 0) ? 1 : (planes[0].bpp_mode == 2) ? 4 : 8;
        erase_buffer_sized(buffer_data_address, planes[0].width, planes[0].height, bpp);
    }
}

// ---------------------------------------------------------------------------
// Draw pixel - uses plane 0 by default (for backward compatibility)
// ---------------------------------------------------------------------------
void draw_pixel2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    // Bounds check
    if (x >= plane->width || y >= plane->height) return;

    if (plane->bpp_mode == 0) { // 1bpp
        uint16_t addr = buffer_data_address + plane->row_offsets[y] + (x >> 3);
        uint8_t bitmask = 0x80 >> (x & 7);
        
        RIA.addr0 = addr;
        RIA.step0 = 0;
        uint8_t val = RIA.rw0;
        
        if (color) val |= bitmask;
        else val &= ~bitmask;
        
        RIA.addr0 = addr;
        RIA.rw0 = val;
        
    } else if (plane->bpp_mode == 2) { // 4bpp
        uint8_t shift = 4 * (1 - (x & 1));
        uint16_t addr = buffer_data_address + plane->row_offsets[y] + (x >> 1);
        
        RIA.addr0 = addr;
        RIA.step0 = 0;
        uint8_t val = RIA.rw0;
        RIA.addr0 = addr;
        RIA.rw0 = (val & ~(15 << shift)) | ((color & 15) << shift);
    }
}

// ---------------------------------------------------------------------------
// Draw line - uses plane 0 by default
// ---------------------------------------------------------------------------
void draw_line2buffer(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_addr)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_addr);
    if (!plane) return;
    
    int16_t dx, dy, sx, sy, err, e2;
    uint16_t addr;
    uint8_t bitmask, val, shift;

    // Bounds check
    if (x0 < 0) x0 = 0; if (x0 >= plane->width) x0 = plane->width - 1;
    if (y0 < 0) y0 = 0; if (y0 >= plane->height) y0 = plane->height - 1;
    if (x1 < 0) x1 = 0; if (x1 >= plane->width) x1 = plane->width - 1;
    if (y1 < 0) y1 = 0; if (y1 >= plane->height) y1 = plane->height - 1;

    dx = abs(x1 - x0);
    dy = abs(y1 - y0);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1; 
    err = (dx > dy ? dx : -dy) / 2;

    RIA.step0 = 0;

    if (plane->bpp_mode == 0) { // 1bpp
        while(true) {
            addr = buffer_addr + plane->row_offsets[y0] + (x0 >> 3);
            bitmask = 0x80 >> (x0 & 7);
            
            RIA.addr0 = addr;
            val = RIA.rw0;
            
            if (color) val |= bitmask;
            else val &= ~bitmask;
            
            RIA.addr0 = addr;
            RIA.rw0 = val;

            if (x0 == x1 && y0 == y1) break;
            e2 = err;
            if (e2 > -dx) { err -= dy; x0 += sx; }
            if (e2 < dy)  { err += dx; y0 += sy; }
        }
    } else if (plane->bpp_mode == 2) { // 4bpp
        uint8_t color_nibble = color & 15;
        while(true) {
            shift = 4 * (1 - (x0 & 1));
            addr = buffer_addr + plane->row_offsets[y0] + (x0 >> 1);
            
            RIA.addr0 = addr;
            val = RIA.rw0;
            RIA.addr0 = addr;
            RIA.rw0 = (val & ~(15 << shift)) | (color_nibble << shift);

            if (x0 == x1 && y0 == y1) break;
            e2 = err;
            if (e2 > -dx) { err -= dy; x0 += sx; }
            if (e2 < dy)  { err += dx; y0 += sy; }
        }
    }
}

void draw_vline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t h, uint16_t buffer_data_address)
{
    for (uint16_t i=y; i<(y+h); i++) {
        draw_pixel2buffer(color, x, i, buffer_data_address);
    }
}

void draw_hline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t buffer_data_address)
{
    for (uint16_t i=x; i<(x+w); i++) {
        draw_pixel2buffer(color, i, y, buffer_data_address);
    }
}

void draw_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t buffer_data_address)
{
    draw_hline2buffer(color, x, y, w, buffer_data_address);
    draw_hline2buffer(color, x, y+h-1, w, buffer_data_address);
    draw_vline2buffer(color, x, y, h, buffer_data_address);
    draw_vline2buffer(color, x+w-1, y, h, buffer_data_address);
}

void fill_rect2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t buffer_data_address)
{
    for(uint16_t i=x; i<(x+w); i++) {
        for(uint16_t j=y; j<(y+h); j++) {
            draw_pixel2buffer(color, i, j, buffer_data_address);
        }
    }
}

void draw_circle2buffer(uint16_t color, uint16_t x0, uint16_t y0, uint16_t r, uint16_t buffer_data_address)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    draw_pixel2buffer(color, x0  , y0+r, buffer_data_address);
    draw_pixel2buffer(color, x0  , y0-r, buffer_data_address);
    draw_pixel2buffer(color, x0+r, y0  , buffer_data_address);
    draw_pixel2buffer(color, x0-r, y0  , buffer_data_address);

    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }

        x++;
        ddF_x += 2;
        f += ddF_x;

        draw_pixel2buffer(color, x0 + x, y0 + y, buffer_data_address);
        draw_pixel2buffer(color, x0 - x, y0 + y, buffer_data_address);
        draw_pixel2buffer(color, x0 + x, y0 - y, buffer_data_address);
        draw_pixel2buffer(color, x0 - x, y0 - y, buffer_data_address);
        draw_pixel2buffer(color, x0 + y, y0 + x, buffer_data_address);
        draw_pixel2buffer(color, x0 - y, y0 + x, buffer_data_address);
        draw_pixel2buffer(color, x0 + y, y0 - x, buffer_data_address);
        draw_pixel2buffer(color, x0 - y, y0 - x, buffer_data_address);
    }
}

// ---------------------------------------------------------------------------
// Text functions
// ---------------------------------------------------------------------------
void set_text_multiplier(uint8_t mult)
{
    textmultiplier = (mult > 0) ? mult : 1;
}

void set_cursor(uint16_t x, uint16_t y) 
{ 
    cursor_x = x; 
    cursor_y = y; 
}

void draw_char2buffer(char chr, uint16_t x, uint16_t y, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    if((x >= plane->width) || (y >= plane->height)) return;

    for (uint8_t i=0; i<6; i++) {
        uint8_t line = (i == 5) ? 0x0 : pgm_read_byte(font+(chr*5)+i);

        for (uint8_t j = 0; j<8; j++) {
            if (line & 0x1) {
                if (textmultiplier == 1) {
                    draw_pixel2buffer(textcolor, x+i, y+j, buffer_data_address);
                } else {
                    fill_rect2buffer(textcolor, x+(i*textmultiplier), y+(j*textmultiplier), 
                                   textmultiplier, textmultiplier, buffer_data_address);
                }
            } else if (textbgcolor != textcolor) {
                if (textmultiplier == 1) {
                    draw_pixel2buffer(textbgcolor, x+i, y+j, buffer_data_address);
                } else {
                    fill_rect2buffer(textbgcolor, x+(i*textmultiplier), y+(j*textmultiplier), 
                                   textmultiplier, textmultiplier, buffer_data_address);
                }
            }
            line >>= 1;
        }
    }
}

static void draw_char_at_cursor2buffer(char chr, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    if (chr == '\n') {
        cursor_y += textmultiplier*8;
        cursor_x = 0;
    } else if (chr == '\r') {
        // skip
    } else if (chr == '\t') {
        uint16_t new_x = cursor_x + TABSPACE;
        if (new_x < plane->width) cursor_x = new_x;
    } else {
        draw_char2buffer(chr, cursor_x, cursor_y, buffer_data_address);
        cursor_x += textmultiplier*6;

        if (wrap && (cursor_x > (plane->width - textmultiplier*6))) {
            cursor_y += textmultiplier*8;
            cursor_x = 0;
        }
    }
}
void draw_string2buffer(const char *str, uint16_t buffer_data_address){
    while (*str) {
        draw_char_at_cursor2buffer(*str++, buffer_data_address);
    }
}