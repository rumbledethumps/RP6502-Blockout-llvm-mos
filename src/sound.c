// sound.c
#include "sound.h"
#include "ezpsg.h"
#include <rp6502.h>
#include <stdbool.h>
#include <stdio.h>
#include "blockout_types.h"

// Keep track of the continuous thrust sound
static bool is_thrust_playing = false;
static uint16_t thrust_channel_xaddr = 0xFFFF;

// Sound effect state
typedef struct {
    uint16_t psg_addr;      // Address in PSG memory (0xFFFF = not playing)
    uint8_t frame_counter;  // For multi-frame effects
    bool active;
} SoundEffect;

// Sound channels
static SoundEffect thrust_sound = {0xFFFF, 0, false};
static SoundEffect ufo_sound = {0xFFFF, 0, false};
static uint8_t beat_timer = 0;
static uint8_t beat_interval = 60;  // Frames between beats (starts slow)
static bool beat_low = true;         // Alternates between two tones

// Initialize the sound system
void init_sound(void) {

    ezpsg_init(PSG_BASE);
}

// Interpolated sound state
typedef struct InterpolatedSoundHandle {
    uint8_t start_note, end_note;
    uint8_t start_duty, end_duty;
    uint8_t start_vol_attack, end_vol_attack;
    uint8_t start_vol_decay, end_vol_decay;
    uint8_t start_wave, end_wave;
    int8_t start_pan, end_pan;
    uint8_t note_duration;
    uint8_t release;
    uint8_t total_steps;
    uint8_t current_step;
    uint8_t frame_counter;
    bool loop;
    bool active;
    uint16_t psg_addr;
} InterpolatedSound;

static InterpolatedSound interp_sounds[MAX_INTERPOLATED_SOUNDS] = {0};

// Helper function to interpolate between two uint8_t values
static uint8_t interpolate_u8(uint8_t start, uint8_t end, uint8_t step, uint8_t total_steps) {
    if (total_steps <= 1) return start;
    int32_t diff = (int32_t)end - (int32_t)start;
    int32_t result = start + (diff * step) / (total_steps - 1);
    return (uint8_t)result;
}

// Helper function to interpolate between two int8_t values (for pan)
static int8_t interpolate_i8(int8_t start, int8_t end, uint8_t step, uint8_t total_steps) {
    if (total_steps <= 1) return start;
    int32_t diff = (int32_t)end - (int32_t)start;
    int32_t result = start + (diff * step) / (total_steps - 1);
    return (int8_t)result;
}

InterpSoundHandle start_interpolated_sound(uint8_t start_note, uint8_t end_note,
                                           uint8_t start_duty, uint8_t end_duty,
                                           uint8_t start_vol_attack, uint8_t end_vol_attack,
                                           uint8_t start_vol_decay, uint8_t end_vol_decay,
                                           uint8_t start_wave, uint8_t end_wave,
                                           int8_t start_pan, int8_t end_pan,
                                           uint8_t note_duration, uint8_t release,
                                           uint8_t steps, bool loop) {
    
    if (steps == 0) return NULL;
    
    // Find a free slot
    InterpolatedSound *sound = NULL;
    for (uint8_t i = 0; i < MAX_INTERPOLATED_SOUNDS; i++) {
        if (!interp_sounds[i].active) {
            sound = &interp_sounds[i];
            break;
        }
    }
    
    // No free slots available
    if (!sound) return NULL;
    
    // Store parameters
    sound->start_note = start_note;
    sound->end_note = end_note;
    sound->start_duty = start_duty;
    sound->end_duty = end_duty;
    sound->start_vol_attack = start_vol_attack;
    sound->end_vol_attack = end_vol_attack;
    sound->start_vol_decay = start_vol_decay;
    sound->end_vol_decay = end_vol_decay;
    sound->start_wave = start_wave;
    sound->end_wave = end_wave;
    sound->start_pan = start_pan;
    sound->end_pan = end_pan;
    sound->note_duration = note_duration;
    sound->release = release;
    sound->total_steps = steps;
    sound->loop = loop;
    
    // Initialize state
    sound->current_step = 0;
    sound->frame_counter = 0;
    sound->active = true;
    sound->psg_addr = 0xFFFF;
    
    return sound;
}

void stop_interpolated_sound(InterpSoundHandle handle) {
    if (handle) {
        handle->active = false;
        handle->psg_addr = 0xFFFF;
    }
}

void update_interpolated_sounds(void) {
    for (uint8_t i = 0; i < MAX_INTERPOLATED_SOUNDS; i++) {
        InterpolatedSound *sound = &interp_sounds[i];
        
        if (!sound->active) continue;
        
        sound->frame_counter++;
        
        // Time to play the next note in the sequence?
        if (sound->frame_counter >= sound->note_duration) {
            sound->frame_counter = 0;
            
            // Calculate interpolated parameters for current step
            uint8_t note = interpolate_u8(sound->start_note, 
                                           sound->end_note, 
                                           sound->current_step, 
                                           sound->total_steps);
            uint8_t duty = interpolate_u8(sound->start_duty, 
                                           sound->end_duty, 
                                           sound->current_step, 
                                           sound->total_steps);
            uint8_t vol_attack = interpolate_u8(sound->start_vol_attack, 
                                                 sound->end_vol_attack, 
                                                 sound->current_step, 
                                                 sound->total_steps);
            uint8_t vol_decay = interpolate_u8(sound->start_vol_decay, 
                                                sound->end_vol_decay, 
                                                sound->current_step, 
                                                sound->total_steps);
            uint8_t wave = interpolate_u8(sound->start_wave, 
                                           sound->end_wave, 
                                           sound->current_step, 
                                           sound->total_steps);
            int8_t pan = interpolate_i8(sound->start_pan, 
                                         sound->end_pan, 
                                         sound->current_step, 
                                         sound->total_steps);
            
            // Determine if this is the last note
            bool is_last = (sound->current_step == sound->total_steps - 1);
            uint8_t note_release = (is_last && !sound->loop) ? sound->release : 0;
            
            // Play the note
            sound->psg_addr = ezpsg_play_note(note, sound->note_duration, 
                                              note_release, duty, vol_attack, 
                                              vol_decay, wave, pan);
            
            // Advance to next step
            sound->current_step++;
            
            // Handle looping or stopping
            if (sound->current_step >= sound->total_steps) {
                if (sound->loop) {
                    sound->current_step = 0;  // Loop back to start
                } else {
                    sound->active = false;  // Stop after sequence completes
                }
            }
        }
    }
}

// Example usage - Looping alarm sound (plays continuously in background)
InterpSoundHandle start_game_over_sound(void) {
    return start_interpolated_sound(
        c5, c2,           // Start note, End note (sweep up 2 octaves)
        0x80, 0xFF,       // Start duty, End duty
        0x40, 0xC0,       // Start vol_attack, End vol_attack (getting louder)
        0x47, 0xCA,       // Start vol_decay, End vol_decay
        EZPSG_WAVE_SQUARE, 0x4A,  // Wave (constant)
        0, 0,             // Pan (center)
        2,                // Duration per note (frames)
        10,               // Release on final note
        30,               // Number of steps
        false              // Loop continuously
    );
}


// --- Sound Effect Definitions ---

void play_drop_sound(void) {
    ezpsg_play_note(d1,   // note
                        5,    // duration
                        0,    // release
                        155,   // duty
                        0x56, // vol_attack
                        0xF7, // vol_decay
                        0x49,
                        0);   // pan
    
}

void play_clear_level_sound(void) {
    ezpsg_play_note(gs3,   // note
                        10,    // duration
                        10,    // release
                        191,   // duty
                        0x08, // vol_attack
                        0xF8, // vol_decay
                        0x0B,
                        0);   // pan
    
}

void play_clear_level_all_sound(void) {
    ezpsg_play_note(cs5,   // note
                        10,    // duration
                        10,    // release
                        191,   // duty
                        0x08, // vol_attack
                        0xF8, // vol_decay
                        0x0A,
                        0);   // pan
    
}

// This function must be called every frame in your main game loop
void update_sound(void) {

    ezpsg_tick(1);

    if (is_thrust_playing && thrust_channel_xaddr != 0xFFFF) {
        uint8_t pan_gate;
        RIA.addr0 = thrust_channel_xaddr + 6; // Offset 6 is pan_gate in the struct
        RIA.step0 = 0;
        pan_gate = RIA.rw0;
        if ((pan_gate & 0x01) == 0) { // If gate bit is 0, sound is off
            is_thrust_playing = false;
        }
    }
    update_interpolated_sounds();
}