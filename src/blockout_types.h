#ifndef BLOCKOUT_TYPES_H
#define BLOCKOUT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 180
#define VIEWPORT_WIDTH 180
#define VIEWPORT_HEIGHT 180
#define VIEWPORT_SIZE (VIEWPORT_WIDTH * VIEWPORT_HEIGHT / 2) 

#define VIEWPORT_X 32 
#define VIEWPORT_Y ((SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2)

#define SCREEN_CENTER_X (VIEWPORT_WIDTH >> 1)
#define SCREEN_CENTER_Y (VIEWPORT_HEIGHT >> 1)

#define STATIC_BUFFER_ADDR   0x0000
#define VIEWPORT_BUFFER_0    0x7080
#define VIEWPORT_BUFFER_1    0xAFC0
#define STATIC_STRUCT_ADDR   0xFE00
#define VIEWPORT_STRUCT_ADDR 0xFE80
#define PSG_BASE             0xFEC0

#define NUM_POINTS 256

#define GRID_SIZE        (VIEWPORT_WIDTH / PIT_WIDTH)
#define CUBE_SIZE        (GRID_SIZE / 2)

#define WORLD_HALF_W     (VIEWPORT_WIDTH / 2)
#define WORLD_HALF_H     (VIEWPORT_HEIGHT / 2)

#define PIT_Z_START      64
#define PIT_Z_STEP       12

#define MAX_PIT_WIDTH 5
#define MAX_PIT_DEPTH 5
#define MAX_PIT_HEIGHT 8

// Runtime pit size (set at game start)
extern uint8_t PIT_WIDTH;
extern uint8_t PIT_DEPTH;
extern uint8_t PIT_HEIGHT;
extern uint8_t selected_pit_size; // 0=3x3, 1=4x4, 2=5x5

#define MAX_BLOCKS 4
#define NUM_SHAPES 8
#define NUM_ZOOM_LEVELS 8
#define NUM_MODES 4

#define ROTATION_STEPS 3
#define ANGLE_STEP_90 (256/4)

#define FILL_STRIDE 1

#define LEVEL_INDICATOR_WIDTH 14

/* ================= SHAPES ================= */

#define MASK_FACE_RIGHT  ((1<<1)|(1<<5)|(1<<9)|(1<<10))
#define MASK_FACE_LEFT   ((1<<3)|(1<<7)|(1<<8)|(1<<11))
#define MASK_FACE_TOP    ((1<<2)|(1<<6)|(1<<10)|(1<<11))
#define MASK_FACE_BOTTOM ((1<<0)|(1<<4)|(1<<8)|(1<<9))
#define MASK_FACE_FRONT  ((1<<4)|(1<<5)|(1<<6)|(1<<7))
#define MASK_FACE_BACK   ((1<<0)|(1<<1)|(1<<2)|(1<<3))

typedef enum {
    STATE_PLAYING,      // Normal gameplay
    STATE_ANIMATING,    // Shape is rotating (blocks most input)
    STATE_FAST_DROP,    // Space bar held - dropping fast
    STATE_LOCKING,      // Shape just hit bottom, about to lock
    STATE_PAUSED,       // Game paused
    STATE_GAME_OVER,     // Game over
    STATE_START_SCREEN   // Start screen displayed
} GameState;

typedef struct {
    GameState current;
    GameState previous;
    uint8_t anim_counter;      // For rotation animation
    uint16_t drop_timer;       // For auto-drop timing
    uint8_t lock_delay;        // Delay before locking (allows last-moment moves)
    bool need_static_redraw;
    bool full_redraw_pending;
} StateMachine;

extern StateMachine state;

typedef struct {
    uint8_t num_blocks;
    const char *name;
    const int8_t offsets[MAX_BLOCKS][3];
    const uint16_t edge_masks[MAX_BLOCKS];
    const int8_t center[3]; // Values are in half-blocks (1 = 0.5 blocks)
} Shape;

// Global variable declarations

extern uint8_t LEVEL_INDICATOR_HEIGHT; 

extern uint32_t score;
extern uint16_t cubes_played;
extern uint16_t lines_cleared;
extern uint16_t drop_delay;       // Frames between auto-drops
extern uint8_t current_level;
extern uint8_t next_shape_idx;
extern uint16_t seed;


/* ================= GLOBAL STATE ================= */


extern uint8_t active_buffer;

extern bool perspective_enabled;
extern uint8_t zoom_level;
extern uint8_t mode;
extern char text_buffer[24];
extern uint8_t current_shape_idx;
extern bool hud_dirty;

void mark_hud_dirty(void);

#endif