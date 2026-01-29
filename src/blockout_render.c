#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "colors.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_render.h"
#include "blockout_state.h"
#include "bitmap_graphics_db.h"


/* ================= PIT BACKGROUND ================= */

void draw_pit_background(uint16_t buf) {
    int16_t grid_size_x = VIEWPORT_WIDTH / PIT_WIDTH;
    int16_t grid_size_y = VIEWPORT_HEIGHT / PIT_DEPTH;
    
    int16_t centerX = SCREEN_CENTER_X + VIEWPORT_X;
    int16_t centerY = SCREEN_CENTER_Y + VIEWPORT_Y;

    uint16_t zi_front = PIT_Z_START;
    uint16_t zi_back = PIT_Z_START + (PIT_HEIGHT * PIT_Z_STEP);

    // 1. Draw the rectangular "rings" for each depth level
    for (uint8_t i = 0; i <= PIT_HEIGHT; i++) {
        uint16_t zi = PIT_Z_START + (i * PIT_Z_STEP);
        if (zi > 255) zi = 255;

        int16_t x0 = apply_perspective(-WORLD_HALF_W, (uint8_t)zi) + centerX;
        int16_t y0 = apply_perspective(-WORLD_HALF_H, (uint8_t)zi) + centerY;
        int16_t x1 = apply_perspective(WORLD_HALF_W, (uint8_t)zi) + centerX;
        int16_t y1 = apply_perspective(WORLD_HALF_H, (uint8_t)zi) + centerY;

        draw_line2buffer(GREEN, x0, y0, x1, y0, buf); 
        draw_line2buffer(GREEN, x1, y0, x1, y1, buf); 
        draw_line2buffer(GREEN, x1, y1, x0, y1, buf); 
        draw_line2buffer(GREEN, x0, y1, x0, y0, buf); 
    }

    // 2. Draw the depth lines 
    for (int16_t x = -WORLD_HALF_W; x <= WORLD_HALF_W; x += grid_size_x) {
        int16_t fx = apply_perspective(x, (uint8_t)zi_front) + centerX;
        int16_t bx = apply_perspective(x, (uint8_t)zi_back) + centerX;

        int16_t fy_top = apply_perspective(-WORLD_HALF_H, (uint8_t)zi_front) + centerY;
        int16_t by_top = apply_perspective(-WORLD_HALF_H, (uint8_t)zi_back) + centerY;
        int16_t fy_bot = apply_perspective(WORLD_HALF_H, (uint8_t)zi_front) + centerY;
        int16_t by_bot = apply_perspective(WORLD_HALF_H, (uint8_t)zi_back) + centerY; 

        // Side walls (depth lines)
        draw_line2buffer(GREEN, fx, fy_top, bx, by_top, buf);
        draw_line2buffer(GREEN, fx, fy_bot, bx, by_bot, buf);
        
        draw_line2buffer(GREEN, bx, by_top, bx, by_bot, buf);
    }

    for (int16_t y = -WORLD_HALF_H; y <= WORLD_HALF_H; y += grid_size_y) {
        int16_t fy = apply_perspective(y, (uint8_t)zi_front) + centerY;
        int16_t by = apply_perspective(y, (uint8_t)zi_back) + centerY;

        int16_t fx_left = apply_perspective(-WORLD_HALF_W, (uint8_t)zi_front) + centerX;
        int16_t bx_left = apply_perspective(-WORLD_HALF_W, (uint8_t)zi_back) + centerX;
        int16_t fx_right = apply_perspective(WORLD_HALF_W, (uint8_t)zi_front) + centerX;
        int16_t bx_right = apply_perspective(WORLD_HALF_W, (uint8_t)zi_back) + centerX;

        // Top/Bottom walls (depth lines)
        draw_line2buffer(GREEN, fx_left, fy, bx_left, by, buf);
        draw_line2buffer(GREEN, fx_right, fy, bx_right, by, buf);

        draw_line2buffer(GREEN, bx_left, by, bx_right, by, buf);
    }

}

void draw_level_color_indicator(uint16_t buf) {
    draw_vline2buffer(GREEN, 4, LEVEL_INDICATOR_HEIGHT - 3, PIT_HEIGHT * LEVEL_INDICATOR_WIDTH, buf);
    draw_vline2buffer(GREEN, 5+LEVEL_INDICATOR_WIDTH, LEVEL_INDICATOR_HEIGHT - 3, PIT_HEIGHT * LEVEL_INDICATOR_WIDTH, buf);

    // Draw level color blocks
    for (uint8_t i = 0; i < PIT_HEIGHT; i++) {
        uint8_t z_idx = (PIT_HEIGHT - 1) - i; 
        
        uint8_t y_bottom = ((PIT_HEIGHT - 1 - i) * LEVEL_INDICATOR_WIDTH) + LEVEL_INDICATOR_HEIGHT;
        
        bool level_has_blocks = false;
        for (uint8_t y = 0; y < PIT_DEPTH && !level_has_blocks; y++) {
            for (uint8_t x = 0; x < PIT_WIDTH; x++) {
                if (pit[z_idx][y][x]) {
                    level_has_blocks = true;
                    break;
                }
            }
        }
        
        if (level_has_blocks) {
            fill_rect2buffer(layer_colors[z_idx], 6, y_bottom - 3, LEVEL_INDICATOR_WIDTH-2, LEVEL_INDICATOR_WIDTH, buf);
        } else {
            draw_pixel2buffer(GREEN, 5, y_bottom - 3, buf);
            draw_pixel2buffer(GREEN, LEVEL_INDICATOR_WIDTH + 4, y_bottom - 3, buf);
        }
    }
}


/* ================= DRAW ================= */


static int16_t cache_px[MAX_BLOCKS * 8];
static int16_t cache_py[MAX_BLOCKS * 8];
static bool cache_valid[MAX_BLOCKS * 8];
static int16_t vert_off_x[8];
static int16_t vert_off_y[8];
static int16_t vert_off_z[8];
static int16_t vert_z_scale[8];
static int16_t block_z_scale[MAX_BLOCKS];

void drawShape(uint16_t buffer) {
    if (state.current == STATE_GAME_OVER) return;

    const Shape *s = &shapes[current_shape_idx];
    uint8_t b, i, e;

    if (angleX != last_ax || angleY != last_ay || angleZ != last_az ||
        last_shape != current_shape_idx || last_zoom != zoom_level) {
        
        last_ax = angleX; last_ay = angleY; last_az = angleZ;
        last_shape = current_shape_idx;
        last_zoom = zoom_level;

        g_sinX = sine_values[angleX]; g_cosX = cosine_values[angleX];
        g_sinY = sine_values[angleY]; g_cosY = cosine_values[angleY];
        g_sinZ = sine_values[angleZ]; g_cosZ = cosine_values[angleZ];

        for (b = 0; b < s->num_blocks; b++) {
            rotate_block_center(s->offsets[b], s->center, block_centers[b]);
        }
        for (i = 0; i < 8; i++) {
            rotate_ref_vertex(ref_vertices[i], rot_ref_v[i]);
        }
        for (i = 0; i < 8; i++) {
            vert_off_x[i] = (rot_ref_v[i][0] * CUBE_SIZE) / UNIT_SCALE;
            vert_off_y[i] = (rot_ref_v[i][1] * CUBE_SIZE) / UNIT_SCALE;
            vert_off_z[i] = (rot_ref_v[i][2] * CUBE_SIZE) / UNIT_SCALE;
            vert_z_scale[i] = (vert_off_z[i] * PIT_Z_STEP) / GRID_SIZE;
        }
        for (b = 0; b < s->num_blocks; b++) {
            block_z_scale[b] = (block_centers[b][2] * PIT_Z_STEP) / GRID_SIZE;
        }
    }

    int16_t base_world_x = (shape_pos_x * GRID_SIZE) + (GRID_SIZE / 2) - (VIEWPORT_WIDTH / 2) + ((s->center[0] * GRID_SIZE) / 2);
    int16_t base_world_y = (shape_pos_y * GRID_SIZE) + (GRID_SIZE / 2) - (VIEWPORT_HEIGHT / 2) + ((s->center[1] * GRID_SIZE) / 2);
    uint16_t base_zi = PIT_Z_START + (shape_pos_z * PIT_Z_STEP) + (PIT_Z_STEP / 2) + ((s->center[2] * PIT_Z_STEP) / 2);

    for (i = 0; i < s->num_blocks * 8; i++) cache_valid[i] = false;

    for (b = 0; b < s->num_blocks; b++) {
        uint16_t mask = s->edge_masks[b];
        int16_t b_off_x = block_centers[b][0];
        int16_t b_off_y = block_centers[b][1];
        int16_t b_off_z = block_centers[b][2];

        for (e = 0; e < 12; e++) {
            if (mask & (1 << e)) continue; // Skip internal edges
            uint8_t v0 = edges[e << 1];
            uint8_t v1 = edges[(e << 1) + 1];
            uint8_t c0 = (b << 3) + v0;
            uint8_t c1 = (b << 3) + v1;
            int16_t sx0, sy0, sx1, sy1;

            if (!cache_valid[c0]) {
                int16_t world_x = base_world_x + b_off_x + vert_off_x[v0];
                int16_t world_y = base_world_y + b_off_y + vert_off_y[v0];
                int16_t zi = base_zi + block_z_scale[b] + vert_z_scale[v0];
                if (zi < 1) zi = 1; if (zi > 255) zi = 255;
                cache_px[c0] = apply_perspective(world_x, (uint8_t)zi) + (VIEWPORT_WIDTH >> 1);
                cache_py[c0] = apply_perspective(world_y, (uint8_t)zi) + (VIEWPORT_HEIGHT >> 1);
                cache_valid[c0] = true;
            }
            sx0 = cache_px[c0];
            sy0 = cache_py[c0];

            if (!cache_valid[c1]) {
                int16_t world_x = base_world_x + b_off_x + vert_off_x[v1];
                int16_t world_y = base_world_y + b_off_y + vert_off_y[v1];
                int16_t zi = base_zi + block_z_scale[b] + vert_z_scale[v1];
                if (zi < 1) zi = 1; if (zi > 255) zi = 255;
                cache_px[c1] = apply_perspective(world_x, (uint8_t)zi) + (VIEWPORT_WIDTH >> 1);
                cache_py[c1] = apply_perspective(world_y, (uint8_t)zi) + (VIEWPORT_HEIGHT >> 1);
                cache_valid[c1] = true;
            }
            sx1 = cache_px[c1];
            sy1 = cache_py[c1];

            draw_line2buffer(WHITE, sx0, sy0, sx1, sy1, buffer);
        }
    }
}

void draw_shape_position(){
    
    set_cursor(0, 10);
    draw_string2buffer("x: ", STATIC_BUFFER_ADDR);
    fill_rect2buffer(0, 20, 10, 20, 10, STATIC_BUFFER_ADDR);
    set_cursor(20, 10);
    sprintf(text_buffer, "%u", shape_pos_x);
    draw_string2buffer(text_buffer, STATIC_BUFFER_ADDR);
    set_cursor(0, 20);
    draw_string2buffer("y: ", STATIC_BUFFER_ADDR);
    fill_rect2buffer(0, 20, 20, 20, 20, STATIC_BUFFER_ADDR);
    set_cursor(20, 20);
    sprintf(text_buffer, "%u", shape_pos_y);
    draw_string2buffer(text_buffer, STATIC_BUFFER_ADDR);
    set_cursor(0, 30);
    draw_string2buffer("z: ", STATIC_BUFFER_ADDR);
    fill_rect2buffer(0, 20, 30, 20, 30, STATIC_BUFFER_ADDR);
    set_cursor(20, 30);
    sprintf(text_buffer, "%u", shape_pos_z);
    draw_string2buffer(text_buffer, STATIC_BUFFER_ADDR);

}

static uint8_t left_edges[SCREEN_HEIGHT];
static uint8_t right_edges[SCREEN_HEIGHT];

void draw_poly_fast(uint16_t buf, int16_t x0, int16_t y0, int16_t x1, int16_t y1, 
                    int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint8_t color, uint8_t stride) {
    uint8_t min_y = SCREEN_HEIGHT, max_y = 0;
    int16_t px[5], py[5];
    px[0]=x0; px[1]=x1; px[2]=x2; px[3]=x3; px[4]=x0;
    py[0]=y0; py[1]=y1; py[2]=y2; py[3]=y3; py[4]=y0;

    for(uint8_t i=0; i<4; i++) {
        if (py[i] < 0) py[i] = 0; if (py[i] > 179) py[i] = 179;
        if (px[i] < 0) px[i] = 0; if (px[i] > 255) px[i] = 255; // Clamp X to 8-bit
        if ((uint8_t)py[i] < min_y) min_y = (uint8_t)py[i];
        if ((uint8_t)py[i] > max_y) max_y = (uint8_t)py[i];
    }

    for (uint8_t i = min_y; i <= max_y; i++) {
        left_edges[i] = 255; right_edges[i] = 0;
    }

    for (uint8_t i = 0; i < 4; i++) {
        int16_t x_s = px[i], y_s = py[i], x_e = px[i+1], y_e = py[i+1];
        if (y_s == y_e) continue;
        if (y_s > y_e) { int16_t t; t=x_s; x_s=x_e; x_e=t; t=y_s; y_s=y_e; y_e=t; }

        uint16_t dx = (x_e >= x_s) ? (x_e - x_s) : (x_s - x_e);
        uint16_t dy = y_e - y_s;
        int16_t sx = (x_e >= x_s) ? 1 : -1;
        int16_t err = dy >> 1;
        int16_t cur_x = x_s;

        for (uint8_t y = (uint8_t)y_s; y <= (uint8_t)y_e; y++) {
            if (y < 180) {
                if ((uint8_t)cur_x < left_edges[y]) left_edges[y] = (uint8_t)cur_x;
                if ((uint8_t)cur_x > right_edges[y]) right_edges[y] = (uint8_t)cur_x;
            }
            err += dx;
            while (err >= (int16_t)dy) { err -= dy; cur_x += sx; }
        }
    }

    for (uint8_t y = min_y; y <= max_y; y += stride) {
        if (left_edges[y] <= right_edges[y]) 
            draw_line2buffer(color, left_edges[y], y, right_edges[y], y, buf);
    }
}

void draw_cube_at(uint16_t buf, uint8_t x, uint8_t y, uint8_t z, uint8_t color) {
    // 1. Calculate visibility flags FIRST
    bool draw_top   = (z == 0) || !pit[z-1][y][x];
    bool draw_left  = (x == 0) || !pit[z][y][x-1];
    bool draw_right = (x == PIT_WIDTH - 1) || !pit[z][y][x+1];
    bool draw_back  = (y == PIT_DEPTH - 1) || !pit[z][y+1][x];
    bool draw_front = (y == 0) || !pit[z][y-1][x]; // Added for completeness/correctness

    // 2. Optimization: If no faces are visible, return immediately
    if (!draw_top && !draw_left && !draw_right && !draw_back && !draw_front) return;

    // 3. Draw all visible faces
    if (draw_top) {
        draw_poly_fast(buf, 
            grid_sx[z][y][x],     grid_sy[z][y],
            grid_sx[z][y][x+1],   grid_sy[z][y],
            grid_sx[z][y+1][x+1], grid_sy[z][y+1],
            grid_sx[z][y+1][x],   grid_sy[z][y+1],
            color, FILL_STRIDE);
        
        // Top face outline
        draw_line2buffer(BLACK, grid_sx[z][y][x], grid_sy[z][y], 
                        grid_sx[z][y][x+1], grid_sy[z][y], buf);
        draw_line2buffer(BLACK, grid_sx[z][y][x+1], grid_sy[z][y], 
                        grid_sx[z][y+1][x+1], grid_sy[z][y+1], buf);
        draw_line2buffer(BLACK, grid_sx[z][y+1][x+1], grid_sy[z][y+1], 
                        grid_sx[z][y+1][x], grid_sy[z][y+1], buf);
        draw_line2buffer(BLACK, grid_sx[z][y+1][x], grid_sy[z][y+1], 
                        grid_sx[z][y][x], grid_sy[z][y], buf);
    }

    if (draw_left) {
        draw_poly_fast(buf,
            grid_sx[z][y][x],     grid_sy[z][y],
            grid_sx[z+1][y][x],   grid_sy[z+1][y],
            grid_sx[z+1][y+1][x], grid_sy[z+1][y+1],
            grid_sx[z][y+1][x],   grid_sy[z][y+1],
            color, FILL_STRIDE);
    }

    if (draw_right) {
        draw_poly_fast(buf,
            grid_sx[z][y][x+1],     grid_sy[z][y],
            grid_sx[z+1][y][x+1],   grid_sy[z+1][y],
            grid_sx[z+1][y+1][x+1], grid_sy[z+1][y+1],
            grid_sx[z][y+1][x+1],   grid_sy[z][y+1],
            color, FILL_STRIDE);
    }

    if (draw_back) {
        draw_poly_fast(buf,
            grid_sx[z][y+1][x],     grid_sy[z][y+1],
            grid_sx[z][y+1][x+1],   grid_sy[z][y+1],
            grid_sx[z+1][y+1][x+1], grid_sy[z+1][y+1],
            grid_sx[z+1][y+1][x],   grid_sy[z+1][y+1],
            color, FILL_STRIDE);
    }

    if (draw_front) {
        draw_poly_fast(buf,
            grid_sx[z][y][x],     grid_sy[z][y],
            grid_sx[z][y][x+1],   grid_sy[z][y],
            grid_sx[z+1][y][x+1], grid_sy[z+1][y],
            grid_sx[z+1][y][x],   grid_sy[z+1][y],
            color, FILL_STRIDE);
    }
    
    
    if (draw_top) {
        draw_line2buffer(BLACK, grid_sx[z][y][x], grid_sy[z][y], 
                        grid_sx[z][y][x+1], grid_sy[z][y], buf);
        draw_line2buffer(BLACK, grid_sx[z][y][x+1], grid_sy[z][y], 
                        grid_sx[z][y+1][x+1], grid_sy[z][y+1], buf);
        draw_line2buffer(BLACK, grid_sx[z][y+1][x+1], grid_sy[z][y+1], 
                        grid_sx[z][y+1][x], grid_sy[z][y+1], buf);
        draw_line2buffer(BLACK, grid_sx[z][y+1][x], grid_sy[z][y+1], 
                        grid_sx[z][y][x], grid_sy[z][y], buf);
    }
}


void draw_settled_range(uint16_t buffer, uint8_t start_z) {
    for (int8_t z = PIT_HEIGHT - 1; z >= start_z; z--) {
        for (uint8_t y = 0; y < PIT_DEPTH; y++) {
            for (uint8_t x = 0; x < PIT_WIDTH; x++) {
                if (pit[z][y][x]) {
                    draw_cube_at(buffer, x, y, z, layer_colors[z]);
                }
            }
        }
    }
}

void draw_settled_blocks(uint16_t buf) {
    // Painter's algorithm: BACK TO FRONT
    // In perspective: higher Z = further back, higher Y = further back
    for (int8_t z = PIT_HEIGHT - 1; z >= 0; z--) {
        for (uint8_t y = 0; y < PIT_DEPTH; y++) {  
            for (uint8_t x = 0; x < PIT_WIDTH; x++) {
                if (!pit[z][y][x]) continue; 
                
                uint8_t color = layer_colors[z];
                
                // Front Face (Index z)
                int16_t fx0 = grid_sx[z][y][x];     int16_t fy0 = grid_sy[z][y];
                int16_t fx1 = grid_sx[z][y][x+1];   int16_t fy1 = grid_sy[z][y];
                int16_t fx3 = grid_sx[z][y+1][x];   int16_t fy3 = grid_sy[z][y+1];
                int16_t fx2 = grid_sx[z][y+1][x+1]; int16_t fy2 = grid_sy[z][y+1];
                
                // Back Face (Index z+1)
                int16_t bx0 = grid_sx[z+1][y][x];   int16_t by0 = grid_sy[z+1][y];
                int16_t bx1 = grid_sx[z+1][y][x+1]; int16_t by1 = grid_sy[z+1][y];
                int16_t bx3 = grid_sx[z+1][y+1][x]; int16_t by3 = grid_sy[z+1][y+1];
                int16_t bx2 = grid_sx[z+1][y+1][x+1]; int16_t by2 = grid_sy[z+1][y+1];

                // Simplified visibility: only draw faces at edges or with empty neighbors
                bool draw_left  = (x == 0) || !pit[z][y][x-1];
                bool draw_right = (x == PIT_WIDTH - 1) || !pit[z][y][x+1];
                bool draw_back  = (y == PIT_DEPTH - 1) || !pit[z][y+1][x];
                bool draw_front = (y == 0) || !pit[z][y-1][x];
                bool draw_top   = (z == 0) || !pit[z-1][y][x];

                // Draw faces
                if (draw_left) 
                    draw_poly_fast(buf, fx0, fy0, fx3, fy3, bx3, by3, bx0, by0, color, FILL_STRIDE);
                
                if (draw_right) 
                    draw_poly_fast(buf, fx1, fy1, bx1, by1, bx2, by2, fx2, fy2, color, FILL_STRIDE);
                
                if (draw_front) 
                    draw_poly_fast(buf, fx0, fy0, bx0, by0, bx1, by1, fx1, fy1, color, FILL_STRIDE);
                
                if (draw_back) 
                    draw_poly_fast(buf, fx3, fy3, fx2, fy2, bx2, by2, bx3, by3, color, FILL_STRIDE);
                
                if (draw_top) {
                    draw_poly_fast(buf, fx0, fy0, fx1, fy1, fx2, fy2, fx3, fy3, color, FILL_STRIDE);
                    
                    draw_line2buffer(BLACK, fx0, fy0, fx1, fy1, buf);
                    draw_line2buffer(BLACK, fx1, fy1, fx2, fy2, buf);
                    draw_line2buffer(BLACK, fx2, fy2, fx3, fy3, buf);
                    draw_line2buffer(BLACK, fx3, fy3, fx0, fy0, buf);
                } 
            }
        }
    }
}

void draw_incremental_lock(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y, int8_t start_z) {
    
    for (int8_t z = start_z; z >= 0; z--) {
        for (int8_t y = max_y; y >= min_y; y--) {
            for (int8_t x = min_x; x <= max_x; x++) {
                if (pit[z][y][x]) {
                    draw_cube_at(STATIC_BUFFER_ADDR, x, y, z, layer_colors[z]);
                }
            }
        }
    }
}

