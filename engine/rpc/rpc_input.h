/* ArcDroid RPC PS/2 device and virtual input clock edge. GPL-2.0-only. */
#ifndef ARC_RPC_INPUT_H
#define ARC_RPC_INPUT_H
#include <stdbool.h>
#include <stdint.h>
void arc_rpc_input_reset(void);
void arc_rpc_input_event(void);
void arc_rpc_input_key(unsigned code, bool down);
void arc_rpc_input_typematic(unsigned value);
void arc_rpc_input_scanning(bool enabled);
bool arc_rpc_input_delete_held(void);
void arc_rpc_input_factory(bool enabled);
unsigned arc_rpc_keyboard_room(void);
unsigned arc_rpc_mouse_room(void);
#endif
