#include <stdint.h>
#include <stdbool.h>
#include "usb_hid_keys.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_state.h"
#include "blockout_input.h"
#include "sound.h"


StateMachine state = {
    .current = STATE_START_SCREEN,
    .previous = STATE_START_SCREEN,
    .anim_counter = 0,
    .drop_timer = 0,
    .lock_delay = 0,
    .need_static_redraw = true,
    .full_redraw_pending = true
};

void change_state(GameState new_state) {
    state.previous = state.current;
    state.current = new_state;
    if (new_state == STATE_GAME_OVER || state.previous == STATE_GAME_OVER) {
        mark_hud_dirty();
    }
    if (state.previous == STATE_GAME_OVER && new_state != STATE_GAME_OVER) {
        state.full_redraw_pending = true;
        state.need_static_redraw = true;
    }
    
    // Entry actions for states
    switch(new_state) {
        case STATE_ANIMATING:
            state.anim_counter = ROTATION_STEPS;
            break;
            
        case STATE_LOCKING:
            state.lock_delay = 5; 
            break;
            
        case STATE_FAST_DROP:
            state.drop_timer = 0;
            play_drop_sound(); 
            break;
            
        case STATE_PLAYING:
            state.drop_timer = 0;
            break;
            
        case STATE_GAME_OVER:
            state.need_static_redraw = true;
            trigger_game_over_shake();
            start_game_over_sound();
            break;

        case STATE_START_SCREEN:
            score = 0;
            cubes_played = 0;
            mark_hud_dirty();
            break;
            
        default:
            break;
    }
}





void toggle_pause(void) {
    if (state.current == STATE_PAUSED) {
        mark_hud_dirty();
        change_state(state.previous);
    } else if (state.current != STATE_GAME_OVER) {
        change_state(STATE_PAUSED);
    }
}

/* ================= STATE HANDLERS ================= */

void handle_playing_state(void) {
    // Auto-drop gravity
    state.drop_timer++;
    if (state.drop_timer >= drop_delay) {
        state.drop_timer = 0;
        if (is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z + 1)) {
            shape_pos_z++;
        } else {
            change_state(STATE_LOCKING);
        }
    }
}

void handle_animating_state(void) {
    angleX = interpolate_angle(angleX, targetX, state.anim_counter);
    angleY = interpolate_angle(angleY, targetY, state.anim_counter);
    angleZ = interpolate_angle(angleZ, targetZ, state.anim_counter);
    
    state.anim_counter--;
    if (state.anim_counter == 0) {
        apply_rotation(targetX, targetY, targetZ);
        change_state(STATE_PLAYING);
    }
}

void handle_fast_drop_state(void) {
    // Drop as fast as possible
    if (is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z + 1)) {
        shape_pos_z++;
        score += 2;
        mark_hud_dirty();
    } else {
        change_state(STATE_LOCKING);
    }
}

void handle_locking_state(void) {
    // Grace period - allow last moment moves
    state.lock_delay--;
    
    // Check if shape can still fall
    if (is_position_valid(shape_pos_x, shape_pos_y, shape_pos_z + 1)) {
        change_state(STATE_PLAYING);
        return;
    }
    
    // Lock when timer expires
    if (state.lock_delay == 0) {
        lock_shape();
        if (state.current != STATE_GAME_OVER) {
            change_state(STATE_PLAYING);
        }
    }
}

/* ================= INPUT HANDLING BY STATE ================= */

void handle_movement_input(void) {
    if (key(KEY_LEFT)) {
        if (is_position_valid(shape_pos_x - 1, shape_pos_y, shape_pos_z)) {
            shape_pos_x--;
            if (state.current == STATE_LOCKING) {
                state.lock_delay = 15;
            }
        }
    }
    if (key(KEY_RIGHT)) {
        if (is_position_valid(shape_pos_x + 1, shape_pos_y, shape_pos_z)) {
            shape_pos_x++;
            if (state.current == STATE_LOCKING) {
                state.lock_delay = 15;
            }
        }
    }
    if (key(KEY_UP)) {
        if (is_position_valid(shape_pos_x, shape_pos_y - 1, shape_pos_z)) {
            shape_pos_y--;
            if (state.current == STATE_LOCKING) {
                state.lock_delay = 15;
            }
        }
    }
    if (key(KEY_DOWN)) {
        if (is_position_valid(shape_pos_x, shape_pos_y + 1, shape_pos_z)) {
            shape_pos_y++;
            if (state.current == STATE_LOCKING) {
                state.lock_delay = 15;
            }
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
}

void handle_rotation_input(void) {
    int8_t kX, kY, kZ;
    uint8_t nextX = targetX, nextY = targetY, nextZ = targetZ;
    bool rotation_requested = false;

    if (key(KEY_Q)) { nextX += ANGLE_STEP_90; rotation_requested = true; }
    if (key(KEY_W)) { nextY += ANGLE_STEP_90; rotation_requested = true; }
    if (key(KEY_E)) { nextZ += ANGLE_STEP_90; rotation_requested = true; }
    if (key(KEY_A)) { nextX -= ANGLE_STEP_90; rotation_requested = true; }
    if (key(KEY_S)) { nextY -= ANGLE_STEP_90; rotation_requested = true; }
    if (key(KEY_D)) { nextZ -= ANGLE_STEP_90; rotation_requested = true; }

    if (rotation_requested) {
        if (try_wall_kick(nextX, nextY, nextZ, &kX, &kY, &kZ)) {
            shape_pos_x = kX;
            shape_pos_y = kY;
            shape_pos_z = kZ;
            targetX = nextX;
            targetY = nextY;
            targetZ = nextZ;
            change_state(STATE_ANIMATING);
        }
    }
}

void handle_playing_input(void) {
    if (key(KEY_SPACE)) {
        change_state(STATE_FAST_DROP);
    }
    handle_movement_input();
    handle_rotation_input();
}

void handle_locking_input(void) {
    // Allow movement during lock delay
    handle_movement_input();
    // Don't allow rotation during lock delay
}

void handle_game_over_input(void) {
    if (key(KEY_R)) {
        // clear pit
        for (uint8_t z = 0; z < MAX_PIT_HEIGHT; z++) {
            for (uint8_t y = 0; y < MAX_PIT_DEPTH; y++) {
                for (uint8_t x = 0; x < MAX_PIT_WIDTH; x++) {
                    pit[z][y][x] = 0;
                    pit_colors[z][y][x] = 0;
                }
            }
        }
        change_state(STATE_START_SCREEN);
    }
}


