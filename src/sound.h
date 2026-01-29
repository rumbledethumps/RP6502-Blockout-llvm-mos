// sound.h
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>
#include <stdbool.h>

// --- Helper Constants for ezpsg ---
// These make the code in sound.c more readable.


// Panning
#define EZPSG_PAN_LEFT    -63
#define EZPSG_PAN_RIGHT   63
#define EZPSG_PAN_CENTER  0 // Both left and right

#define MAX_INTERPOLATED_SOUNDS 4

typedef struct InterpolatedSoundHandle* InterpSoundHandle;

InterpSoundHandle start_interpolated_sound(uint8_t start_note, uint8_t end_note,
                                           uint8_t start_duty, uint8_t end_duty,
                                           uint8_t start_vol_attack, uint8_t end_vol_attack,
                                           uint8_t start_vol_decay, uint8_t end_vol_decay,
                                           uint8_t start_wave, uint8_t end_wave,
                                           int8_t start_pan, int8_t end_pan,
                                           uint8_t note_duration, uint8_t release,
                                           uint8_t steps, bool loop);
void stop_interpolated_sound(InterpSoundHandle handle);
void update_interpolated_sounds(void);  

void init_sound(void);
void update_sound(void);



void play_drop_sound(void); 
void play_clear_level_sound(void);
void play_clear_level_all_sound(void);

InterpSoundHandle start_game_over_sound(void);



#endif // SOUND_H
