#ifndef BLOCKOUT_INPUT_H
#define BLOCKOUT_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "usb_hid_keys.h"
#include "blockout_types.h"
#include "blockout_math.h"
#include "blockout_shapes.h"
#include "blockout_pit.h"
#include "blockout_state.h"


#define KEYBOARD_INPUT 0xFF10
#define KEYBOARD_BYTES 32
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
extern uint8_t keystates[32];
extern bool handled_key;

#endif