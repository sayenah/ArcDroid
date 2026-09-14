/* ArcDroid libretro-to-KART input backend. GPL-2.0-only. */
#include "arcdroid_platform.h"
#include "plat_input.h"

#include <string.h>

int key[512];
static int arc_mouse_x;
static int arc_mouse_y;
static int arc_mouse_buttons;

void input_init(void)
{
   memset(key, 0, sizeof(key));
   arc_mouse_x = arc_mouse_y = arc_mouse_buttons = 0;
}
void input_close(void) { input_init(); }
void mouse_capture_enable(void) {}
void mouse_capture_disable(void) {}
void mouse_poll_host(void) {}
void keyboard_poll_host(void) {}

void arc_platform_input_apply(ArcInputState *input)
{
   if (!input) return;
   for (unsigned code = 0; code < 512u; ++code)
      key[code] = arc_input_key_down(input, code) ? 1 : 0;
   int dx = 0, dy = 0;
   unsigned buttons = 0u;
   arc_input_mouse_packet(input, &dx, &dy, &buttons);
   arc_mouse_x += dx;
   arc_mouse_y += dy;
   arc_mouse_buttons = (int)buttons;
}

void mouse_get_mickeys(int *x, int *y)
{
   if (x) *x = arc_mouse_x;
   if (y) *y = arc_mouse_y;
   arc_mouse_x = arc_mouse_y = 0;
}
int mouse_get_buttons(void) { return arc_mouse_buttons; }
void get_mouse_deltas(int *x, int *y, int *buttons)
{
   mouse_get_mickeys(x, y);
   if (buttons) *buttons = arc_mouse_buttons;
}
