#include <stdint.h>
#include <stdbool.h>
#include "colors.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_render.h"
#include "sound.h"


uint8_t pit[MAX_PIT_HEIGHT][MAX_PIT_DEPTH][MAX_PIT_WIDTH];           // 1 if block present
uint8_t pit_colors[MAX_PIT_HEIGHT][MAX_PIT_DEPTH][MAX_PIT_WIDTH];    // Color of each block
const uint8_t layer_colors[MAX_PIT_HEIGHT] = {
    DARK_GRAY, DARK_BLUE, BROWN, DARK_MAGENTA, DARK_CYAN, DARK_RED, DARK_GREEN, DARK_BLUE
};


bool is_layer_complete(uint8_t z) {
    for (uint8_t y = 0; y < PIT_DEPTH; y++) {
        for (uint8_t x = 0; x < PIT_WIDTH; x++) {
            if (!pit[z][y][x]) return false;
        }
    }
    return true;
}

void clear_layer(uint8_t z) {
    // Shift all layers above down using 8-bit counters
    for (int8_t zz = z; zz > 0; zz--) {
        for (uint8_t y = 0; y < PIT_DEPTH; y++) {
            for (uint8_t x = 0; x < PIT_WIDTH; x++) {
                pit[zz][y][x] = pit[zz-1][y][x];
                pit_colors[zz][y][x] = pit_colors[zz-1][y][x];
            }
        }
    }
    // Clear top layer
    for (uint8_t y = 0; y < PIT_DEPTH; y++) {
        for (uint8_t x = 0; x < PIT_WIDTH; x++) {
            pit[0][y][x] = 0;
            pit_colors[0][y][x] = 0;
        }
    }
    lines_cleared++;
    score += 100 * (current_level + 1);
    mark_hud_dirty();
    state.need_static_redraw = true;
}

void check_and_clear_layers(void) {
    int8_t deepest_cleared = -1;
    
    for (int8_t z = PIT_HEIGHT - 1; z >= 0; z--) {
        if (is_layer_complete((uint8_t)z)) {
            if (deepest_cleared == -1) {
                trigger_screen_shake();
                play_clear_level_sound();
            }
            clear_layer((uint8_t)z);
            if (z > deepest_cleared) deepest_cleared = z;
            z++; 
        }
    }

    if (deepest_cleared != -1) {
        state.full_redraw_pending = true;
        state.need_static_redraw = true;
    }
}



void redraw_region(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y) {
    for (int8_t y = min_y; y <= max_y; y++) {
        for (int8_t x = min_x; x <= max_x; x++) {
            for (uint8_t z = 0; z < PIT_HEIGHT; z++) {
                if (pit[z][y][x]) {
                    int16_t fx0 = grid_sx[z][y][x];
                    int16_t fx1 = grid_sx[z][y][x+1];
                    int16_t fx2 = grid_sx[z][y+1][x+1];
                    int16_t fx3 = grid_sx[z][y+1][x];
                    int16_t fy0 = grid_sy[z][y];
                    int16_t fy1 = grid_sy[z][y];
                    int16_t fy2 = grid_sy[z][y+1];
                    int16_t fy3 = grid_sy[z][y+1];
                    draw_poly_fast(STATIC_BUFFER_ADDR, fx0, fy0, fx1, fy1, fx2, fy2, fx3, fy3, BLACK, 1);
                }
            }
        }
    }
    
    for (int8_t z = PIT_HEIGHT - 1; z >= 0; z--) {
        for (int8_t y = max_y; y >= min_y; y--) {
            for (int8_t x = min_x; x <= max_x; x++) {
                if (pit[z][y][x]) {
                    draw_cube_at(STATIC_BUFFER_ADDR, x, y, z, layer_colors[z]);
                }
            }
        }
    }
}

void lock_shape(void) {
    const Shape *s = &shapes[current_shape_idx];
    int8_t min_x = PIT_WIDTH, max_x = -1;
    int8_t min_y = PIT_DEPTH, max_y = -1;
    
    int8_t max_z = -1; 
    
    for (uint8_t b = 0; b < s->num_blocks; b++) {
        int8_t rx, ry, rz;
        get_rotated_offset(b, targetX, targetY, targetZ, &rx, &ry, &rz);
        int8_t ax = shape_pos_x + rx;
        int8_t ay = shape_pos_y + ry;
        int8_t az = shape_pos_z + rz;
        
        if (az >= 0 && az < PIT_HEIGHT && ax >= 0 && ax < PIT_WIDTH && ay >= 0 && ay < PIT_DEPTH) {
            pit[az][ay][ax] = 1;
            pit_colors[az][ay][ax] = layer_colors[az];
            
            if (ax < min_x) min_x = ax;
            if (ax > max_x) max_x = ax;
            if (ay < min_y) min_y = ay;
            if (ay > max_y) max_y = ay;
            
            // NEW: Update max Z
            if (az > max_z) max_z = az;
        }
    }

    min_x--; if (min_x < 0) min_x = 0;
    max_x++; if (max_x >= PIT_WIDTH) max_x = PIT_WIDTH - 1;
    min_y--; if (min_y < 0) min_y = 0;
    max_y++; if (max_y >= PIT_DEPTH) max_y = PIT_DEPTH - 1;
    
    if (max_z >= 0) {
        draw_incremental_lock(min_x, max_x, min_y, max_y, max_z);
    }

    check_and_clear_layers();
    spawn_new_shape(); 
    
    // IMPORTANT: Remove this line, otherwise main loop will redraw everything again
    // state.need_static_redraw = true; 
}

uint8_t count_occupied_levels(void) {
    uint8_t count = 0;
    
    for (uint8_t z = 0; z < PIT_HEIGHT; z++) {
        bool level_has_blocks = false;
        
        for (uint8_t y = 0; y < PIT_DEPTH; y++) {
            for (uint8_t x = 0; x < PIT_WIDTH; x++) {
                if (pit[z][y][x]) {
                    level_has_blocks = true;
                    break; 
                }
            }
            if (level_has_blocks) break; 
        }
        
        if (level_has_blocks) {
            count++;
        }
    }
    return count;
}