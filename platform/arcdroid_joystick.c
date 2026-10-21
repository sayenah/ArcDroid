/* ArcDroid joystick host backend; Slice 1 maps RetroPad to KART keys. GPL-2.0-only. */
#include "plat_joystick.h"
#include "arcdroid_platform.h"
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

void arc_platform_joystick_snapshot(bool active,
                                    const bool levels[ARC_PAD_PORTS][5])
{
   for (unsigned port = 0; port < ARC_PAD_PORTS; ++port)
   {
      const bool *v = levels ? levels[port] : NULL;
      bool up = active && v && v[ARC_PAD_UP];
      bool down = active && v && v[ARC_PAD_DOWN];
      bool left = active && v && v[ARC_PAD_LEFT];
      bool right = active && v && v[ARC_PAD_RIGHT];
      joystick_state[port].axis[0] = left == right ? 0 : left ? -32767 : 32767;
      joystick_state[port].axis[1] = up == down ? 0 : up ? -32767 : 32767;
      joystick_state[port].button[0] = active && v && v[ARC_PAD_A];
   }
}

#if defined(ARCDROID_SAVE_STATES)
#include "../engine/state-generated/arcdroid_joystick_state.inc"
#endif
