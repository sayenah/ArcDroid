/* ArcDroid joystick host backend; Slice 1 maps RetroPad to KART keys. GPL-2.0-only. */
#include "plat_joystick.h"
#include <string.h>
plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t joystick_state[MAX_JOYSTICKS];
int joysticks_present;
void joystick_init(void)
{
   memset(plat_joystick_state, 0, sizeof(plat_joystick_state));
   memset(joystick_state, 0, sizeof(joystick_state));
   joysticks_present = 0;
}
void joystick_close(void) { joystick_init(); }
void joystick_poll_host(void) {}
