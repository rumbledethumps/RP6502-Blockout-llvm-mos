#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "bitmap_graphics_db.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_state.h"

/* ================= SHAPE POSITION ================= */

int8_t shape_pos_x;
int8_t shape_pos_y;
int8_t shape_pos_z;

const Shape shapes[NUM_SHAPES] = {
    // Cube: Center at 0
    {1,"CUBE",{{0,0,0}}, {0}, {0,0,0}},
    
    // short I-Piece
    {2,"I",{{0,0,0},{0,1,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM}, 
        {0,0,0}},

    // I-Piece: Center on middle block
    {3,"I",{{0,-1,0},{0,0,0},{0,1,0}},
        {MASK_FACE_TOP, MASK_FACE_TOP|MASK_FACE_BOTTOM, MASK_FACE_BOTTOM}, 
        {0,0,0}},

    // big cube: MUST have center at (0.5, 0.5, 0) = {1, 1, 0} in half-blocks
    {4, "C", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
        {MASK_FACE_RIGHT|MASK_FACE_TOP, MASK_FACE_TOP|MASK_FACE_LEFT, 
         MASK_FACE_RIGHT|MASK_FACE_BOTTOM, MASK_FACE_BOTTOM|MASK_FACE_LEFT},
        {1,1,1}},  // This is correct for a 2x2 cube
    
    // L-Piece: Center on middle block
    {3,"L",{{0,-1,0},{0,0,0},{1,0,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT, MASK_FACE_LEFT}, 
        {0,0,0}},
    
    // T-Piece: Center at intersection
    {4,"T",{{-1,0,0},{0,0,0},{1,0,0},{0,-1,0}},
        {MASK_FACE_RIGHT, MASK_FACE_LEFT|MASK_FACE_RIGHT|MASK_FACE_BOTTOM,
         MASK_FACE_LEFT, MASK_FACE_TOP}, 
        {0,0,0}},
    
    // S-Piece
    {4,"S",{{0,-1,0},{0,0,0},{1,0,0},{1,1,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT,
         MASK_FACE_LEFT|MASK_FACE_TOP, MASK_FACE_BOTTOM}, 
        {2,0,0}},
    
    // L+-Piece: Center on middle block
    {4,"L+",{{0,-1,0},{0,0,0},{1,0,0},{0,0,1}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT|MASK_FACE_FRONT, MASK_FACE_LEFT, MASK_FACE_BACK}, 
        {0,0,0}},
};


void get_rotated_offset(uint8_t block_idx, uint8_t use_angleX, uint8_t use_angleY, uint8_t use_angleZ, int8_t *rx, int8_t *ry, int8_t *rz) {
    const Shape *s = &shapes[current_shape_idx];
    
    bool has_half_center = (s->center[0] != 0) || (s->center[1] != 0) || (s->center[2] != 0);
    
    if (!has_half_center) {
        int16_t x = s->offsets[block_idx][0];
        int16_t y = s->offsets[block_idx][1];
        int16_t z = s->offsets[block_idx][2];
        int16_t t;

        uint8_t ay = (use_angleY >> 6) & 3;
        for(uint8_t i=0; i<ay; i++) { t=x; x=z; z=-t; }
        
        uint8_t ax = (use_angleX >> 6) & 3;
        for(uint8_t i=0; i<ax; i++) { t=y; y=-z; z=t; }
        
        uint8_t az = (use_angleZ >> 6) & 3;
        for(uint8_t i=0; i<az; i++) { t=x; x=-y; y=t; }

        *rx = (int8_t)x;
        *ry = (int8_t)y;
        *rz = (int8_t)z;
    } else {
        int16_t x = (int16_t)s->offsets[block_idx][0] * 2 - s->center[0];
        int16_t y = (int16_t)s->offsets[block_idx][1] * 2 - s->center[1];
        int16_t z = (int16_t)s->offsets[block_idx][2] * 2 - s->center[2];
        int16_t t;

        uint8_t ay = (use_angleY >> 6) & 3;
        for(uint8_t i=0; i<ay; i++) { t=x; x=z; z=-t; }
        
        uint8_t ax = (use_angleX >> 6) & 3;
        for(uint8_t i=0; i<ax; i++) { t=y; y=-z; z=t; }
        
        uint8_t az = (use_angleZ >> 6) & 3;
        for(uint8_t i=0; i<az; i++) { t=x; x=-y; y=t; }

        x += s->center[0];
        y += s->center[1];
        z += s->center[2];
        
        *rx = x / 2;
        *ry = y / 2;
        *rz = z / 2;
    }
}

bool is_position_valid(int8_t px, int8_t py, int8_t pz) {
    const Shape *s = &shapes[current_shape_idx];
    for (uint8_t b = 0; b < s->num_blocks; b++) {
        int8_t rx, ry, rz;
        get_rotated_offset(b, angleX, angleY, angleZ, &rx, &ry, &rz);
        
        int8_t abs_x = px + rx;
        int8_t abs_y = py + ry;
        int8_t abs_z = pz + rz;

        if (abs_x < 0 || abs_x >= PIT_WIDTH)  return false;
        if (abs_y < 0 || abs_y >= PIT_DEPTH)  return false;
        if (abs_z < 0 || abs_z >= PIT_HEIGHT) return false;
        if (pit[abs_z][abs_y][abs_x]) return false;
    }
    return true;
}

bool is_rotation_valid_at(uint8_t nX, uint8_t nY, uint8_t nZ, int8_t px, int8_t py, int8_t pz) {
    const Shape *s = &shapes[current_shape_idx];
    for (uint8_t b = 0; b < s->num_blocks; b++) {
        int8_t rx, ry, rz;
        get_rotated_offset(b, nX, nY, nZ, &rx, &ry, &rz);
        
        int8_t abs_x = px + rx;
        int8_t abs_y = py + ry;
        int8_t abs_z = pz + rz;

        if (abs_x < 0 || abs_x >= PIT_WIDTH)  return false;
        if (abs_y < 0 || abs_y >= PIT_DEPTH)  return false;
        if (abs_z < 0 || abs_z >= PIT_HEIGHT) return false;
        if (pit[abs_z][abs_y][abs_x]) return false;
    }
    return true;
}

void apply_rotation(uint8_t new_angleX, uint8_t new_angleY, uint8_t new_angleZ) {
    angleX = new_angleX;
    angleY = new_angleY;
    angleZ = new_angleZ;
}

bool try_wall_kick(uint8_t nX, uint8_t nY, uint8_t nZ, int8_t *out_x, int8_t *out_y, int8_t *out_z) {
    if (is_rotation_valid_at(nX, nY, nZ, shape_pos_x, shape_pos_y, shape_pos_z)) {
        *out_x = shape_pos_x;
        *out_y = shape_pos_y;
        *out_z = shape_pos_z;
        return true;
    }

    int8_t kick_offsets[][3] = {
        // Single steps in each direction
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        // Double steps (important for corners)
        {2, 0, 0}, {-2, 0, 0}, {0, 2, 0}, {0, -2, 0},
        // Diagonal kicks
        {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0},
        // Diagonal with Z
        {1, 0, 1}, {-1, 0, 1}, {0, 1, 1}, {0, -1, 1},
        {1, 0, -1}, {-1, 0, -1}, {0, 1, -1}, {0, -1, -1}
    };

    for (uint8_t i = 0; i < 22; i++) {
        int8_t test_x = shape_pos_x + kick_offsets[i][0];
        int8_t test_y = shape_pos_y + kick_offsets[i][1];
        int8_t test_z = shape_pos_z + kick_offsets[i][2];

        if (is_rotation_valid_at(nX, nY, nZ, test_x, test_y, test_z)) {
            *out_x = test_x;
            *out_y = test_y;
            *out_z = test_z;
            return true;
        }
    }

    return false;
}

void spawn_new_shape(void) {
    const Shape *s = &shapes[current_shape_idx];
    current_shape_idx = next_shape_idx;
    srand(seed);
    next_shape_idx = random(0, NUM_SHAPES);
    
    shape_pos_x = PIT_WIDTH / 2;
    shape_pos_y = PIT_DEPTH / 2;
    shape_pos_z = 0;
    angleX = angleY = angleZ = 0;
    targetX = targetY = targetZ = 0;
    state.anim_counter = 0;
    
    if (!is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z)) {
        change_state(STATE_GAME_OVER);
        return;
    }
    
    cubes_played += s->num_blocks;
    current_level = 1+ lines_cleared / 5;
    drop_delay = 60 - (current_level * 10);
    if (drop_delay < 10) drop_delay = 10;
    mark_hud_dirty();
}