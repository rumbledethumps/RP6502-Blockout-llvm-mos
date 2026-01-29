#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "blockout_demo.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_state.h"
#include "bitmap_graphics_db.h"

static const uint16_t DEMO_START_DELAY_FRAMES = 90;

static bool demo_mode = false;
static uint16_t start_screen_idle_frames = 0;
static uint16_t demo_timer = 0;
static uint16_t demo_lines_base = 0;
static uint16_t demo_last_cubes_played = 0;
static uint8_t demo_clear_target = 0;

// Random movement plan for current shape
static int8_t demo_move_dir_x = 0;   // -1 = left, 1 = right, 0 = no move
static int8_t demo_move_dir_y = 0;   // -1 = front, 1 = back, 0 = no move
static uint8_t demo_steps_x = 0;     // Steps remaining in X
static uint8_t demo_steps_y = 0;     // Steps remaining in Y
static bool demo_movement_done = false;

extern void apply_selected_pit_size(void);
extern void reset_game_state(void);
extern void update_static_buffer(void);
extern void spawn_new_shape(void);

static bool demo_should_reset(void) {
    uint8_t occupied = count_occupied_levels();
    return (occupied >= (uint8_t)(PIT_HEIGHT - 2));
}




static void demo_plan_random_movement(void) {
    // Select random direction and steps for X axis
    demo_move_dir_x = (int8_t)(random(0, 2) ? 1 : -1);  // 1 = right, -1 = left
    demo_steps_x = (uint8_t)random(0, PIT_WIDTH / 2 + 1);  // 0 to 5 steps
    
    // Select random direction and steps for Y axis
    demo_move_dir_y = (int8_t)(random(0, 2) ? 1 : -1);  // 1 = back, -1 = front
    demo_steps_y = (uint8_t)random(0, PIT_DEPTH / 2 + 1);  // 0 to 5 steps
    
    demo_movement_done = false;
}

static void demo_execute_movement_step(void) {
    if (state.current == STATE_ANIMATING) return;
    
    bool moved = false;
    
    // Try to move in X direction if steps remain
    if (demo_steps_x > 0) {
        int8_t new_x = shape_pos_x + demo_move_dir_x;
        if (is_position_valid(new_x, shape_pos_y, shape_pos_z)) {
            shape_pos_x = new_x;
            moved = true;
        }
        demo_steps_x--;
    }
    
    // Try to move in Y direction if steps remain
    if (!moved && demo_steps_y > 0) {
        int8_t new_y = shape_pos_y + demo_move_dir_y;
        if (is_position_valid(shape_pos_x, new_y, shape_pos_z)) {
            shape_pos_y = new_y;
            moved = true;
        }
        demo_steps_y--;
    }
    
    // After each movement, randomly decide if we should rotate
    if (random(0, 2)) {  // 50% chance to rotate
        uint8_t axis = (uint8_t)random(0, 3);  // 0=X, 1=Y, 2=Z
        uint8_t step = ANGLE_STEP_90;
        int8_t kX, kY, kZ;
        
        if (axis == 0) {
            // Rotate on X axis
            uint8_t nextX = (uint8_t)(targetX + step);
            if (try_wall_kick(nextX, targetY, targetZ, &kX, &kY, &kZ)) {
                shape_pos_x = kX;
                shape_pos_y = kY;
                shape_pos_z = kZ;
                targetX = nextX;
                change_state(STATE_ANIMATING);
                return;
            }
        } else if (axis == 1) {
            // Rotate on Y axis
            uint8_t nextY = (uint8_t)(targetY + step);
            if (try_wall_kick(targetX, nextY, targetZ, &kX, &kY, &kZ)) {
                shape_pos_x = kX;
                shape_pos_y = kY;
                shape_pos_z = kZ;
                targetY = nextY;
                change_state(STATE_ANIMATING);
                return;
            }
        } else {
            // Rotate on Z axis
            uint8_t nextZ = (uint8_t)(targetZ + step);
            if (try_wall_kick(targetX, targetY, nextZ, &kX, &kY, &kZ)) {
                shape_pos_x = kX;
                shape_pos_y = kY;
                shape_pos_z = kZ;
                targetZ = nextZ;
                change_state(STATE_ANIMATING);
                return;
            }
        }
    }
    
    // Check if movement is complete
    if (demo_steps_x == 0 && demo_steps_y == 0) {
        demo_movement_done = true;
    }
}

static void demo_reset_cycle(void) {
    reset_game_state();
    update_static_buffer();

    demo_clear_target = 1 + (uint8_t)random(0, 2);
    demo_lines_base = lines_cleared;
    demo_timer = 0;

    spawn_new_shape();
    demo_last_cubes_played = cubes_played;
    demo_plan_random_movement();
}

static void demo_on_new_shape(void) {
    if (lines_cleared >= (uint16_t)(demo_lines_base + demo_clear_target)) {
        demo_reset_cycle();
        return;
    }

    demo_plan_random_movement();
}

bool demo_is_active(void) {
    return demo_mode;
}

void demo_tick(void) {
    if (!demo_mode) return;

    if (demo_should_reset()) {
        demo_reset_cycle();
        return;
    }

    if (cubes_played != demo_last_cubes_played) {
        demo_last_cubes_played = cubes_played;
        demo_on_new_shape();
    }

    if (state.current != STATE_PLAYING) return;

    demo_timer++;
    
    // Execute movement steps periodically
    if ((demo_timer % random(8, 50)) == 0) {
        if (!demo_movement_done) {
            demo_execute_movement_step();
        }
    }

    // Drop after movement is done and some delay
    if (demo_movement_done && demo_timer > 60) {
        change_state(STATE_FAST_DROP);
        demo_timer = 0;
    }
}

void demo_start(void) {
    if (demo_mode) return;
    apply_selected_pit_size();
    demo_mode = true;
    demo_reset_cycle();
    change_state(STATE_PLAYING);
    start_screen_idle_frames = 0;
}

void demo_stop(void) {
    if (!demo_mode) return;
    demo_mode = false;
    change_state(STATE_START_SCREEN);
    state.full_redraw_pending = true;
    state.need_static_redraw = true;
    start_screen_idle_frames = 0;
}

bool demo_idle_update(bool is_start_screen, bool key_pressed) {
    if (!is_start_screen || demo_mode) {
        start_screen_idle_frames = 0;
        return false;
    }

    if (key_pressed) {
        start_screen_idle_frames = 0;
        return false;
    }

    start_screen_idle_frames++;
    if (start_screen_idle_frames >= DEMO_START_DELAY_FRAMES) {
        demo_start();
        start_screen_idle_frames = 0;
        return true;
    }

    return false;
}

void demo_notify_start_screen_input(void) {
    start_screen_idle_frames = 0;
}
