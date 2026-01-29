#ifndef BLOCKOUT_RENDER_H
#define BLOCKOUT_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"




void draw_pit_background(uint16_t buf);

void draw_level_color_indicator(uint16_t buf);

void drawShape(uint16_t buffer);

void draw_shape_position();

void draw_poly_fast(uint16_t buf, int16_t x0, int16_t y0, int16_t x1, int16_t y1, 
                    int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint8_t color, uint8_t stride);

void draw_cube_at(uint16_t buf, uint8_t x, uint8_t y, uint8_t z, uint8_t color);



void draw_settled_range(uint16_t buffer, uint8_t start_z);

void draw_settled_blocks(uint16_t buf);

void draw_incremental_lock(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y, int8_t start_z);




#endif