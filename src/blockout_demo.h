#ifndef BLOCKOUT_DEMO_H
#define BLOCKOUT_DEMO_H

#include <stdbool.h>

bool demo_is_active(void);
void demo_tick(void);
void demo_start(void);
void demo_stop(void);
bool demo_idle_update(bool is_start_screen, bool key_pressed);
void demo_notify_start_screen_input(void);

#endif
