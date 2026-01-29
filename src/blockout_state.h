#ifndef BLOCKOUT_STATE_H
#define BLOCKOUT_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"



void change_state(GameState new_state);
void toggle_pause(void);

/* ================= STATE HANDLERS ================= */

void handle_playing_state(void);

void handle_animating_state(void);

void handle_locking_state(void);

void handle_fast_drop_state(void);

void handle_start_screen_state(void);

/* ================= INPUT HANDLING BY STATE ================= */

void handle_movement_input(void);

void handle_playing_input(void);

void handle_locking_input(void);

void handle_game_over_input(void);

void handle_start_screen_input(void);


#endif