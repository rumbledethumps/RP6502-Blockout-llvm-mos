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
#define CUBE_SIZE 4096

#define MAX_BLOCKS 4
#define NUM_SHAPES 5
#define NUM_ZOOM_LEVELS 8
#define NUM_MODES 2

#define ROTATION_STEPS 4
#define ANGLE_STEP_90 (256/4)

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

/* ================= LUTS ================= */

static int16_t sine_values[NUM_POINTS];
static int16_t cosine_values[NUM_POINTS];
static uint16_t persp_lut[256];

static const uint16_t zoom_lut[NUM_ZOOM_LEVELS] = {
    1024, 896, 768, 640, 512, 384, 256, 128
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
} Shape;

static const Shape shapes[NUM_SHAPES] = {
    {1,"CUBE",{{0,0,0}}, {0}},
    {3,"I",{{0,-1,0},{0,0,0},{0,1,0}},
        {MASK_FACE_TOP, MASK_FACE_TOP|MASK_FACE_BOTTOM, MASK_FACE_BOTTOM}},
    {3,"L",{{0,-1,0},{0,0,0},{1,0,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT, MASK_FACE_LEFT}},
    {4,"T",{{-1,0,0},{0,0,0},{1,0,0},{0,-1,0}},
        {MASK_FACE_RIGHT, MASK_FACE_LEFT|MASK_FACE_RIGHT|MASK_FACE_BOTTOM,
         MASK_FACE_LEFT, MASK_FACE_TOP}},
    {4,"S",{{0,-1,0},{0,0,0},{1,0,0},{1,1,0}},
        {MASK_FACE_TOP, MASK_FACE_BOTTOM|MASK_FACE_RIGHT,
         MASK_FACE_LEFT|MASK_FACE_TOP, MASK_FACE_BOTTOM}}
};

static uint8_t current_shape_idx = 0;

/* ================= GEOMETRY ================= */

static const int16_t ref_vertices[8][3] = {
    {-CUBE_SIZE,-CUBE_SIZE,-CUBE_SIZE},
    { CUBE_SIZE,-CUBE_SIZE,-CUBE_SIZE},
    { CUBE_SIZE, CUBE_SIZE,-CUBE_SIZE},
    {-CUBE_SIZE, CUBE_SIZE,-CUBE_SIZE},
    {-CUBE_SIZE,-CUBE_SIZE, CUBE_SIZE},
    { CUBE_SIZE,-CUBE_SIZE, CUBE_SIZE},
    { CUBE_SIZE, CUBE_SIZE, CUBE_SIZE},
    {-CUBE_SIZE, CUBE_SIZE, CUBE_SIZE}
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
    return (v * (int32_t)persp_lut[zi]) >> 9;
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


static void rotate_block_center(const int8_t *o, int16_t *v) {
    int16_t tmp[3] = {
        o[0] * (CUBE_SIZE * 2),
        o[1] * (CUBE_SIZE * 2),
        o[2] * (CUBE_SIZE * 2)
    };
    rotate_ref_vertex(tmp, v);
}



/* ================= HUD ================= */

static void draw_static_hud(uint16_t buf) {
    int y=SCREEN_HEIGHT-70;
    set_cursor(8,y); draw_string2buffer("[S] SHAPE",buf);
    y+=10; set_cursor(8,y); draw_string2buffer("[SPACE] PAUSE",buf);
    y+=10; set_cursor(8,y); draw_string2buffer("[Q/W/E] ROTATE",buf);
    y+=10; set_cursor(8,y); draw_string2buffer("[M] MODE",buf);
    y+=10; set_cursor(8,y); draw_string2buffer("[P] PERSPECTIVE",buf);
    y+=10; set_cursor(8,y); draw_string2buffer("[UP/DN] ZOOM",buf);
}

/* ================= DRAW ================= */

static void drawShape(uint16_t buffer) {
    const Shape *s=&shapes[current_shape_idx];

    if(angleX!=last_ax||angleY!=last_ay||angleZ!=last_az||
       last_shape!=current_shape_idx||last_zoom!=zoom_level) {

        last_ax=angleX; last_ay=angleY; last_az=angleZ;
        last_shape=current_shape_idx;
        last_zoom=zoom_level;

        g_sinX=sine_values[angleX]; g_cosX=cosine_values[angleX];
        g_sinY=sine_values[angleY]; g_cosY=cosine_values[angleY];
        g_sinZ=sine_values[angleZ]; g_cosZ=cosine_values[angleZ];

        for(uint8_t i=0;i<8;i++) {
            rotate_ref_vertex(ref_vertices[i],rot_ref_v[i]);
            scaled_ref_v[i][0] =
                (int16_t)(((int32_t)rot_ref_v[i][0] * zoom_lut[zoom_level]) >> 19);
            scaled_ref_v[i][1] =
                (int16_t)(((int32_t)rot_ref_v[i][1] * zoom_lut[zoom_level]) >> 19);
            scaled_ref_v[i][2] =
                (int16_t)(((int32_t)rot_ref_v[i][2] * zoom_lut[zoom_level]) >> 19);

        }
        for(uint8_t b=0;b<s->num_blocks;b++) {
            rotate_block_center(s->offsets[b], block_centers[b]);

            scaled_block_centers[b][0] =
                (int16_t)(((int32_t)block_centers[b][0] * zoom_lut[zoom_level]) >> 19);
            scaled_block_centers[b][1] =
                (int16_t)(((int32_t)block_centers[b][1] * zoom_lut[zoom_level]) >> 19);
            scaled_block_centers[b][2] =
                (int16_t)(((int32_t)block_centers[b][2] * zoom_lut[zoom_level]) >> 19);
        }
    }

    for(uint8_t b=0;b<s->num_blocks;b++) {
        int16_t *c = scaled_block_centers[b];
        for(uint8_t i=0;i<8;i++) {
            int16_t vx=scaled_ref_v[i][0]+c[0];
            int16_t vy=scaled_ref_v[i][1]+c[1];
            int16_t vz=scaled_ref_v[i][2]+c[2];
            int16_t dz = 128 + vz;
            if (dz < 64) dz = 64;
            else if (dz > 512) dz = 512;
            uint8_t zi = (uint8_t)(dz >> 1);

            if(perspective_enabled) {
                px[i]=apply_perspective(vx,zi)+SCREEN_CENTER_X;
                py[i]=apply_perspective(vy,zi)+SCREEN_CENTER_Y;
            } else {
                px[i]=vx+SCREEN_CENTER_X;
                py[i]=vy+SCREEN_CENTER_Y;
            }
        }

        uint8_t color=(current_shape_idx%14)+1;
        if(mode==0) {
            uint16_t mask=s->edge_masks[b];
            for(uint8_t e=0;e<12;e++)
                if(!(mask&(1<<e)))
                    draw_line2buffer(color,
                        px[edges[e<<1]],py[edges[e<<1]],
                        px[edges[(e<<1)+1]],py[edges[(e<<1)+1]],
                        buffer);
        } else {
            for(uint8_t i=0;i<8;i++)
                draw_pixel2buffer(color,px[i],py[i],buffer);
        }
    }

    set_cursor(8,8);
    draw_string2buffer("Shape:",buffer);
    draw_string2buffer(s->name,buffer);
}

/* ================= INTERPOLATION ================= */

static inline uint8_t interpolate_angle(uint8_t cur,uint8_t tgt,uint8_t steps) {
    int16_t d=(int16_t)tgt-(int16_t)cur;
    if(d>128) d-=256;
    else if(d<-128) d+=256;
    return cur+(int8_t)(d/steps);
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
    switch_buffer_plane(STATIC_STRUCT_ADDR,STATIC_BUFFER_ADDR);

    uint8_t v=RIA.vsync;
    while (true) {
        if (v == RIA.vsync) continue;
        v = RIA.vsync;

        // *** MODIFIED *** Handle animation using the new helper function
        if (animating > 0) {
            angleX = interpolate_angle(angleX, targetX, animating);
            angleY = interpolate_angle(angleY, targetY, animating);
            angleZ = interpolate_angle(angleZ, targetZ, animating);

            animating--;
            // On the last step, snap to the exact target value to prevent rounding errors.
            if (animating == 0) {
                angleX = targetX;
                angleY = targetY;
                angleZ = targetZ;
            }
        }

        if(!paused || animating > 0) {
            uint16_t back_buffer = viewport_buffers[!active_buffer];
            erase_buffer_sized(back_buffer, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
            drawShape(back_buffer);
            switch_buffer_plane(VIEWPORT_STRUCT_ADDR, back_buffer);
            active_buffer = !active_buffer;
        }

        xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;
        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            keystates[i] = RIA.rw0;
        }

        if (!(keystates[0] & 1)) {
            if (!handled_key) {
                if (key(KEY_SPACE)) paused = !paused;
                if (key(KEY_S)) { current_shape_idx = (current_shape_idx + 1) % NUM_SHAPES; }
                if (key(KEY_M)) mode = (mode + 1) % NUM_MODES;
                if (key(KEY_P)) perspective_enabled = !perspective_enabled;
                if (key(KEY_UP)) {
                    if (zoom_level > 0) zoom_level--;
                }
                if (key(KEY_DOWN)) {
                    if (zoom_level < NUM_ZOOM_LEVELS - 1) zoom_level++;
                }
                if (key(KEY_ESC)) break;

                if (animating == 0) {
                    if (key(KEY_Q)) { targetX = (angleX + ANGLE_STEP_90); targetY = angleY; targetZ = angleZ; animating = ROTATION_STEPS; }
                    if (key(KEY_W)) { targetY = (angleY + ANGLE_STEP_90); targetX = angleX; targetZ = angleZ; animating = ROTATION_STEPS; }
                    if (key(KEY_E)) { targetZ = (angleZ + ANGLE_STEP_90); targetX = angleX; targetY = angleY; animating = ROTATION_STEPS; }
                }
                handled_key = true;
                printf("zoom: %d\n", zoom_level);
            }
        } else {
            handled_key = false;
        }
    }
    return 0;
}
