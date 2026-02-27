// Blockout game clone for Picocomputer RP6502
// by Grzegorz Rakoczy

#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>      // For open() flags
#include <unistd.h>     // For open(), close(), read(), write()
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics_db.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_render.h"
#include "blockout_state.h"
#include "blockout_input.h"
#include "blockout_demo.h"
#include "sound.h"

/* ================= CONFIG ================= */

// Runtime pit size variables
uint8_t PIT_WIDTH = 5;
uint8_t PIT_DEPTH = 5;
uint8_t PIT_HEIGHT = 8;
uint8_t selected_pit_size = 2; // Default to 5x5 (1=4x4, 2=5x5)

uint8_t LEVEL_INDICATOR_HEIGHT = SCREEN_HEIGHT - LEVEL_INDICATOR_WIDTH * MAX_PIT_HEIGHT;


uint32_t score = 0;
uint16_t cubes_played = 0;
uint16_t lines_cleared = 0;
uint16_t drop_delay = 60;       // Frames between auto-drops
uint8_t current_level = 0;
uint8_t next_shape_idx = 0;
uint16_t seed = 0;


/* ================= GLOBAL STATE ================= */


uint8_t active_buffer = 0;

bool perspective_enabled = true;
uint8_t zoom_level = 0;
uint8_t mode = 0;
char text_buffer[24];
uint8_t current_shape_idx = 0;
bool hud_dirty = true;
static uint16_t viewport_buffers[2] = {
    VIEWPORT_BUFFER_0, VIEWPORT_BUFFER_1
};
static bool start_screen_drawn = false;
static uint8_t shake_timer = 0;
static uint8_t shake_index = 0;
static const int8_t shake_offsets_standard[][2] = {
    {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {0, 0}
};
static const int8_t shake_offsets_game_over[][2] = {
    {0, 0},  {3, 0},  {-3, 0}, {0, 3},  {0, -3},
    {3, 3},  {-3, -3}, {3, -3}, {-3, 3}, {0, 2},
    {0, -2}, {2, 0},  {-2, 0}, {3, 1}, {-3, -1},
    {0, 0}
};
static const int8_t (*active_shake_offsets)[2] = shake_offsets_standard;
static uint8_t active_shake_len = (uint8_t)(sizeof(shake_offsets_standard) / sizeof(shake_offsets_standard[0]));

void mark_hud_dirty(void) {
    hud_dirty = true;
    if (state.current != STATE_FAST_DROP) {
        state.need_static_redraw = true;
    }
}

void apply_selected_pit_size(void) {
    switch (selected_pit_size) {
        case 1: // 4x4
            PIT_WIDTH = 4;
            PIT_DEPTH = 4;
            PIT_HEIGHT = 8;
            break;
        case 2: // 5x5
        default:
            PIT_WIDTH = 5;
            PIT_DEPTH = 5;
            PIT_HEIGHT = 8;
            break;
    }

    LEVEL_INDICATOR_HEIGHT = SCREEN_HEIGHT - LEVEL_INDICATOR_WIDTH * PIT_HEIGHT;
    precompute_grid_coordinates();
    mark_hud_dirty();
    state.full_redraw_pending = true;
    state.need_static_redraw = true;
}

static const uint8_t shape_colors[7] = {
    RED, YELLOW, CYAN, GREEN, MAGENTA, BLUE, LIGHT_GRAY
};


void draw_palette(uint16_t buf) {
    static uint8_t size = 8;
    uint8_t y = 0;
    for (uint8_t i=0; i < 16; i++) {
        fill_rect2buffer(i, 214, y, size, size, buf);
        y += size;
    }
}

void draw_start_screen(uint16_t buf) {
    uint16_t front_buffer = viewport_buffers[active_buffer];
    uint16_t back_buffer = viewport_buffers[!active_buffer];

    int fd = open("ROM:start_screen", O_RDONLY);
    if (fd >= 0) {
        int bytes_read = read_xram(front_buffer, VIEWPORT_SIZE, fd);
        if (bytes_read >= 0) {
            lseek(fd, 0, SEEK_SET);
            read_xram(back_buffer, VIEWPORT_SIZE, fd);
        } else {
            printf("ERROR: read_xram failed %i\n\n", bytes_read);
        }
        close(fd);
    } else {
        printf("ERROR: reading ROM:start_screen %i\n\n", fd);
    }

    switch_buffer_plane(VIEWPORT_STRUCT_ADDR, front_buffer);

}

void draw_pause_screen(uint16_t buf) {

    fill_rect2buffer(DARK_GRAY, 24, 30, 135, 28, buf);
    set_text_multiplier(1);
    set_text_color(DARK_RED);

    sprintf(text_buffer, "Paused: [P] to resume");
    set_cursor(30, 40);
    draw_string2buffer(text_buffer, buf);
}

void reset_game_state(void) {
    score = 0;
    lines_cleared = 0;
    cubes_played = 0;
    current_level = 0;
    drop_delay = 60;
    current_shape_idx = 0;
    next_shape_idx = 0;
    angleX = angleY = angleZ = 0;
    targetX = targetY = targetZ = 0;

    for (uint8_t z = 0; z < MAX_PIT_HEIGHT; z++) {
        for (uint8_t y = 0; y < MAX_PIT_DEPTH; y++) {
            for (uint8_t x = 0; x < MAX_PIT_WIDTH; x++) {
                pit[z][y][x] = 0;
                pit_colors[z][y][x] = 0;
            }
        }
    }

    mark_hud_dirty();
    state.full_redraw_pending = true;
    state.need_static_redraw = true;
}

/* ================= HUD ================= */

void draw_static_hud(uint16_t buf) {

    // draw_palette(buf);

    static uint32_t last_score = 0xFFFFFFFFu;
    static uint16_t last_cubes = 0xFFFFu;
    static uint8_t last_pit_w = 0xFFu;
    static uint8_t last_pit_d = 0xFFu;
    static uint8_t last_pit_h = 0xFFu;
    static uint8_t last_level = 0xFFu;
    static bool last_game_over = false;

    set_text_multiplier(1);

    if (score != last_score) {
        set_text_color(YELLOW);
        uint8_t old_len = sprintf(text_buffer, "%lu", last_score) * 5;
        uint8_t len = sprintf(text_buffer, "%lu", score) * 5;
        uint8_t clear_len = (old_len > len) ? old_len : len;
        set_cursor(290-len, 94);
        fill_rect2buffer(BLACK, 290-clear_len, 94, clear_len+5, 7, buf);
        draw_string2buffer(text_buffer, buf);
        last_score = score;
    }

    if (cubes_played != last_cubes) {
        set_text_color(YELLOW);
        uint8_t old_len = sprintf(text_buffer, "%u", last_cubes) * 5;
        uint8_t len = sprintf(text_buffer, "%u", cubes_played) * 5;
        uint8_t clear_len = (old_len > len) ? old_len : len;
        fill_rect2buffer(BLACK, 290-clear_len, 125, clear_len+5, 7, buf);
        set_cursor(290-len, 125);
        draw_string2buffer(text_buffer, buf);
        last_cubes = cubes_played;
    }

    if (PIT_WIDTH != last_pit_w || PIT_DEPTH != last_pit_d || PIT_HEIGHT != last_pit_h) {
        set_text_color(YELLOW);
        uint8_t len = sprintf(text_buffer, "%ux%ux%u", PIT_WIDTH, PIT_DEPTH, PIT_HEIGHT) * 5;
        fill_rect2buffer(BLACK, 281-len, 155, len+5, 7, buf);
        set_cursor(281-len, 155);
        draw_string2buffer(text_buffer, buf);
        last_pit_w = PIT_WIDTH;
        last_pit_d = PIT_DEPTH;
        last_pit_h = PIT_HEIGHT;
    }

    if (current_level != last_level) {
        set_text_color(GREEN);
        fill_rect2buffer(BLACK, 8, 14, 5, 7, buf);
        set_cursor(8, 14);
        sprintf(text_buffer, "%u", current_level);
        draw_string2buffer(text_buffer, buf);
        last_level = current_level;
    }

    if (demo_is_active()) {
        set_text_color(LIGHT_GRAY);
        // fill_rect2buffer(BLACK, 200, 14, 90, 7, buf);
        set_cursor(100, 4);
        draw_string2buffer("DEMO MODE", buf);
        set_cursor(60, 14);
        draw_string2buffer("Press any key to start", buf);
    }

    if (state.current == STATE_GAME_OVER) {
        if (!last_game_over) {
            set_text_color(RED);
            fill_rect2buffer(DARK_BLUE, 88, 145, 80, 30, buf);
            set_cursor(98, 150);
            draw_string2buffer("GAME OVER!", buf);
            set_cursor(96, 160);
            draw_string2buffer("[R] RESTART", buf);
        }
        last_game_over = true;
    } else if (last_game_over) {
        // Full redraw will clear the banner and restore pit lines
        last_game_over = false;
    }
}


void update_static_buffer(void) {
    if (state.full_redraw_pending) {
        fill_rect2buffer(BLACK, VIEWPORT_X, 0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, STATIC_BUFFER_ADDR);
        fill_rect2buffer(0, 3, 27, 18, 150, STATIC_BUFFER_ADDR);
        draw_pit_background(STATIC_BUFFER_ADDR);
        draw_settled_blocks(STATIC_BUFFER_ADDR);
        state.full_redraw_pending = false;
        mark_hud_dirty();
    }
    if (hud_dirty) {
        draw_static_hud(STATIC_BUFFER_ADDR);
        hud_dirty = false;
    }
    draw_level_color_indicator(STATIC_BUFFER_ADDR);
    switch_buffer_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR);
    state.need_static_redraw = false;
}

void trigger_screen_shake(void) {
    active_shake_offsets = shake_offsets_standard;
    active_shake_len = (uint8_t)(sizeof(shake_offsets_standard) / sizeof(shake_offsets_standard[0]));
    shake_timer = active_shake_len;
    shake_index = 0;
}

void trigger_game_over_shake(void) {
    active_shake_offsets = shake_offsets_game_over;
    active_shake_len = (uint8_t)(sizeof(shake_offsets_game_over) / sizeof(shake_offsets_game_over[0]));
    // Run multiple passes for a longer, heavier effect on game over.
    shake_timer = (uint8_t)(active_shake_len * 3);
    shake_index = 0;
}

static void update_screen_shake(void) {
    if (shake_timer == 0) {
        set_plane_position(STATIC_STRUCT_ADDR, 0, 0);
        set_plane_position(VIEWPORT_STRUCT_ADDR, VIEWPORT_X, VIEWPORT_Y);
        return;
    }

    int8_t dx = active_shake_offsets[shake_index][0];
    int8_t dy = active_shake_offsets[shake_index][1];

    set_plane_position(STATIC_STRUCT_ADDR, (uint16_t)(0 + dx), (uint16_t)(0 + dy));
    set_plane_position(VIEWPORT_STRUCT_ADDR, (uint16_t)(VIEWPORT_X + dx), (uint16_t)(VIEWPORT_Y + dy));

    shake_index++;
    if (shake_index >= active_shake_len) {
        shake_index = 0;
    }

    shake_timer--;
}

void handle_start_screen_state(void) {
    if (!start_screen_drawn) {
        if (state.full_redraw_pending || state.need_static_redraw) {
            update_static_buffer();
        }
        draw_start_screen(STATIC_BUFFER_ADDR);
        start_screen_drawn = true;
    }
}

void handle_start_screen_input(void) {
    if (key(KEY_1)) {
        demo_notify_start_screen_input();
        selected_pit_size = 0; // 4x4
        apply_selected_pit_size();
        update_static_buffer();
        draw_start_screen(STATIC_BUFFER_ADDR);
        start_screen_drawn = true;
    }
    if (key(KEY_2)) {
        demo_notify_start_screen_input();
        selected_pit_size = 1; // 5x5
        apply_selected_pit_size();
        update_static_buffer();
        draw_start_screen(STATIC_BUFFER_ADDR);
        start_screen_drawn = true;
    }
    if (key(KEY_SPACE)) {
        demo_notify_start_screen_input();
        apply_selected_pit_size();
        reset_game_state();
        update_static_buffer();
        spawn_new_shape();
        if (state.current != STATE_GAME_OVER) {
            change_state(STATE_PLAYING);
        }
        start_screen_drawn = false;
    }
}

void read_keyboard(void) {
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
        RIA.addr0 = KEYBOARD_INPUT + i;
        keystates[i] = RIA.rw0;
    }
}

static bool any_key_pressed(void) {
    return ((keystates[0] & 1) == 0);
}

/* ================= MAIN ================= */

int main(void) {
    precompute_tables();
    precompute_grid_coordinates();

    init_graphics_plane(STATIC_STRUCT_ADDR, STATIC_BUFFER_ADDR,
        0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    init_graphics_plane(VIEWPORT_STRUCT_ADDR, viewport_buffers[0],
        1, VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    
    switch_buffer_plane(VIEWPORT_STRUCT_ADDR, VIEWPORT_BUFFER_0);

    erase_buffer_sized(VIEWPORT_BUFFER_0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
    erase_buffer_sized(VIEWPORT_BUFFER_1, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);

    update_static_buffer();
    init_sound();

    uint8_t v = RIA.vsync;
    bool handled_key = false;
    int8_t last_shape_x = shape_pos_x;
    int8_t last_shape_y = shape_pos_y;
    int8_t last_shape_z = shape_pos_z;
    uint8_t last_angle_x = angleX;
    uint8_t last_angle_y = angleY;
    uint8_t last_angle_z = angleZ;
    uint8_t last_shape_idx = current_shape_idx;
    GameState last_state = state.current;
    GameState last_state_transition = state.current;
    
    while (true) {
        // Wait for vsync
        if (v == RIA.vsync) continue;
        v = RIA.vsync;
        seed++;

        // Update static buffer if needed
        if (state.need_static_redraw) {
            update_static_buffer();
            state.need_static_redraw = false;
        }

        update_screen_shake();

        demo_tick();

        // State machine update
        switch(state.current) {
            case STATE_START_SCREEN:
                handle_start_screen_state();
                break;

            case STATE_PLAYING:
                handle_playing_state();
                break;
                
            case STATE_ANIMATING:
                handle_animating_state();
                break;
                
            case STATE_FAST_DROP:
                handle_fast_drop_state();
                break;
                
            case STATE_LOCKING:
                handle_locking_state();
                break;
                
            case STATE_PAUSED:
                // Do nothing during pause
                break;
                
            case STATE_GAME_OVER:
                // Static state - wait for restart
                break;
        }

        if (state.current != last_state_transition) {
            if (state.current == STATE_START_SCREEN) {
                start_screen_drawn = false;
            }
            last_state_transition = state.current;
        }

        // Render only when needed to save cycles
        bool shape_changed = (shape_pos_x != last_shape_x) || (shape_pos_y != last_shape_y) ||
                             (shape_pos_z != last_shape_z) || (angleX != last_angle_x) ||
                             (angleY != last_angle_y) || (angleZ != last_angle_z) ||
                             (current_shape_idx != last_shape_idx);
        bool state_changed = (state.current != last_state);
        bool needs_render = (state.current != STATE_PAUSED) &&
                    (state.current != STATE_START_SCREEN) &&
                            (shape_changed || state_changed || state.current == STATE_ANIMATING);

        if (needs_render) {
            uint16_t back_buffer = viewport_buffers[!active_buffer];
            erase_buffer_sized(back_buffer, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 4);
            drawShape(back_buffer);
            switch_buffer_plane(VIEWPORT_STRUCT_ADDR, back_buffer);
            active_buffer = !active_buffer;

            last_shape_x = shape_pos_x;
            last_shape_y = shape_pos_y;
            last_shape_z = shape_pos_z;
            last_angle_x = angleX;
            last_angle_y = angleY;
            last_angle_z = angleZ;
            last_shape_idx = current_shape_idx;
            last_state = state.current;
        }

        read_keyboard();

        bool demo_was_stopped = false;
        if (demo_is_active() && any_key_pressed()) {
            demo_stop();
            start_screen_drawn = false;
            demo_was_stopped = true;
        }

        if (demo_idle_update((state.current == STATE_START_SCREEN || state.current == STATE_GAME_OVER), any_key_pressed())) {
            start_screen_drawn = false;
        }

        if (!(keystates[0] & 1) && !demo_was_stopped) {
            if (!handled_key) {
                // Global keys
                if (key(KEY_P)) {
                    draw_pause_screen(viewport_buffers[active_buffer]);
                    toggle_pause();
                }
                if (key(KEY_ESC)) break;
                
                // State-specific input
                switch(state.current) {
                    case STATE_START_SCREEN:
                        handle_start_screen_input();
                        break;

                    case STATE_PLAYING:
                        handle_playing_input();
                        break;
                        
                    case STATE_LOCKING:
                        handle_locking_input();
                        break;
                        
                    case STATE_GAME_OVER:
                        handle_game_over_input();
                        break;
                        
                    default:
                        break;
                }
                
                // Debug keys (work in any state except game over)
                if (state.current != STATE_GAME_OVER) {
                    if (key(KEY_Z)) {
                        current_shape_idx = (current_shape_idx + 1) % NUM_SHAPES;
                    }
                    if (key(KEY_M)) {
                        mode = (mode + 1) % NUM_MODES;
                    }
                }
                
                handled_key = true;
            }
        } else {
            handled_key = false;
        }

        update_sound();
    }
    
    return 0;
}
