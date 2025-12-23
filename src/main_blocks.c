#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics_db.h"

// --- Constants Optimized for int16 math ---
#define SCALE 96
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 180
#define VIEWPORT_WIDTH 160
#define VIEWPORT_HEIGHT 160
#define OFFSET_X 30
#define OFFSET_Y 0

// Geometry Scale
#define CUBE_SIZE 32        
#define BLOCK_SPACING 64    

// Math Constants
#define NUM_POINTS 256
// We use a scale of 128 (2^7) for trig to allow fast shifting
#define TRIG_SHIFT 7 

// Buffers
#define STATIC_STRUCT_ADDR   0xFE00
#define VIEWPORT_STRUCT_ADDR 0xFE80
#define STATIC_BUFFER_ADDR   0x0000 
#define VIEWPORT_BUFFER_0    0x7200 
#define VIEWPORT_BUFFER_1    0xA400 

uint16_t viewport_buffers[2] = {VIEWPORT_BUFFER_0, VIEWPORT_BUFFER_1};
uint8_t active_buffer = 0;

int16_t distance = 1; 
char *buf[] = {"                                                                  "};
bool paused = false;
bool perspective_enabled = true;

// Keyboard
#define KEYBOARD_INPUT 0xFF10 
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

// Fast Math Lookup Tables
int16_t sine_values[NUM_POINTS];
int16_t cosine_values[NUM_POINTS];
uint16_t persp_lut[512]; 

// Scratchpad 
static int16_t g_sinX, g_cosX, g_sinY, g_cosY, g_sinZ, g_cosZ;
static int16_t rot_ref_x[8], rot_ref_y[8], rot_ref_z[8];

const int16_t ref_vertices[8][3] = {
    {-CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE}, {CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE}, 
    {CUBE_SIZE, CUBE_SIZE, -CUBE_SIZE}, {-CUBE_SIZE, CUBE_SIZE, -CUBE_SIZE},  
    {-CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE}, {CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE}, 
    {CUBE_SIZE, CUBE_SIZE,  CUBE_SIZE}, {-CUBE_SIZE, CUBE_SIZE,  CUBE_SIZE}   
};

// Shape Data
#define MAX_BLOCKS 4
#define NUM_SHAPES 5

typedef struct {
    uint8_t num_blocks;
    int8_t offsets[MAX_BLOCKS][3]; 
    char* name;
} Shape;

const Shape shapes[NUM_SHAPES] = {
    { 1, {{0,0,0}}, "CUBE" },
    { 3, {{0,-1,0}, {0,0,0}, {0,1,0}}, "I-PIECE" },
    { 3, {{0,-1,0}, {0,0,0}, {1,0,0}}, "L-PIECE" },
    { 4, {{-1,0,0}, {0,0,0}, {1,0,0}, {0,-1,0}}, "T-PIECE" },
    { 4, {{0,-1,0}, {0,0,0}, {1,0,0}, {1,1,0}}, "S-PIECE" }
};
uint8_t current_shape_idx = 0;

// Rotation State
uint16_t angleX = 0, angleY = 0, angleZ = 0;
uint16_t targetX = 0, targetY = 0, targetZ = 0;
uint8_t animating = 0;
#define ROTATION_STEPS 4
#define ANGLE_STEP_90 64

// --- USER PROVIDED FIXED POINT MATH ---
enum {cA1 = 3370945099UL, cB1 = 2746362156UL, cC1 = 292421UL};
enum {n = 13, p = 32, q = 31, r = 3, a = 12};

// Fixed-point sine
int16_t fpsin(int16_t i) {
    i <<= 1;
    uint8_t c = i < 0; 
    if(i == (i | 0x4000)) i = (1 << 15) - i;
    i = (i & 0x7FFF) >> 1;
    uint32_t y = (cC1 * ((uint32_t)i)) >> n;
    y = cB1 - (((uint32_t)i * y) >> r);
    y = (uint32_t)i * (y >> n);
    y = (uint32_t)i * (y >> n);
    y = cA1 - (y >> (p - q));
    y = (uint32_t)i * (y >> n);
    y = (y + (1UL << (q - a - 1))) >> (q - a); 
    return c ? -y : y;
}
#define fpcos(i) fpsin((int16_t)(((uint16_t)(i)) + 8192U))

void precompute_sin_cos() {
    int16_t angle_step = 32768 / NUM_POINTS; 
    for (int i = 0; i < NUM_POINTS; i++) {
        // Optimization: Shift right by 8.
        // fpsin returns +/- 32767 (approx). We need +/- 128 for fast multiplication logic.
        sine_values[i] = fpsin(i * angle_step) >> 8;
        cosine_values[i] = fpcos(i * angle_step) >> 8;
    }
}

void precompute_perspective_lut() {
    persp_lut[0] = 65535;
    for (uint16_t i = 1; i < 512; i++) {
        persp_lut[i] = (uint16_t)(32768UL / i); 
    }
}

// Optimized Rotation: Uses int16 multiplication
void rotate_point_fast(int16_t x, int16_t y, int16_t z, int16_t* ox, int16_t* oy, int16_t* oz) {
    // Y Rotation
    // (x * cos - z * sin) >> 7
    int16_t rx = ((x * g_cosY) + (z * g_sinY)) >> TRIG_SHIFT;
    int16_t rz = ((z * g_cosY) - (x * g_sinY)) >> TRIG_SHIFT;
    int16_t ry = y;
    
    // X Rotation
    int16_t rxx = rx;
    int16_t ryy = ((ry * g_cosX) - (rz * g_sinX)) >> TRIG_SHIFT;
    int16_t rzz = ((ry * g_sinX) + (rz * g_cosX)) >> TRIG_SHIFT;
    
    // Z Rotation
    *ox = ((rxx * g_cosZ) - (ryy * g_sinZ)) >> TRIG_SHIFT;
    *oy = ((rxx * g_sinZ) + (ryy * g_cosZ)) >> TRIG_SHIFT;
    *oz = rzz;
}

// Logic to hide shared edges
bool is_shared_edge(const Shape* s, uint8_t b1, uint8_t edge_idx, uint8_t b2) {
    int8_t dx = s->offsets[b2][0] - s->offsets[b1][0];
    int8_t dy = s->offsets[b2][1] - s->offsets[b1][1];
    int8_t dz = s->offsets[b2][2] - s->offsets[b1][2];

    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || dz < -1 || dz > 1) return false;
    if (dx == 0 && dy == 0 && dz == 0) return false;

    if (dx == 1) { // Right
        if (edge_idx == 1 || edge_idx == 5 || edge_idx == 9 || edge_idx == 10) return true;
    } else if (dx == -1) { // Left
        if (edge_idx == 3 || edge_idx == 7 || edge_idx == 8 || edge_idx == 11) return true;
    } else if (dy == 1) { // Top
        if (edge_idx == 2 || edge_idx == 6 || edge_idx == 10 || edge_idx == 11) return true;
    } else if (dy == -1) { // Bottom
        if (edge_idx == 0 || edge_idx == 4 || edge_idx == 8 || edge_idx == 9) return true;
    } else if (dz == 1) { // Front
        if (edge_idx >= 4 && edge_idx <= 7) return true;
    } else if (dz == -1) { // Back
        if (edge_idx <= 3) return true;
    }
    return false;
}

#define SCREEN_CENTER_X 80
#define SCREEN_CENTER_Y 80

void drawShape(uint8_t aX, uint8_t aY, uint8_t aZ, int16_t color, uint8_t mode, uint16_t buffer_addr) {
    // 1. Cache Trig
    g_sinX = sine_values[aX]; g_cosX = cosine_values[aX];
    g_sinY = sine_values[aY]; g_cosY = cosine_values[aY];
    g_sinZ = sine_values[aZ]; g_cosZ = cosine_values[aZ];

    // 2. Rotate the standard cube vertices once (Local Space)
    for(uint8_t i=0; i<8; i++) {
        rotate_point_fast(ref_vertices[i][0], ref_vertices[i][1], ref_vertices[i][2], 
                     &rot_ref_x[i], &rot_ref_y[i], &rot_ref_z[i]);
    }

    const Shape* s = &shapes[current_shape_idx];
    const uint8_t edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7}
    };

    // 3. Process each block
    for(uint8_t b=0; b < s->num_blocks; b++) {
        
        // Calculate block center in world space
        int16_t bx = (int16_t)s->offsets[b][0] * (int16_t)BLOCK_SPACING;
        int16_t by = (int16_t)s->offsets[b][1] * (int16_t)BLOCK_SPACING;
        int16_t bz = (int16_t)s->offsets[b][2] * (int16_t)BLOCK_SPACING;

        // Rotate just the center
        int16_t cx, cy, cz;
        rotate_point_fast(bx, by, bz, &cx, &cy, &cz);

        int16_t px[8], py[8];
        
        // 4. Compute screen coordinates
        for(uint8_t i=0; i<8; i++) {
            // Add rotated vertex offset to rotated center
            int16_t vx = rot_ref_x[i] + cx;
            int16_t vy = rot_ref_y[i] + cy;
            // Z push back
            int16_t vz = rot_ref_z[i] + cz + 256 + (distance * 32); 

            if (perspective_enabled) {
                if (vz < 16) vz = 16;       
                if (vz > 511) vz = 511;     
                
                uint16_t inv = persp_lut[vz];
                px[i] = ((int32_t)vx * inv >> 9) + SCREEN_CENTER_X + OFFSET_X;
                py[i] = ((int32_t)vy * inv >> 9) + SCREEN_CENTER_Y + OFFSET_Y;
            } else {
                px[i] = (vx >> distance) + SCREEN_CENTER_X + OFFSET_X;
                py[i] = (vy >> distance) + SCREEN_CENTER_Y + OFFSET_Y;
            }
        }

        // 5. Draw Edges
        if (mode == 0) {
            for(uint8_t e = 0; e < 12; e++) {
                bool is_shared = false;
                for(uint8_t other = 0; other < s->num_blocks; other++) {
                    if (other == b) continue;
                    if (is_shared_edge(s, b, e, other)) {
                        is_shared = true;
                        break;
                    }
                }
                
                if (!is_shared) {
                    draw_line2buffer(color, px[edges[e][0]], py[edges[e][0]], px[edges[e][1]], py[edges[e][1]], buffer_addr);
                }
            }
        } 
        else if (mode == 1) {
            for(uint8_t i=0; i<8; i++) {
                draw_pixel2buffer(color, px[i], py[i], buffer_addr);
            }
        }
    }

    set_cursor(10, 10);
    sprintf(*buf, "Shape: %s", s->name);
    draw_string2buffer(*buf, buffer_addr);
}

void draw_static_hud(uint16_t buffer_addr) {
    int16_t y_pos = SCREEN_HEIGHT - 90;
    set_cursor(10, y_pos); draw_string2buffer("[S]     SWITCH SHAPE", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[SPACE] PAUSE/RESUME", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[Q/W/E] ROTATE X/Y/Z", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[M]     DRAW MODE", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[P]     PERSPECTIVE", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[UP/DN] ZOOM", buffer_addr);
    y_pos+=10; set_cursor(10, y_pos); draw_string2buffer("[ESC]   EXIT", buffer_addr);
}

int main() {
    precompute_sin_cos();
    precompute_perspective_lut();

    bool handled_key = false;
    uint8_t mode = 0;
    
    init_graphics_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR, 1, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    init_graphics_plane(VIEWPORT_STRUCT_ADDR, viewport_buffers[0], 0, (SCREEN_WIDTH-VIEWPORT_WIDTH)/2, (SCREEN_HEIGHT-VIEWPORT_HEIGHT)/2, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    
    erase_buffer_sized(STATIC_BUFFER_ADDR, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    draw_static_hud(STATIC_BUFFER_ADDR);
    switch_buffer_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR);
    
    erase_buffer_sized(VIEWPORT_BUFFER_0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    erase_buffer_sized(VIEWPORT_BUFFER_1, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    switch_buffer_plane(VIEWPORT_STRUCT_ADDR, VIEWPORT_BUFFER_0);

    uint8_t v = RIA.vsync;

    while (true) {
        if (v == RIA.vsync) continue;
        v = RIA.vsync;

        // --- Animation Logic ---
        if (animating > 0) {
            int8_t diffX = (int8_t)(targetX - angleX);
            int8_t diffY = (int8_t)(targetY - angleY);
            int8_t diffZ = (int8_t)(targetZ - angleZ);
            
            angleX = (angleX + (diffX / animating)) & 0xFF;
            angleY = (angleY + (diffY / animating)) & 0xFF;
            angleZ = (angleZ + (diffZ / animating)) & 0xFF;
            
            animating--;
            if (animating == 0) {
                angleX = targetX; angleY = targetY; angleZ = targetZ;
            }
        }

        if(!paused || animating > 0) {
            uint16_t back_buffer = viewport_buffers[!active_buffer];
            erase_buffer_sized(back_buffer, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
            
            drawShape(angleX, angleY, angleZ, current_shape_idx+1, mode, back_buffer);
            
            switch_buffer_plane(VIEWPORT_STRUCT_ADDR, back_buffer);
            active_buffer = !active_buffer;
        }

        // --- Input ---
        xregn(0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT; RIA.step0 = 0;
        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) keystates[i] = RIA.rw0;

        if (!(keystates[0] & 1)) {
            if (!handled_key) { 
                if (key(KEY_SPACE)) paused = !paused;
                if (key(KEY_S)) {
                    current_shape_idx++;
                    if(current_shape_idx >= NUM_SHAPES) current_shape_idx = 0;
                }
                if (key(KEY_Q) && animating == 0) {
                    targetX = (angleX + ANGLE_STEP_90) & 0xFF; targetY = angleY; targetZ = angleZ; animating = ROTATION_STEPS;
                }
                if (key(KEY_W) && animating == 0) {
                    targetX = angleX; targetY = (angleY + ANGLE_STEP_90) & 0xFF; targetZ = angleZ; animating = ROTATION_STEPS;
                }
                if (key(KEY_E) && animating == 0) {
                    targetX = angleX; targetY = angleY; targetZ = (angleZ + ANGLE_STEP_90) & 0xFF; animating = ROTATION_STEPS;
                }
                if (key(KEY_M)) mode = ((mode + 1) >= 2 ? 0 : (mode + 1));
                if (key(KEY_P)) perspective_enabled = !perspective_enabled;
                if (key(KEY_UP)) if (distance < 8) distance++;
                if (key(KEY_DOWN)) if (distance > 0) distance--;
                if (key(KEY_ESC)) break;
                handled_key = true;
            }
        } else {
            handled_key = false;
        }
    }
    return 0;
}
