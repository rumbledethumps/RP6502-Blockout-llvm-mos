#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics_db.h"

/* ================= CONFIG ================= */

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 180
#define VIEWPORT_WIDTH 180
#define VIEWPORT_HEIGHT 180

#define VIEWPORT_X (((SCREEN_WIDTH - VIEWPORT_WIDTH) / 2) + 8) // Added 8 pixel offset
#define VIEWPORT_Y ((SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2)

#define SCREEN_CENTER_X (VIEWPORT_WIDTH >> 1)
#define SCREEN_CENTER_Y (VIEWPORT_HEIGHT >> 1)

#define STATIC_STRUCT_ADDR   0xFE00
#define VIEWPORT_STRUCT_ADDR 0xFE80
#define STATIC_BUFFER_ADDR   0x0000
#define VIEWPORT_BUFFER_0    0x7080
#define VIEWPORT_BUFFER_1    0xAFC0

#define NUM_POINTS 256

#define GRID_SIZE        (VIEWPORT_WIDTH / PIT_WIDTH)
#define CUBE_SIZE        (GRID_SIZE / 2)

#define WORLD_HALF_W     (VIEWPORT_WIDTH / 2)
#define WORLD_HALF_H     (VIEWPORT_HEIGHT / 2)

#define PIT_Z_START      64
#define PIT_Z_STEP       12

#define PIT_WIDTH 5
#define PIT_DEPTH 5
#define PIT_HEIGHT 8

#define MAX_BLOCKS 4
#define NUM_SHAPES 7
#define NUM_ZOOM_LEVELS 8
#define NUM_MODES 2

#define ROTATION_STEPS 4
#define ANGLE_STEP_90 (256/4)

/* ================= SHAPE POSITION ================= */

static int8_t shape_pos_x = PIT_WIDTH / 2;
static int8_t shape_pos_y = PIT_DEPTH / 2;
static int8_t shape_pos_z = 0;

/* ================= GLOBAL STATE ================= */

static uint16_t viewport_buffers[2] = {
    VIEWPORT_BUFFER_0, VIEWPORT_BUFFER_1
};
static uint8_t active_buffer = 0;

static bool paused = false;
static bool perspective_enabled = true;
static uint8_t zoom_level = 0;
static uint8_t mode = 0;

#define KEYBOARD_INPUT 0xFF10
#define KEYBOARD_BYTES 32
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
static uint8_t keystates[32];
static bool handled_key = false;

char text_buffer[24];
/* ================= LUTS ================= */

static int16_t sine_values[NUM_POINTS];
static int16_t cosine_values[NUM_POINTS];
static uint16_t persp_lut[256];

static const uint16_t zoom_lut[NUM_ZOOM_LEVELS] = {
    8192, 4096, 2048, 1024, 896, 768, 640, 512
};

/* ================= SHAPES ================= */

#define MASK_FACE_RIGHT  ((1<<1)|(1<<5)|(1<<9)|(1<<10))
#define MASK_FACE_LEFT   ((1<<3)|(1<<7)|(1<<8)|(1<<11))
#define MASK_FACE_TOP    ((1<<2)|(1<<6)|(1<<10)|(1<<11))
#define MASK_FACE_BOTTOM ((1<<0)|(1<<4)|(1<<8)|(1<<9))

typedef struct {
    uint8_t num_blocks;
    const char *name;
    const int8_t offsets[MAX_BLOCKS][3];
    const uint16_t edge_masks[MAX_BLOCKS];
    const int8_t center[3]; // Values are in half-blocks (1 = 0.5 blocks)
} Shape;

static const Shape shapes[NUM_SHAPES] = {
    // Cube: Center at 0 (rotates around its own middle)
    {1,"CUBE",{{0,0,0}}, {0}, {0,0,0}},
    
    // short I-Piece: Center at 0 (rotates on the middle block)
    {2,"I",{{0,0,0},{0,1,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM}, 
        {0,0,0}},

    // I-Piece: Center at 0 (rotates on the middle block)
    {3,"I",{{0,-1,0},{0,0,0},{0,1,0}},
        {MASK_FACE_TOP, MASK_FACE_TOP|MASK_FACE_BOTTOM, MASK_FACE_BOTTOM}, 
        {0,0,0}},

    // big cube 
    {4, "C", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
        {MASK_FACE_RIGHT|MASK_FACE_TOP, MASK_FACE_TOP|MASK_FACE_LEFT, MASK_FACE_RIGHT|MASK_FACE_BOTTOM, MASK_FACE_BOTTOM|MASK_FACE_LEFT},
        {1,1,0}},
    
    // L-Piece: Inside corner center (0.5, -0.5, 0)
    {3,"L",{{0,-1,0},{0,0,0},{1,0,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT, MASK_FACE_LEFT}, 
        {1,-1,0}},
    
    // T-Piece: Center at 0 (rotates on the intersection)
    {4,"T",{{-1,0,0},{0,0,0},{1,0,0},{0,-1,0}},
        {MASK_FACE_RIGHT, MASK_FACE_LEFT|MASK_FACE_RIGHT|MASK_FACE_BOTTOM,
         MASK_FACE_LEFT, MASK_FACE_TOP}, 
        {0,-1,0}},
    
    // S-Piece: Center at (0.5, -0.5, 0) for a "flat" rotation
    {4,"S",{{0,-1,0},{0,0,0},{1,0,0},{1,1,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT,
         MASK_FACE_LEFT|MASK_FACE_TOP, MASK_FACE_BOTTOM}, 
        {0,0,0}}
};

static uint8_t current_shape_idx = 0;

/* ================= GEOMETRY ================= */

#define UNIT_SCALE 1024 

static const int16_t ref_vertices[8][3] = {
    {-UNIT_SCALE,-UNIT_SCALE,-UNIT_SCALE},
    { UNIT_SCALE,-UNIT_SCALE,-UNIT_SCALE},
    { UNIT_SCALE, UNIT_SCALE,-UNIT_SCALE},
    {-UNIT_SCALE, UNIT_SCALE,-UNIT_SCALE},
    {-UNIT_SCALE,-UNIT_SCALE, UNIT_SCALE},
    { UNIT_SCALE,-UNIT_SCALE, UNIT_SCALE},
    { UNIT_SCALE, UNIT_SCALE, UNIT_SCALE},
    {-UNIT_SCALE, UNIT_SCALE, UNIT_SCALE}
};

static const uint8_t edges[24] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,4, 1,5, 2,6, 3,7
};

/* ================= ANGLES ================= */

static uint8_t angleX=0, angleY=0, angleZ=0;
static uint8_t targetX=0, targetY=0, targetZ=0;
static uint8_t animating=0;
static bool need_static_redraw = false;

/* ================= ROTATION CACHE ================= */

static uint8_t last_ax=255, last_ay=255, last_az=255;
static uint8_t last_shape=255;
static uint8_t last_zoom=255;

static int16_t g_sinX, g_cosX;
static int16_t g_sinY, g_cosY;
static int16_t g_sinZ, g_cosZ;

static int16_t rot_ref_v[8][3];
static int16_t scaled_ref_v[8][3];
static int16_t block_centers[MAX_BLOCKS][3];
static int16_t scaled_block_centers[MAX_BLOCKS][3];

static int16_t px[8], py[8];

/* ================= FAST MATH ================= */

enum {cA1 = 3370945099UL, cB1 = 2746362156UL, cC1 = 292421UL};
enum {n = 13, p = 32, q = 31, r = 3, a = 12};
int16_t fpsin(int16_t i) {
    i <<= 1; uint8_t c = i < 0;
    if(i == (i | 0x4000)) i = (1 << 15) - i;
    i = (i & 0x7FFF) >> 1;
    uint32_t y = (cC1 * ((uint32_t)i)) >> n;
    y = cB1 - (((uint32_t)i * y) >> r);
    y = (uint32_t)i * (y >> n); y = (uint32_t)i * (y >> n);
    y = cA1 - (y >> (p - q)); y = (uint32_t)i * (y >> n);
    y = (y + (1UL << (q - a - 1))) >> (q - a);
    return c ? -y : y;
}
#define fpcos(i) fpsin((int16_t)(((uint16_t)(i)) + 8192U))

static inline int16_t apply_perspective(int16_t v, uint8_t zi) {
    return (v * (int32_t)persp_lut[zi]) >> 10;
}

/* ================= PRECOMPUTE ================= */

static void precompute_tables(void) {
    for (uint16_t i=0;i<NUM_POINTS;i++) {
        sine_values[i]=fpsin(i*(32768/NUM_POINTS));
        cosine_values[i]=fpcos(i*(32768/NUM_POINTS));
    }
    persp_lut[0]=65535;
    for(uint16_t i=1;i<256;i++) persp_lut[i]=65536U/i;
}

/* ================= ROTATION ================= */

static void rotate_ref_vertex(const int16_t *v, int16_t *o) {
    int32_t x = v[0], y = v[1], z = v[2];

    // Y-axis rotation
    int32_t ry = (x * g_cosY + z * g_sinY) >> 12;
    int32_t rz = (z * g_cosY - x * g_sinY) >> 12;

    // X-axis rotation
    int32_t rx = ry;
    int32_t ry2 = (y * g_cosX - rz * g_sinX) >> 12;
    int32_t rz2 = (y * g_sinX + rz * g_cosX) >> 12;

    // Z-axis rotation
    o[0] = (int16_t)((rx * g_cosZ - ry2 * g_sinZ) >> 12);
    o[1] = (int16_t)((rx * g_sinZ + ry2 * g_cosZ) >> 12);
    o[2] = (int16_t)rz2;
}

static void rotate_block_center(const int8_t *o, const int8_t *center, int16_t *v) {
    int16_t tmp[3] = {
        (int16_t)(o[0] * 2 - center[0]) * CUBE_SIZE,
        (int16_t)(o[1] * 2 - center[1]) * CUBE_SIZE,
        (int16_t)(o[2] * 2 - center[2]) * CUBE_SIZE
    };
    rotate_ref_vertex(tmp, v);
}

void get_rotated_offset(uint8_t block_idx, uint8_t use_angleX, uint8_t use_angleY, uint8_t use_angleZ, int8_t *rx, int8_t *ry, int8_t *rz) {
    const Shape *s = &shapes[current_shape_idx];
    
    // 1. Move to half-block space
    int16_t x = (int16_t)s->offsets[block_idx][0] * 2 - s->center[0];
    int16_t y = (int16_t)s->offsets[block_idx][1] * 2 - s->center[1];
    int16_t z = (int16_t)s->offsets[block_idx][2] * 2 - s->center[2];
    int16_t t;

    // 2. Apply 90-degree rotations
    uint8_t ay = (use_angleY >> 6) & 3;
    for(uint8_t i=0; i<ay; i++) { t=x; x=z; z=-t; }
    uint8_t ax = (use_angleX >> 6) & 3;
    for(uint8_t i=0; i<ax; i++) { t=y; y=-z; z=t; }
    uint8_t az = (use_angleZ >> 6) & 3;
    for(uint8_t i=0; i<az; i++) { t=x; x=-y; y=t; }

    // 3. Convert back to block units. 
    // We use floor division (shift right) to ensure the 0.5 offset 
    // maps to the correct integer grid cell.
    *rx = (x + s->center[0]) >> 1;
    *ry = (y + s->center[1]) >> 1;
    *rz = (z + s->center[2]) >> 1;
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
    }
    return true;
}

void apply_rotation(uint8_t new_angleX, uint8_t new_angleY, uint8_t new_angleZ) {
    // The shape_pos_x/y/z now represents the position of the manual center block
    angleX = new_angleX;
    angleY = new_angleY;
    angleZ = new_angleZ;
}


/* ================= WALL KICK LOGIC ================= */

// Attempts to find a valid position for a new rotation
// Returns true if a valid position (original or kicked) is found
bool try_wall_kick(uint8_t nX, uint8_t nY, uint8_t nZ, int8_t *out_x, int8_t *out_y, int8_t *out_z) {
    // 1. Try the current position first
    if (is_rotation_valid_at(nX, nY, nZ, shape_pos_x, shape_pos_y, shape_pos_z)) {
        *out_x = shape_pos_x;
        *out_y = shape_pos_y;
        *out_z = shape_pos_z;
        return true;
    }

    // 2. Define the search space for "kicks" (one block in every direction)
    // We prioritize X/Y kicks, then Z kicks
    int8_t dx[] = {1, -1, 0, 0, 0, 0};
    int8_t dy[] = {0, 0, 1, -1, 0, 0};
    int8_t dz[] = {0, 0, 0, 0, 1, -1};

    for (uint8_t i = 0; i < 6; i++) {
        int8_t test_x = shape_pos_x + dx[i];
        int8_t test_y = shape_pos_y + dy[i];
        int8_t test_z = shape_pos_z + dz[i];

        if (is_rotation_valid_at(nX, nY, nZ, test_x, test_y, test_z)) {
            *out_x = test_x;
            *out_y = test_y;
            *out_z = test_z;
            return true;
        }
    }

    return false; // No valid kick found
}


/* ================= PIT BACKGROUND ================= */

static void draw_pit_background(uint16_t buf) {
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

    // 2. Draw the depth lines (corners and side grids) 
    // AND the bottom face grid
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
        
        // NEW: Bottom face vertical grid lines
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

        // NEW: Bottom face horizontal grid lines
        draw_line2buffer(GREEN, bx_left, by, bx_right, by, buf);
    }
}

/* ================= HUD ================= */

static void draw_static_hud(uint16_t buf) {
    int y = 10;
    set_cursor(SCREEN_WIDTH - 60, y);
    draw_string2buffer("BLOCKOUT", buf);
    
    y = SCREEN_HEIGHT - 70;
    set_cursor(0, y); 
    y += 10; set_cursor(8, y); draw_string2buffer("[SPC] PAUSE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("[Q/W/E] ROT", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("[ARRS] MOVE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("[M] MODE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("[P] PERSP", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("[+/-] ZOOM", buf);
}

static void update_static_buffer(void) {
    erase_buffer_sized(STATIC_BUFFER_ADDR, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    draw_static_hud(STATIC_BUFFER_ADDR);
    draw_pit_background(STATIC_BUFFER_ADDR);
    switch_buffer_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR);
}

/* ================= DRAW ================= */

static void drawShape(uint16_t buffer) {
    const Shape *s = &shapes[current_shape_idx];

    if (angleX != last_ax || angleY != last_ay || angleZ != last_az ||
        last_shape != current_shape_idx || last_zoom != zoom_level) {

        last_ax = angleX; last_ay = angleY; last_az = angleZ;
        last_shape = current_shape_idx;
        last_zoom = zoom_level;

        g_sinX = sine_values[angleX]; g_cosX = cosine_values[angleX];
        g_sinY = sine_values[angleY]; g_cosY = cosine_values[angleY];
        g_sinZ = sine_values[angleZ]; g_cosZ = cosine_values[angleZ];

        for (uint8_t b = 0; b < s->num_blocks; b++) {
            rotate_block_center(s->offsets[b], s->center, block_centers[b]);
        }

        for (uint8_t i = 0; i < 8; i++) {
            rotate_ref_vertex(ref_vertices[i], rot_ref_v[i]);
        }
    }

    // 1. Pivot offset (half-blocks to pixels)
    int16_t center_offset_x = (s->center[0] * GRID_SIZE) / 2;
    int16_t center_offset_y = (s->center[1] * GRID_SIZE) / 2;
    int16_t center_offset_z = (s->center[2] * PIT_Z_STEP) / 2;

    // 2. THE FIX: Add (GRID_SIZE / 2) to center the coordinate in the cell, 
    // THEN add the shape's specific pivot offset.
    int16_t base_world_x = (shape_pos_x * GRID_SIZE) + (GRID_SIZE / 2) - (VIEWPORT_WIDTH / 2) + center_offset_x;
    int16_t base_world_y = (shape_pos_y * GRID_SIZE) + (GRID_SIZE / 2) - (VIEWPORT_HEIGHT / 2) + center_offset_y;
    
    // Z also needs the half-step to sit inside the "layer"
    uint16_t base_zi = PIT_Z_START + (shape_pos_z * PIT_Z_STEP) + (PIT_Z_STEP / 2) + center_offset_z;

    for (uint8_t b = 0; b < s->num_blocks; b++) {
        int16_t block_offset_x = block_centers[b][0];
        int16_t block_offset_y = block_centers[b][1];
        int16_t block_offset_z = block_centers[b][2];

        for (uint8_t i = 0; i < 8; i++) {
            int16_t vertex_offset_x = (rot_ref_v[i][0] * CUBE_SIZE) / UNIT_SCALE;
            int16_t vertex_offset_y = (rot_ref_v[i][1] * CUBE_SIZE) / UNIT_SCALE;
            int16_t vertex_offset_z = (rot_ref_v[i][2] * CUBE_SIZE) / UNIT_SCALE;

            int32_t vx = base_world_x + block_offset_x + vertex_offset_x;
            int32_t vy = base_world_y + block_offset_y + vertex_offset_y;
            
            int16_t block_zi_offset = (block_offset_z * PIT_Z_STEP) / GRID_SIZE;
            int16_t vertex_zi_offset = (vertex_offset_z * PIT_Z_STEP) / GRID_SIZE;
            int16_t vertex_zi = base_zi - block_zi_offset - vertex_zi_offset;
            
            if (vertex_zi < 1) vertex_zi = 1;
            if (vertex_zi > 255) vertex_zi = 255;

            px[i] = apply_perspective((int16_t)vx, (uint8_t)vertex_zi) + (VIEWPORT_WIDTH >> 1);
            py[i] = apply_perspective((int16_t)vy, (uint8_t)vertex_zi) + (VIEWPORT_HEIGHT >> 1);
        }

        uint16_t color = WHITE;
        if (mode == 0) {
            uint16_t mask = s->edge_masks[b];
            for (uint8_t e = 0; e < 12; e++)
                if (!(mask & (1 << e)))
                    draw_line2buffer(color,
                        px[edges[e << 1]], py[edges[e << 1]],
                        px[edges[(e << 1) + 1]], py[edges[(e << 1) + 1]],
                        buffer);
        } else {
            for (uint8_t i = 0; i < 8; i++)
                draw_pixel2buffer(color, px[i], py[i], buffer);
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
    draw_string2buffer("x: ", STATIC_BUFFER_ADDR);
    fill_rect2buffer(0, 20, 20, 20, 20, STATIC_BUFFER_ADDR);
    set_cursor(20, 20);
    sprintf(text_buffer, "%u", shape_pos_y);
    draw_string2buffer(text_buffer, STATIC_BUFFER_ADDR);
    set_cursor(0, 30);
    draw_string2buffer("x: ", STATIC_BUFFER_ADDR);
    fill_rect2buffer(0, 20, 30, 20, 30, STATIC_BUFFER_ADDR);
    set_cursor(20, 30);
    sprintf(text_buffer, "%u", shape_pos_z);
    draw_string2buffer(text_buffer, STATIC_BUFFER_ADDR);

}

/* ================= INTERPOLATION ================= */

static inline uint8_t interpolate_angle(uint8_t cur, uint8_t tgt, uint8_t steps) {
    int16_t d = (int16_t)tgt - (int16_t)cur;
    if (d > 128) d -= 256;
    else if (d < -128) d += 256;
    return cur + (int8_t)(d / steps);
}

/* ================= MAIN ================= */

int main(void) {
    precompute_tables();

    init_graphics_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR,
        0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    init_graphics_plane(VIEWPORT_STRUCT_ADDR, viewport_buffers[0],
        1, VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);

    update_static_buffer();

    erase_buffer_sized(VIEWPORT_BUFFER_0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    erase_buffer_sized(VIEWPORT_BUFFER_1, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);

    uint8_t v = RIA.vsync;
    while (true) {
        if (v == RIA.vsync) continue;
        v = RIA.vsync;

        if (animating > 0) {
            angleX = interpolate_angle(angleX, targetX, animating);
            angleY = interpolate_angle(angleY, targetY, animating);
            angleZ = interpolate_angle(angleZ, targetZ, animating);

            animating--;
            if (animating == 0) {
                apply_rotation(targetX, targetY, targetZ);
            }
        }

        if (!paused || animating > 0) {
            uint16_t back_buffer = viewport_buffers[!active_buffer];
            erase_buffer_sized(back_buffer, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
            drawShape(back_buffer);
            switch_buffer_plane(VIEWPORT_STRUCT_ADDR, back_buffer);
            active_buffer = !active_buffer;
        }

        xregn(0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;
        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            keystates[i] = RIA.rw0;
        }

        if (!(keystates[0] & 1)) {
            if (!handled_key) {
                if (key(KEY_SPACE)) paused = !paused;
                if (key(KEY_Z)) {
                    current_shape_idx = (current_shape_idx + 1) % NUM_SHAPES;
                }
                if (key(KEY_M)) mode = (mode + 1) % NUM_MODES;
                
                if (key(KEY_LEFT)) {
                    if (is_position_valid(shape_pos_x - 1, shape_pos_y, shape_pos_z)) {
                        shape_pos_x--;
                    }
                }
                if (key(KEY_RIGHT)) {
                    if (is_position_valid(shape_pos_x + 1, shape_pos_y, shape_pos_z)) {
                        shape_pos_x++;
                    }
                }
                if (key(KEY_UP)) {
                    if (is_position_valid(shape_pos_x, shape_pos_y - 1, shape_pos_z)) {
                        shape_pos_y--;
                    }
                }
                if (key(KEY_DOWN)) {
                    if (is_position_valid(shape_pos_x, shape_pos_y + 1, shape_pos_z)) {
                        shape_pos_y++;
                    }
                }
                
                if (key(KEY_EQUAL) || key(KEY_KPEQUAL)) {
                    if (is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z - 1)) {
                        shape_pos_z--;
                    }
                }
                if (key(KEY_MINUS)) {
                    if (is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z + 1)) {
                        shape_pos_z++;
                    }
                }
                
                if (key(KEY_ESC)) break;

                if (animating == 0) {
                    int8_t kX, kY, kZ;
                    uint8_t nextX = targetX, nextY = targetY, nextZ = targetZ;

                    if (key(KEY_Q)) nextX += ANGLE_STEP_90;
                    if (key(KEY_W)) nextY += ANGLE_STEP_90;
                    if (key(KEY_E)) nextZ += ANGLE_STEP_90;
                    if (key(KEY_A)) nextX -= ANGLE_STEP_90;
                    if (key(KEY_S)) nextY -= ANGLE_STEP_90;
                    if (key(KEY_D)) nextZ -= ANGLE_STEP_90;

                    // Only run if a rotation key was actually pressed
                    if (nextX != targetX || nextY != targetY || nextZ != targetZ) {
                        if (try_wall_kick(nextX, nextY, nextZ, &kX, &kY, &kZ)) {
                            shape_pos_x = kX;
                            shape_pos_y = kY;
                            shape_pos_z = kZ;
                            targetX = nextX;
                            targetY = nextY;
                            targetZ = nextZ;
                            animating = ROTATION_STEPS;
                        }
                    }
                }

                handled_key = true;
                draw_shape_position();
            }
        } else {
            handled_key = false;
        }
    }
    return 0;
}