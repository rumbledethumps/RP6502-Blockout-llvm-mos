#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics_db.h"

/* ================= CONFIG ================= */

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 180
#define VIEWPORT_WIDTH 160
#define VIEWPORT_HEIGHT 160

#define VIEWPORT_X ((SCREEN_WIDTH - VIEWPORT_WIDTH) / 2)
#define VIEWPORT_Y ((SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2)

#define SCREEN_CENTER_X (VIEWPORT_WIDTH >> 1)
#define SCREEN_CENTER_Y (VIEWPORT_HEIGHT >> 1)

#define STATIC_STRUCT_ADDR   0xFE00
#define VIEWPORT_STRUCT_ADDR 0xFE80
#define STATIC_BUFFER_ADDR   0x0000
#define VIEWPORT_BUFFER_0    0x7200
#define VIEWPORT_BUFFER_1    0xA400

#define NUM_POINTS 256
#define CUBE_SIZE 64

#define PIT_WIDTH 4
#define PIT_DEPTH 4
#define PIT_HEIGHT 8

#define MAX_PIECE_BLOCKS 4

#define DROP_DELAY 200

#define ROTATION_STEPS 4
#define ANGLE_STEP_90 (256/4)

/* ================= GLOBAL STATE ================= */

static uint16_t viewport_buffers[2] = {
    VIEWPORT_BUFFER_0, VIEWPORT_BUFFER_1
};
static uint8_t active_buffer = 0;

static bool game_over = false;
static uint16_t score = 0;
static uint16_t level = 0;
static uint16_t lines_cleared = 0;

#define KEYBOARD_INPUT 0xFF10
#define KEYBOARD_BYTES 32
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
static uint8_t keystates[32];
static bool handled_key = false;

/* ================= LUTS ================= */

static int16_t sine_values[NUM_POINTS];
static int16_t cosine_values[NUM_POINTS];
static uint16_t persp_lut[256];

/* ================= TETROMINO SHAPES ================= */

typedef struct {
    uint8_t num_blocks;
    int8_t blocks[MAX_PIECE_BLOCKS][3];
} Tetromino;

static const Tetromino tetrominos[7] = {
    // I piece (vertical)
    {4, {{0,0,0}, {0,0,1}, {0,0,2}, {0,0,3}}},
    // O piece
    {4, {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}}},
    // T piece
    {4, {{0,0,0}, {1,0,0}, {2,0,0}, {1,0,1}}},
    // L piece
    {4, {{0,0,0}, {0,0,1}, {0,0,2}, {1,0,2}}},
    // J piece
    {4, {{1,0,0}, {1,0,1}, {1,0,2}, {0,0,2}}},
    // S piece
    {4, {{1,0,0}, {2,0,0}, {0,1,0}, {1,1,0}}},
    // Z piece
    {4, {{0,0,0}, {1,0,0}, {1,1,0}, {2,1,0}}}
};

// Edge masks matching the order in the cube_edges table
#define MASK_FACE_BACK   ((1<<0)|(1<<1)|(1<<2)|(1<<3))
#define MASK_FACE_FRONT  ((1<<4)|(1<<5)|(1<<6)|(1<<7))
#define MASK_FACE_LEFT   ((1<<3)|(1<<7)|(1<<8)|(1<<10))
#define MASK_FACE_RIGHT  ((1<<1)|(1<<5)|(1<<9)|(1<<11))
#define MASK_FACE_BOTTOM ((1<<0)|(1<<4)|(1<<8)|(1<<9))
#define MASK_FACE_TOP    ((1<<2)|(1<<6)|(1<<10)|(1<<11))

/* ================= GAME STATE ================= */

static uint8_t pit[PIT_WIDTH][PIT_DEPTH][PIT_HEIGHT];

typedef struct {
    Tetromino shape;
    int8_t x, y, z;
    uint8_t color;
    int8_t blocks[MAX_PIECE_BLOCKS][3];
} Piece;

static Piece current_piece;
static uint8_t drop_counter = 0;

/* ================= ANGLES ================= */

static uint8_t angleX = 0, angleY = 0, angleZ = 0;
static uint8_t targetX = 0, targetY = 0, targetZ = 0;
static uint8_t animating = 0;
static uint8_t pending_rotation_axis = 0;

// Store old piece geometry before rotation for animation
static int8_t old_piece_blocks[MAX_PIECE_BLOCKS][3];

/* ================= ROTATION CACHE ================= */

static int16_t g_sinX, g_cosX;
static int16_t g_sinY, g_cosY;
static int16_t g_sinZ, g_cosZ;

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

/* ================= PRECOMPUTE ================= */

static void precompute_tables(void) {
    for (uint16_t i=0;i<NUM_POINTS;i++) {
        sine_values[i]=fpsin(i*(32768/NUM_POINTS));
        cosine_values[i]=fpcos(i*(32768/NUM_POINTS));
    }
    persp_lut[0]=65535;
    for(uint16_t i=1;i<256;i++) persp_lut[i]=65536U/i;
}

/* ================= INTERPOLATION ================= */

static inline uint8_t interpolate_angle(uint8_t cur, uint8_t tgt, uint8_t steps) {
    int16_t d = (int16_t)tgt - (int16_t)cur;
    if(d > 128) d -= 256;
    else if(d < -128) d += 256;
    return cur + (int8_t)(d / steps);
}

/* ================= ROTATION ================= */

static void rotate_point_3d(int16_t x, int16_t y, int16_t z, int16_t *ox, int16_t *oy, int16_t *oz) {
    int32_t tx = x, ty = y, tz = z;
    int32_t nx, ny, nz;

    // 1. Rotate around Y (X and Z change)
    nx = (tx * g_cosY + tz * g_sinY) >> 12;
    nz = (tz * g_cosY - tx * g_sinY) >> 12;
    tx = nx; tz = nz;

    // 2. Rotate around X (Y and Z change)
    ny = (ty * g_cosX - tz * g_sinX) >> 12;
    nz = (ty * g_sinX + tz * g_cosX) >> 12;
    ty = ny; tz = nz;

    // 3. Rotate around Z (X and Y change)
    nx = (tx * g_cosZ - ty * g_sinZ) >> 12;
    ny = (tx * g_sinZ + ty * g_cosZ) >> 12;

    *ox = (int16_t)nx;
    *oy = (int16_t)ny;
    *oz = (int16_t)tz;
}

static inline int16_t apply_perspective(int16_t v, int16_t dist) {
    if (dist < 1) dist = 1;
    uint16_t idx = (uint16_t)dist >> 4;
    if (idx > 255) idx = 255;
    return (int16_t)(((int32_t)v * (int32_t)persp_lut[idx]) >> 16);
}

static void project_point(int16_t x3d, int16_t y3d, int16_t z3d,
                          int16_t *px, int16_t *py)
{
    int32_t z_view = (int32_t)z3d + 512; 
    
    if (z_view < 64) z_view = 64;

    int16_t sx = (int16_t)(((int32_t)x3d * 160) / z_view);
    int16_t sy = (int16_t)(((int32_t)y3d * 160) / z_view);

    *px = sx + (VIEWPORT_WIDTH  >> 1);
    *py = sy + (VIEWPORT_HEIGHT >> 1);
}

static void draw_line_safe(uint8_t color, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t buf, int16_t w, int16_t h) {
    if (x1 < 0 || x1 >= w || y1 < 0 || y1 >= h) return;
    if (x2 < 0 || x2 >= w || y2 < 0 || y2 >= h) return;
    draw_line2buffer(color, x1, y1, x2, y2, buf);
}

/* ================= HUD ================= */

static void draw_static_hud(uint16_t buf) {
    int y = 10;
    set_cursor(SCREEN_WIDTH - 70, y);
    draw_string2buffer("BLOCKOUT", buf);
    
    y = SCREEN_HEIGHT - 70;
    set_cursor(8, y); draw_string2buffer("SCORE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("LEVEL", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("LINES", buf);
    y += 20; set_cursor(8, y); draw_string2buffer("ARROWS:MOVE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("Q/W/E:ROTATE", buf);
    y += 10; set_cursor(8, y); draw_string2buffer("SPACE:DROP", buf);
}

static void draw_pit_background(uint16_t buf) {
    g_sinX = sine_values[angleX]; g_cosX = cosine_values[angleX];
    g_sinY = sine_values[angleY]; g_cosY = cosine_values[angleY];
    g_sinZ = sine_values[angleZ]; g_cosZ = cosine_values[angleZ];

    int16_t half_w = PIT_WIDTH * CUBE_SIZE; 
    int16_t half_d = PIT_DEPTH * CUBE_SIZE;
    int16_t height = PIT_HEIGHT * CUBE_SIZE * 2;

    int16_t offX = VIEWPORT_X + (VIEWPORT_WIDTH / 2);
    int16_t offY = VIEWPORT_Y + (VIEWPORT_HEIGHT / 2);

    int16_t corners[8][3] = {
        {-half_w,-half_d,0}, {half_w,-half_d,0}, {half_w,half_d,0}, {-half_w,half_d,0},
        {-half_w,-half_d,height}, {half_w,-half_d,height}, {half_w,half_d,height}, {-half_w,half_d,height}
    };

    int16_t px[8], py[8];
    for (uint8_t i = 0; i < 8; i++) {
        int16_t rx, ry, rz;
        rotate_point_3d(corners[i][0], corners[i][1], corners[i][2], &rx, &ry, &rz);
        int16_t dist = 48 + (rz >> 3);
        if (dist < 24) dist = 24;
        px[i] = apply_perspective(rx, dist) + offX;
        py[i] = apply_perspective(ry, dist) + offY;
    }

    uint8_t edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, 
        {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7}
    };

    for (uint8_t i = 0; i < 12; i++) {
        draw_line_safe(YELLOW, px[edges[i][0]], py[edges[i][0]], 
                      px[edges[i][1]], py[edges[i][1]], buf, 320, 180);
    }

    for (uint8_t j = 0; j <= PIT_HEIGHT; j++) {
        int16_t gz = j * (CUBE_SIZE * 2);
        for (uint8_t i = 0; i < 4; i++) {
            int16_t rx1, ry1, rz1, rx2, ry2, rz2;
            rotate_point_3d(corners[i][0], corners[i][1], gz, &rx1, &ry1, &rz1);
            rotate_point_3d(corners[(i+1)%4][0], corners[(i+1)%4][1], gz, &rx2, &ry2, &rz2);
            
            int16_t dist1 = 48 + (rz1 >> 3);
            int16_t dist2 = 48 + (rz2 >> 3);
            if (dist1 < 24) dist1 = 24;
            if (dist2 < 24) dist2 = 24;
            
            int16_t p1x = apply_perspective(rx1, dist1) + offX;
            int16_t p1y = apply_perspective(ry1, dist1) + offY;
            int16_t p2x = apply_perspective(rx2, dist2) + offX;
            int16_t p2y = apply_perspective(ry2, dist2) + offY;
            
            draw_line_safe(DARK_GRAY, p1x, p1y, p2x, p2y, buf, 320, 180);
        }
    }
}

/* ================= GAME LOGIC ================= */

static void init_game(void) {
    for (uint8_t x = 0; x < PIT_WIDTH; x++)
        for (uint8_t y = 0; y < PIT_DEPTH; y++)
            for (uint8_t z = 0; z < PIT_HEIGHT; z++)
                pit[x][y][z] = 0;
    
    score = 0;
    level = 0;
    lines_cleared = 0;
    game_over = false;
}

static void spawn_piece(void) {
    uint8_t type = 5;
    current_piece.shape = tetrominos[type];
    current_piece.x = PIT_WIDTH / 2 - 1;
    current_piece.y = PIT_DEPTH / 2 - 1;
    current_piece.z = PIT_HEIGHT - 2;
    current_piece.color = (type % 13) + 1;
    
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        current_piece.blocks[i][0] = current_piece.shape.blocks[i][0];
        current_piece.blocks[i][1] = current_piece.shape.blocks[i][1];
        current_piece.blocks[i][2] = current_piece.shape.blocks[i][2];
    }
}

static bool check_collision(int8_t dx, int8_t dy, int8_t dz) {
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        int8_t bx = current_piece.blocks[i][0];
        int8_t by = current_piece.blocks[i][1];
        int8_t bz = current_piece.blocks[i][2];
        
        int8_t nx = current_piece.x + bx + dx;
        int8_t ny = current_piece.y + by + dy;
        int8_t nz = current_piece.z + bz + dz;
        
        if (nx < 0 || nx >= PIT_WIDTH || 
            ny < 0 || ny >= PIT_DEPTH || 
            nz < 0) return true;
        
        if (nz < PIT_HEIGHT && pit[nx][ny][nz]) return true;
    }
    return false;
}

static void lock_piece(void) {
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        int8_t bx = current_piece.x + current_piece.blocks[i][0];
        int8_t by = current_piece.y + current_piece.blocks[i][1];
        int8_t bz = current_piece.z + current_piece.blocks[i][2];
        
        if (bz >= 0 && bz < PIT_HEIGHT && bx >= 0 && bx < PIT_WIDTH && by >= 0 && by < PIT_DEPTH) {
            pit[bx][by][bz] = current_piece.color;
        }
    }
}

static void check_lines(void) {
    for (int8_t z = 0; z < PIT_HEIGHT; z++) {
        bool full = true;
        for (uint8_t x = 0; x < PIT_WIDTH && full; x++)
            for (uint8_t y = 0; y < PIT_DEPTH && full; y++)
                if (!pit[x][y][z]) full = false;
        
        if (full) {
            lines_cleared++;
            score += 100;
            
            for (int8_t zz = z; zz < PIT_HEIGHT - 1; zz++)
                for (uint8_t x = 0; x < PIT_WIDTH; x++)
                    for (uint8_t y = 0; y < PIT_DEPTH; y++)
                        pit[x][y][zz] = pit[x][y][zz + 1];
            
            for (uint8_t x = 0; x < PIT_WIDTH; x++)
                for (uint8_t y = 0; y < PIT_DEPTH; y++)
                    pit[x][y][PIT_HEIGHT - 1] = 0;
            
            z--;
        }
    }
}

static bool try_rotate_piece_90(uint8_t axis) {
    int8_t new_blocks[MAX_PIECE_BLOCKS][3];
    
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        int8_t x = current_piece.blocks[i][0];
        int8_t y = current_piece.blocks[i][1];
        int8_t z = current_piece.blocks[i][2];
        
        if (axis == 0) {
            new_blocks[i][0] = x;
            new_blocks[i][1] = -z;
            new_blocks[i][2] = y;
        } else if (axis == 1) {
            new_blocks[i][0] = z;
            new_blocks[i][1] = y;
            new_blocks[i][2] = -x;
        } else {
            new_blocks[i][0] = -y;
            new_blocks[i][1] = x;
            new_blocks[i][2] = z;
        }
    }
    
    int8_t old_blocks[MAX_PIECE_BLOCKS][3];
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        old_blocks[i][0] = current_piece.blocks[i][0];
        old_blocks[i][1] = current_piece.blocks[i][1];
        old_blocks[i][2] = current_piece.blocks[i][2];
    }
    
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        current_piece.blocks[i][0] = new_blocks[i][0];
        current_piece.blocks[i][1] = new_blocks[i][1];
        current_piece.blocks[i][2] = new_blocks[i][2];
    }
    
    if (check_collision(0, 0, 0)) {
        for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
            current_piece.blocks[i][0] = old_blocks[i][0];
            current_piece.blocks[i][1] = old_blocks[i][1];
            current_piece.blocks[i][2] = old_blocks[i][2];
        }
        return false;
    }
    return true;
}

static void rotate_piece_90(uint8_t axis) {
    try_rotate_piece_90(axis);
}

/* ================= DRAW ================= */

static void draw_cube(int16_t cx, int16_t cy, int16_t cz, uint8_t color, uint16_t mask, uint16_t buffer) {
    int16_t px[8], py[8];
    int16_t s = CUBE_SIZE;

    for (uint8_t i = 0; i < 8; i++) {
        int16_t vx = (i & 1) ? s : -s;
        int16_t vy = (i & 2) ? s : -s;
        int16_t vz = (i & 4) ? s : -s;
        
        project_point(vx + cx, vy + cy, vz + cz, &px[i], &py[i]);
    }
    
    static const uint8_t cube_edges[12][2] = {
        {0,1}, {1,3}, {3,2}, {2,0},
        {4,5}, {5,7}, {7,6}, {6,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };

    for (uint8_t i = 0; i < 12; i++) {
        if (!(mask & (1 << i))) {
            int16_t x1 = px[cube_edges[i][0]], y1 = py[cube_edges[i][0]];
            int16_t x2 = px[cube_edges[i][1]], y2 = py[cube_edges[i][1]];

            if (x1 >= 0 && x1 < VIEWPORT_WIDTH && y1 >= 0 && y1 < VIEWPORT_HEIGHT &&
                x2 >= 0 && x2 < VIEWPORT_WIDTH && y2 >= 0 && y2 < VIEWPORT_HEIGHT) {
                draw_line2buffer(color, x1, y1, x2, y2, buffer);
            }
        }
    }
}

static void draw_game(uint16_t buffer) {
    if (animating > 0) {
        g_sinX = sine_values[angleX]; g_cosX = cosine_values[angleX];
        g_sinY = sine_values[angleY]; g_cosY = cosine_values[angleY];
        g_sinZ = sine_values[angleZ]; g_cosZ = cosine_values[angleZ];
    } else {
        g_sinX = 0; g_cosX = 4096;
        g_sinY = 0; g_cosY = 4096;
        g_sinZ = 0; g_cosZ = 4096;
    }
    
    int16_t world_ox = -(PIT_WIDTH * CUBE_SIZE);
    int16_t world_oy = -(PIT_DEPTH * CUBE_SIZE);

    for (uint8_t x = 0; x < PIT_WIDTH; x++) {
        for (uint8_t y = 0; y < PIT_DEPTH; y++) {
            for (uint8_t z = 0; z < PIT_HEIGHT; z++) {
                if (pit[x][y][z]) {
                    int16_t cx = world_ox + (x * CUBE_SIZE * 2) + CUBE_SIZE;
                    int16_t cy = world_oy + (y * CUBE_SIZE * 2) + CUBE_SIZE;
                    int16_t cz = (z * CUBE_SIZE * 2) + CUBE_SIZE;
                    draw_cube(cx, cy, cz, pit[x][y][z], 0, buffer);
                }
            }
        }
    }

    // Use the correct array based on animation state
    int8_t (*blocks_to_draw)[3] = animating > 0 ? old_piece_blocks : current_piece.blocks;

    // Draw each block of the falling piece
    for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
        uint16_t mask = 0;
        int8_t bx = blocks_to_draw[i][0];
        int8_t by = blocks_to_draw[i][1];
        int8_t bz = blocks_to_draw[i][2];

        // Calculate adjacency mask
        for (uint8_t j = 0; j < current_piece.shape.num_blocks; j++) {
            if (i == j) continue;
            int8_t dx = blocks_to_draw[j][0] - bx;
            int8_t dy = blocks_to_draw[j][1] - by;
            int8_t dz = blocks_to_draw[j][2] - bz;
            
            if (dx == 1 && dy == 0 && dz == 0) mask |= MASK_FACE_RIGHT;
            if (dx == -1 && dy == 0 && dz == 0) mask |= MASK_FACE_LEFT;
            if (dy == 1 && dx == 0 && dz == 0) mask |= MASK_FACE_TOP;
            if (dy == -1 && dx == 0 && dz == 0) mask |= MASK_FACE_BOTTOM;
            if (dz == 1 && dx == 0 && dy == 0) mask |= MASK_FACE_FRONT;
            if (dz == -1 && dx == 0 && dy == 0) mask |= MASK_FACE_BACK;
        }

        // Convert block grid coordinates to world coordinates
        int16_t block_world_x = bx * CUBE_SIZE * 2;
        int16_t block_world_y = by * CUBE_SIZE * 2;
        int16_t block_world_z = bz * CUBE_SIZE * 2;
        
        // Apply rotation if animating
        int16_t rx, ry, rz;
        if (animating > 0) {
            rotate_point_3d(block_world_x, block_world_y, block_world_z, &rx, &ry, &rz);
        } else {
            rx = block_world_x;
            ry = block_world_y;
            rz = block_world_z;
        }
        
        // Add piece position and world offset
        int16_t piece_world_x = current_piece.x * CUBE_SIZE * 2;
        int16_t piece_world_y = current_piece.y * CUBE_SIZE * 2;
        int16_t piece_world_z = current_piece.z * CUBE_SIZE * 2;
        
        int16_t cx = world_ox + piece_world_x + rx + CUBE_SIZE;
        int16_t cy = world_oy + piece_world_y + ry + CUBE_SIZE;
        int16_t cz = piece_world_z + rz + CUBE_SIZE;
        
        draw_cube(cx, cy, cz, current_piece.color, mask, buffer);
    }
}

/* ================= MAIN ================= */

int main(void) {
    precompute_tables();
    
    init_graphics_plane(STATIC_STRUCT_ADDR,STATIC_BUFFER_ADDR,
        1,0,0,SCREEN_WIDTH,SCREEN_HEIGHT,4);
    init_graphics_plane(VIEWPORT_STRUCT_ADDR,viewport_buffers[0],
        0,VIEWPORT_X,VIEWPORT_Y,VIEWPORT_WIDTH,VIEWPORT_HEIGHT,4);

    erase_buffer_sized(STATIC_BUFFER_ADDR,SCREEN_WIDTH,SCREEN_HEIGHT,4);
    draw_static_hud(STATIC_BUFFER_ADDR);
    draw_pit_background(STATIC_BUFFER_ADDR);
    switch_buffer_plane(STATIC_STRUCT_ADDR,STATIC_BUFFER_ADDR);

    erase_buffer_sized(VIEWPORT_BUFFER_0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    erase_buffer_sized(VIEWPORT_BUFFER_1, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    
    init_game();
    spawn_piece();
    
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
                angleX = 0;
                angleY = 0;
                angleZ = 0;
                targetX = 0;
                targetY = 0;
                targetZ = 0;
            }
        }
        
        if (!game_over) {
            drop_counter++;
            if (drop_counter >= DROP_DELAY) {
                drop_counter = 0;
                if (!check_collision(0, 0, -1)) {
                    current_piece.z--;
                } else {
                    lock_piece();
                    check_lines();
                    spawn_piece();
                    if (check_collision(0, 0, 0)) {
                        game_over = true;
                    }
                }
            }
        }
        
        uint16_t back_buffer = viewport_buffers[!active_buffer];
        erase_buffer_sized(back_buffer, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
        draw_game(back_buffer);
        switch_buffer_plane(VIEWPORT_STRUCT_ADDR, back_buffer);
        active_buffer = !active_buffer;
        
        xregn(0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;
        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            keystates[i] = RIA.rw0;
        }
        
        if (!(keystates[0] & 1)) {
            if (!handled_key) {
                if (key(KEY_ESC)) break;
                
                if (!game_over) {
                    if (key(KEY_LEFT) && !check_collision(-1, 0, 0)) current_piece.x--;
                    if (key(KEY_RIGHT) && !check_collision(1, 0, 0)) current_piece.x++;
                    if (key(KEY_UP) && !check_collision(0, -1, 0)) current_piece.y--;
                    if (key(KEY_DOWN) && !check_collision(0, 1, 0)) current_piece.y++;
                    
                    if (key(KEY_SPACE)) {
                        while (!check_collision(0, 0, -1)) {
                            current_piece.z--;
                        }
                        drop_counter = DROP_DELAY;
                    }
                    
                    if (animating == 0) {
                        if (key(KEY_Q)) {
                            for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
                                old_piece_blocks[i][0] = current_piece.blocks[i][0];
                                old_piece_blocks[i][1] = current_piece.blocks[i][1];
                                old_piece_blocks[i][2] = current_piece.blocks[i][2];
                            }
                            if (try_rotate_piece_90(0)) {
                                pending_rotation_axis = 0;
                                targetX = ANGLE_STEP_90;
                                targetY = 0;
                                targetZ = 0;
                                angleX = 0;
                                angleY = 0;
                                angleZ = 0;
                                animating = ROTATION_STEPS;
                            }
                        }
                        if (key(KEY_W)) {
                            for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
                                old_piece_blocks[i][0] = current_piece.blocks[i][0];
                                old_piece_blocks[i][1] = current_piece.blocks[i][1];
                                old_piece_blocks[i][2] = current_piece.blocks[i][2];
                            }
                            if (try_rotate_piece_90(1)) {
                                pending_rotation_axis = 1;
                                targetY = ANGLE_STEP_90;
                                targetX = 0;
                                targetZ = 0;
                                angleX = 0;
                                angleY = 0;
                                angleZ = 0;
                                animating = ROTATION_STEPS;
                            }
                        }
                        if (key(KEY_E)) {
                            for (uint8_t i = 0; i < current_piece.shape.num_blocks; i++) {
                                old_piece_blocks[i][0] = current_piece.blocks[i][0];
                                old_piece_blocks[i][1] = current_piece.blocks[i][1];
                                old_piece_blocks[i][2] = current_piece.blocks[i][2];
                            }
                            if (try_rotate_piece_90(2)) {
                                pending_rotation_axis = 2;
                                targetZ = ANGLE_STEP_90;
                                targetX = 0;
                                targetY = 0;
                                angleX = 0;
                                angleY = 0;
                                angleZ = 0;
                                animating = ROTATION_STEPS;
                            }
                        }
                    }
                }
                
                handled_key = true;
            }
        } else {
            handled_key = false;
        }
    }
    return 0;
}