#ifndef BLOCKOUT_PIT_H
#define BLOCKOUT_PIT_H

#include <stdint.h>
#include <stdbool.h>
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"


extern uint8_t pit[MAX_PIT_HEIGHT][MAX_PIT_DEPTH][MAX_PIT_WIDTH];           // 1 if block present
extern uint8_t pit_colors[MAX_PIT_HEIGHT][MAX_PIT_DEPTH][MAX_PIT_WIDTH];    // Color of each block
extern const uint8_t layer_colors[MAX_PIT_HEIGHT];

bool is_layer_complete(uint8_t z);

void clear_layer(uint8_t z);

void check_and_clear_layers(void);

void redraw_region(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y);

void lock_shape(void);

uint8_t count_occupied_levels(void);

void trigger_screen_shake(void);
void trigger_game_over_shake(void);
#endif