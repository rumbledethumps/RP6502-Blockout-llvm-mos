#ifndef BLOCKOUT_MATH_H
#define BLOCKOUT_MATH_H

#include <stdint.h>
#include <stdbool.h>
#include "blockout_types.h"

/* ================= LUTS ================= */

extern const int16_t sine_values[256];
extern const int16_t cosine_values[256];


extern uint16_t persp_lut[256];

extern const uint16_t zoom_lut[NUM_ZOOM_LEVELS];

// Cache for screen coordinates of every grid point
// Dimensions: [Z levels][Depth Y][Width X]
extern int16_t grid_sx[MAX_PIT_HEIGHT + 1][MAX_PIT_DEPTH + 1][MAX_PIT_WIDTH + 1];
extern int16_t grid_sy[MAX_PIT_HEIGHT + 1][MAX_PIT_DEPTH + 1];


/* ================= GEOMETRY ================= */

#define UNIT_SCALE 1024 

extern const int16_t ref_vertices[8][3];

extern const uint8_t edges[24];

/* ================= ANGLES ================= */

extern uint8_t angleX, angleY, angleZ;
extern uint8_t targetX, targetY, targetZ;

/* ================= ROTATION CACHE ================= */

extern uint8_t last_ax, last_ay, last_az;
extern uint8_t last_shape;
extern uint8_t last_zoom;

extern int16_t g_sinX, g_cosX;
extern int16_t g_sinY, g_cosY;
extern int16_t g_sinZ, g_cosZ;

extern int16_t rot_ref_v[8][3];
extern int16_t scaled_ref_v[8][3];
extern int16_t block_centers[MAX_BLOCKS][3];
extern int16_t scaled_block_centers[MAX_BLOCKS][3];

extern int16_t px[8], py[8];

static inline int16_t apply_perspective(int16_t v, uint8_t zi) {
    return (v * (int32_t)persp_lut[zi]) >> 10;
}

/* ================= PRECOMPUTE ================= */

void precompute_tables(void);
void precompute_grid_coordinates(void);

/* ================= ROTATION ================= */

void rotate_ref_vertex(const int16_t *v, int16_t *o);
void rotate_block_center(const int8_t *o, const int8_t *center, int16_t *v);

/* ================= INTERPOLATION ================= */

static inline uint8_t interpolate_angle(uint8_t cur, uint8_t tgt, uint8_t steps) {
    int16_t d = (int16_t)tgt - (int16_t)cur;
    if (d > 128) d -= 256;
    else if (d < -128) d += 256;
    return cur + (int8_t)(d / steps);
}


#endif