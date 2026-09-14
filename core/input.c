/* ArcDroid merged keyboard/mouse input state. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <string.h>

void arc_input_reset(ArcInputState *state) { if (state) memset(state, 0, sizeof(*state)); }

void arc_input_key(ArcInputState *state, unsigned scancode, bool down, bool mapped)
{
   if (!state || scancode >= 512u) return;
   if (mapped) state->mapped[scancode] = down;
   else state->keyboard[scancode] = down;
}

bool arc_input_key_down(const ArcInputState *state, unsigned scancode)
{
   return state && scancode < 512u && (state->keyboard[scancode] || state->mapped[scancode]);
}

void arc_input_mouse_add(ArcInputState *state, int dx, int dy, unsigned buttons)
{
   if (!state) return;
   state->mouse_x += dx; state->mouse_y += dy; state->mouse_buttons = buttons;
}

static int arc_take_axis(int *axis)
{
   int value = *axis;
   if (value > 63) value = 63;
   if (value < -63) value = -63;
   *axis -= value;
   return value;
}

void arc_input_mouse_packet(ArcInputState *state, int *dx, int *dy, unsigned *buttons)
{
   if (!state || !dx || !dy || !buttons) return;
   *dx = arc_take_axis(&state->mouse_x);
   *dy = arc_take_axis(&state->mouse_y);
   *buttons = state->mouse_buttons;
}

void arc_input_pointer(ArcInputState *state, bool pressed, int x, int y, unsigned buttons)
{
   if (!state) return;
   if (!pressed)
   {
      state->pointer_contact = false;
      state->mouse_buttons = 0u;
      return;
   }
   if (state->pointer_contact)
      arc_input_mouse_add(state, x - state->pointer_x, y - state->pointer_y, buttons);
   else
      state->mouse_buttons = buttons;
   state->pointer_x = x; state->pointer_y = y; state->pointer_contact = true;
}
