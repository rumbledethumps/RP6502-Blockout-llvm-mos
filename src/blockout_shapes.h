#ifndef BLOCKOUT_SHAPES_H
#define BLOCKOUT_SHAPES_H

#include <stdint.h>
#include <stdbool.h>
#include "blockout_types.h"
#include "blockout_math.h"

extern const Shape shapes[NUM_SHAPES];

extern int8_t shape_pos_x;
extern int8_t shape_pos_y;
extern int8_t shape_pos_z;

void get_rotated_offset(uint8_t block_idx, uint8_t use_angleX, uint8_t use_angleY, uint8_t use_angleZ, int8_t *rx, int8_t *ry, int8_t *rz);

bool is_position_valid(int8_t px, int8_t py, int8_t pz);

bool is_rotation_valid_at(uint8_t nX, uint8_t nY, uint8_t nZ, int8_t px, int8_t py, int8_t pz);

void apply_rotation(uint8_t new_angleX, uint8_t new_angleY, uint8_t new_angleZ);

bool try_wall_kick(uint8_t nX, uint8_t nY, uint8_t nZ, int8_t *out_x, int8_t *out_y, int8_t *out_z);

void spawn_new_shape(void);


#endif