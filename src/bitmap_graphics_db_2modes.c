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

static inline uint16_t get_row_offset(const PlaneConfig* plane, uint16_t y) {
    return (uint16_t)((uint32_t)plane->bytes_per_row * y);
}

// Helper to precompute row offsets for a specific plane
static void precompute_plane_offsets(PlaneConfig* plane) {
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

void set_plane_position(uint16_t canvas_struct_address, uint16_t x_position, uint16_t y_position)
{
    PlaneConfig* plane = get_plane_by_struct(canvas_struct_address);
    if (!plane) return;

    plane->x_pos = x_position;
    plane->y_pos = y_position;
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, x_pos_px, x_position);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, y_pos_px, y_position);
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
static PlaneConfig* last_plane_cache = NULL;
static uint16_t last_buffer_cache = 0xFFFF;

static PlaneConfig* infer_plane_from_buffer(uint16_t buffer_addr) {
    if (buffer_addr == last_buffer_cache && last_plane_cache) {
        return last_plane_cache;
    }

    PlaneConfig* found_plane = NULL;
    
    int initialized_count = 0;
    for (int i = 0; i < MAX_PLANES; i++) {
        if (planes[i].initialized) initialized_count++;
    }
    
    if (initialized_count == 1) {
        for (int i = 0; i < MAX_PLANES; i++) {
            if (planes[i].initialized) {
                found_plane = &planes[i];
                break;
            }
        }
    } else {
        // Plane 1 (background): 0x0000 - 0x7000
        // Plane 0 (viewport):   0x7000+
        if (buffer_addr < 0x7000) {
            if (planes[0].initialized) found_plane = &planes[0];
        } else {
            if (planes[1].initialized) found_plane = &planes[1];
        }
        
        // Fallback
        if (!found_plane) {
            for (int i = 0; i < MAX_PLANES; i++) {
                if (planes[i].initialized) {
                    found_plane = &planes[i];
                    break;
                }
            }
        }
    }

    if (found_plane) {
        last_buffer_cache = buffer_addr;
        last_plane_cache = found_plane;
    }
    return found_plane;
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
void draw_pixel2plane(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_addr, uint8_t plane_num)
{
    if (plane_num >= MAX_PLANES) return;
    PlaneConfig* plane = &planes[plane_num];
    if (!plane->initialized) return;
    
    if (x >= plane->width || y >= plane->height) return;

    RIA.step0 = 0;
    if (plane->bpp_mode == 0) { // 1bpp
        uint16_t addr = buffer_addr + get_row_offset(plane, y) + (x >> 3);
        uint8_t bitmask = 0x80 >> (x & 7);
        RIA.addr0 = addr;
        uint8_t val = RIA.rw0;
        if (color) val |= bitmask;
        else val &= ~bitmask;
        RIA.rw0 = val;
    } else if (plane->bpp_mode == 2) { // 4bpp
        uint8_t shift = 4 * (1 - (x & 1));
        uint16_t addr = buffer_addr + get_row_offset(plane, y) + (x >> 1);
        RIA.addr0 = addr;
        uint8_t val = RIA.rw0;
        RIA.rw0 = (val & ~(15 << shift)) | ((color & 15) << shift);
    }
}

void draw_pixel2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    draw_pixel2plane(color, x, y, buffer_data_address, (uint8_t)(plane - planes));
}

// ---------------------------------------------------------------------------
// Draw line
// ---------------------------------------------------------------------------
void draw_line2plane(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_addr, uint8_t plane_num)
{
    if (plane_num >= MAX_PLANES) return;
    PlaneConfig* plane = &planes[plane_num];
    if (!plane->initialized) return;

    if (x0 < 0) x0 = 0; if (x0 >= (int16_t)plane->width) x0 = plane->width - 1;
    if (y0 < 0) y0 = 0; if (y0 >= (int16_t)plane->height) y0 = plane->height - 1;
    if (x1 < 0) x1 = 0; if (x1 >= (int16_t)plane->width) x1 = plane->width - 1;
    if (y1 < 0) y1 = 0; if (y1 >= (int16_t)plane->height) y1 = plane->height - 1;

    if (x0 == x1) {
        int16_t h = y1 - y0;
        if (h < 0) { swap(y0, y1); h = -h; }
        draw_vline2buffer(color, x0, y0, h + 1, buffer_addr);
        return;
    }
    if (y0 == y1) {
        int16_t w = x1 - x0;
        if (w < 0) { swap(x0, x1); w = -w; }
        draw_hline2buffer(color, x0, y0, w + 1, buffer_addr);
        return;
    }

    if (plane->bpp_mode == 2) { // 4bpp Optimized
        int16_t dx = x1 - x0; if (dx < 0) dx = -dx;
        int16_t dy = y1 - y0; if (dy < 0) dy = -dy;
        int16_t sx = (x0 < x1) ? 1 : -1;
        int16_t sy = (y0 < y1) ? 1 : -1;
        
        uint16_t bytes_per_row = plane->bytes_per_row;
        uint16_t current_row_addr = buffer_addr + get_row_offset(plane, y0);
        uint8_t color_nibble = color & 0x0F;
        
        // Pre-compute masks for both pixel positions
        uint8_t mask_high = 0x0F;  // Mask for high nibble (even x)
        uint8_t mask_low = 0xF0;   // Mask for low nibble (odd x)
        
        if (dx >= dy) { // X is major axis
            int16_t err = dx / 2;
            uint16_t last_byte_addr = 0xFFFF;
            uint8_t last_byte_val = 0;
            bool have_cached_byte = false;
            
            for (; ; x0 += sx) {
                uint16_t byte_addr = current_row_addr + (x0 >> 1);
                uint8_t is_odd = x0 & 1;
                
                // Check if we can use cached byte (same address)
                if (byte_addr == last_byte_addr && have_cached_byte) {
                    // Reuse cached value
                    if (is_odd) {
                        last_byte_val = (last_byte_val & mask_low) | color_nibble;
                    } else {
                        last_byte_val = (last_byte_val & mask_high) | (color_nibble << 4);
                    }
                } else {
                    // Write previous cached byte if we have one
                    if (have_cached_byte) {
                        RIA.addr0 = last_byte_addr;
                        RIA.rw0 = last_byte_val;
                    }
                    
                    // Read new byte
                    RIA.addr0 = byte_addr;
                    RIA.step0 = 0;
                    last_byte_val = RIA.rw0;
                    
                    if (is_odd) {
                        last_byte_val = (last_byte_val & mask_low) | color_nibble;
                    } else {
                        last_byte_val = (last_byte_val & mask_high) | (color_nibble << 4);
                    }
                    
                    last_byte_addr = byte_addr;
                    have_cached_byte = true;
                }
                
                if (x0 == x1) break;
                
                err -= dy;
                if (err < 0) {
                    RIA.addr0 = last_byte_addr;
                    RIA.rw0 = last_byte_val;
                    have_cached_byte = false;
                    
                    y0 += sy;
                    if (sy > 0) current_row_addr += bytes_per_row;
                    else current_row_addr -= bytes_per_row;
                    err += dx;
                }
            }
            
            if (have_cached_byte) {
                RIA.addr0 = last_byte_addr;
                RIA.rw0 = last_byte_val;
            }
            
        } else { 
            int16_t err = dy / 2;
            uint16_t last_byte_addr = 0xFFFF;
            
            RIA.step0 = 0;
            
            for (; ; y0 += sy) {
                uint16_t byte_addr = current_row_addr + (x0 >> 1);
                uint8_t is_odd = x0 & 1;
                
                if (byte_addr != last_byte_addr) {
                    RIA.addr0 = byte_addr;
                    last_byte_addr = byte_addr;
                }
                
                uint8_t val = RIA.rw0;
                
                if (is_odd) {
                    RIA.rw0 = (val & mask_low) | color_nibble;
                } else {
                    RIA.rw0 = (val & mask_high) | (color_nibble << 4);
                }
                
                if (y0 == y1) break;
                
                err -= dx;
                if (err < 0) {
                    x0 += sx;
                    err += dy;
                }
                
                if (sy > 0) current_row_addr += bytes_per_row;
                else current_row_addr -= bytes_per_row;
            }
        }
        return;
    }

    // Fallback for other modes
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1; 
    int16_t err = (dx > dy ? dx : -dy) / 2;
    uint16_t bytes_per_row = plane->bytes_per_row;
    uint16_t row_addr = buffer_addr + get_row_offset(plane, y0);

    RIA.step0 = 0;
    if (plane->bpp_mode == 0) { // 1bpp
        while(true) {
            uint16_t addr = row_addr + (x0 >> 3);
            uint8_t bitmask = 0x80 >> (x0 & 7);
            RIA.addr0 = addr;
            uint8_t val = RIA.rw0;
            if (color) val |= bitmask;
            else val &= ~bitmask;
            RIA.rw0 = val;

            if (x0 == x1 && y0 == y1) break;
            int16_t e2 = err;
            if (e2 > -dx) { err -= dy; x0 += sx; }
            if (e2 < dy) { 
                err += dx; y0 += sy; 
                if (sy > 0) row_addr += bytes_per_row;
                else row_addr -= bytes_per_row;
            }
        }
    }
}

void draw_line2buffer(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t buffer_addr)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_addr);
    if (!plane) return;
    draw_line2plane(color, x0, y0, x1, y1, buffer_addr, (uint8_t)(plane - planes));
}

void draw_vline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t h, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    if (x >= plane->width || y >= plane->height) return;
    if (y + h > plane->height) h = plane->height - y;

    RIA.step0 = 0;
    uint16_t stride = plane->bytes_per_row;
    uint16_t addr = buffer_data_address + get_row_offset(plane, y) + (x >> (plane->bpp_mode == 0 ? 3 : 1));

    if (plane->bpp_mode == 0) { // 1bpp
        uint8_t bitmask = 0x80 >> (x & 7);
        for (uint16_t i=0; i<h; i++) {
            RIA.addr0 = addr;
            uint8_t val = RIA.rw0;
            if (color) val |= bitmask;
            else val &= ~bitmask;
            RIA.rw0 = val;
            addr += stride;
        }
    } else if (plane->bpp_mode == 2) { // 4bpp
        uint8_t shift = 4 * (1 - (x & 1));
        uint8_t color_nibble = (color & 15) << shift;
        uint8_t mask = ~(15 << shift);
        for (uint16_t i=0; i<h; i++) {
            RIA.addr0 = addr;
            uint8_t val = RIA.rw0;
            RIA.rw0 = (val & mask) | color_nibble;
            addr += stride;
        }
    } else {
        for (uint16_t i=y; i<(y+h); i++) draw_pixel2buffer(color, x, i, buffer_data_address);
    }
}

void draw_hline2buffer(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    if (x >= plane->width || y >= plane->height) return;
    if (x + w > plane->width) w = plane->width - x;

    RIA.step0 = 0;
    uint16_t addr = buffer_data_address + get_row_offset(plane, y) + (x >> (plane->bpp_mode == 0 ? 3 : 1));

    if (plane->bpp_mode == 0) { // 1bpp
        for (uint16_t i=0; i<w; i++) {
            uint8_t bitmask = 0x80 >> (x & 7);
            RIA.addr0 = addr;
            uint8_t val = RIA.rw0;
            if (color) val |= bitmask;
            else val &= ~bitmask;
            RIA.rw0 = val;
            if ((x & 7) == 7) addr++;
            x++;
        }
    } else if (plane->bpp_mode == 2) { // 4bpp
        uint8_t color_nibble = color & 15;
        for (uint16_t i=0; i<w; i++) {
            uint8_t shift = 4 * (1 - (x & 1));
            RIA.addr0 = addr;
            uint8_t val = RIA.rw0;
            RIA.rw0 = (val & ~(15 << shift)) | (color_nibble << shift);
            if (shift == 0) addr++;
            x++;
        }
    } else {
        for (uint16_t i=x; i<(x+w); i++) draw_pixel2buffer(color, i, y, buffer_data_address);
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
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;
    
    // Bounds checking
    if (x >= plane->width || y >= plane->height) return;
    if (x + w > plane->width) w = plane->width - x;
    if (y + h > plane->height) h = plane->height - y;
    
    // Optimized path for 4bpp mode
    if (plane->bpp_mode == 2) {
        uint8_t color_nibble = color & 0x0F;
        uint16_t bytes_per_row = plane->bytes_per_row;
        
        RIA.step0 = 1;
        
        for (uint16_t row = 0; row < h; row++) {
            uint16_t current_y = y + row;
            uint16_t row_base = buffer_data_address + get_row_offset(plane, current_y);
            uint16_t current_x = x;
            uint16_t remaining_pixels = w;
            
            // Handle first partial byte if x is odd
            if (current_x & 1) {
                RIA.step0 = 0;
                uint16_t addr = row_base + (current_x >> 1);
                RIA.addr0 = addr;
                uint8_t val = RIA.rw0;
                RIA.rw0 = (val & 0xF0) | color_nibble;  // Low nibble
                current_x++;
                remaining_pixels--;
                RIA.step0 = 1;
            }
            
            // Fill complete bytes (2 pixels at a time)
            uint16_t full_bytes = remaining_pixels >> 1;
            if (full_bytes > 0) {
                uint8_t fill_byte = (color_nibble << 4) | color_nibble;
                uint16_t start_addr = row_base + (current_x >> 1);
                RIA.addr0 = start_addr;
                
                // Use chunked writes for full bytes
                uint16_t chunks = full_bytes >> 5;  // 32-byte chunks
                for (uint16_t i = 0; i < chunks; i++) {
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                }
                
                // Handle remaining full bytes
                uint16_t remaining_bytes = full_bytes & 31;
                for (uint16_t i = 0; i < remaining_bytes; i++) {
                    RIA.rw0 = fill_byte;
                }
                
                current_x += full_bytes << 1;
                remaining_pixels -= full_bytes << 1;
            }
            
            // Handle last partial byte if width is odd
            if (remaining_pixels > 0) {
                RIA.step0 = 0;
                uint16_t addr = row_base + (current_x >> 1);
                RIA.addr0 = addr;
                uint8_t val = RIA.rw0;
                RIA.rw0 = (val & 0x0F) | (color_nibble << 4);  // High nibble
                RIA.step0 = 1;
            }
        }
        return;
    }
    
    // Optimized path for 1bpp mode
    if (plane->bpp_mode == 0) {
        uint8_t fill_byte = color ? 0xFF : 0x00;
        uint16_t bytes_per_row = plane->bytes_per_row;
        
        for (uint16_t row = 0; row < h; row++) {
            uint16_t current_y = y + row;
            uint16_t row_base = buffer_data_address + get_row_offset(plane, current_y);
            uint16_t current_x = x;
            uint16_t remaining_pixels = w;
            
            RIA.step0 = 0;
            
            // Handle first partial byte
            uint8_t start_bit = current_x & 7;
            if (start_bit != 0) {
                uint16_t addr = row_base + (current_x >> 3);
                RIA.addr0 = addr;
                uint8_t val = RIA.rw0;
                
                uint8_t bits_in_first = 8 - start_bit;
                if (bits_in_first > remaining_pixels) bits_in_first = remaining_pixels;
                
                uint8_t mask = ((1 << bits_in_first) - 1) << (8 - start_bit - bits_in_first);
                if (color) val |= mask;
                else val &= ~mask;
                
                RIA.rw0 = val;
                current_x += bits_in_first;
                remaining_pixels -= bits_in_first;
            }
            
            // Fill complete bytes
            uint16_t full_bytes = remaining_pixels >> 3;
            if (full_bytes > 0) {
                RIA.step0 = 1;
                uint16_t start_addr = row_base + (current_x >> 3);
                RIA.addr0 = start_addr;
                
                uint16_t chunks = full_bytes >> 5;
                for (uint16_t i = 0; i < chunks; i++) {
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                    RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte; RIA.rw0 = fill_byte;
                }
                
                uint16_t remaining_bytes = full_bytes & 31;
                for (uint16_t i = 0; i < remaining_bytes; i++) {
                    RIA.rw0 = fill_byte;
                }
                
                current_x += full_bytes << 3;
                remaining_pixels -= full_bytes << 3;
                RIA.step0 = 0;
            }
            
            // Handle last partial byte
            if (remaining_pixels > 0) {
                uint16_t addr = row_base + (current_x >> 3);
                RIA.addr0 = addr;
                uint8_t val = RIA.rw0;
                
                uint8_t mask = ((1 << remaining_pixels) - 1) << (8 - remaining_pixels);
                if (color) val |= mask;
                else val &= ~mask;
                
                RIA.rw0 = val;
            }
        }
        return;
    }
    
    // Fallback for other modes
    for (uint16_t j = y; j < (y + h); j++) {
        draw_hline2buffer(color, x, j, w, buffer_data_address);
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

// ---------------------------------------------------------------------------
// Set colors of text to be displayed.
//     For 'transparent' background, we'll set the bg
//     to the same as fg instead of using a flag
// ---------------------------------------------------------------------------
void set_text_color(uint16_t color)
{
    textcolor = textbgcolor = color;
}

// ---------------------------------------------------------------------------
// Set colors of text to be displayed
//      color = color of text
//      background = color of text background
// ---------------------------------------------------------------------------
void set_text_colors(uint16_t color, uint16_t background)
{
    textcolor   = color;
    textbgcolor = background;
}

void set_text_multiplier(uint8_t mult)
{
    textmultiplier = (mult > 0) ? mult : 1;
}

void set_cursor(uint16_t x, uint16_t y) 
{ 
    cursor_x = x; 
    cursor_y = y; 
}

static inline void draw_char2buffer_fast(char chr, uint16_t x, uint16_t y, uint16_t buffer_data_address, const PlaneConfig* plane)
{
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

void draw_char2buffer(char chr, uint16_t x, uint16_t y, uint16_t buffer_data_address)
{
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;

    draw_char2buffer_fast(chr, x, y, buffer_data_address, plane);
}

static inline void draw_char_at_cursor2buffer(char chr, uint16_t buffer_data_address, PlaneConfig* plane)
{
    if (chr == '\n') {
        cursor_y += textmultiplier*8;
        cursor_x = 0;
    } else if (chr == '\r') {
        // skip
    } else if (chr == '\t') {
        uint16_t new_x = cursor_x + TABSPACE;
        if (new_x < plane->width) cursor_x = new_x;
    } else {
        draw_char2buffer_fast(chr, cursor_x, cursor_y, buffer_data_address, plane);
        cursor_x += textmultiplier*6;

        if (wrap && (cursor_x > (plane->width - textmultiplier*6))) {
            cursor_y += textmultiplier*8;
            cursor_x = 0;
        }
    }
}
void draw_string2buffer(const char *str, uint16_t buffer_data_address){
    PlaneConfig* plane = infer_plane_from_buffer(buffer_data_address);
    if (!plane) return;

    while (*str) {
        draw_char_at_cursor2buffer(*str++, buffer_data_address, plane);
    }
}