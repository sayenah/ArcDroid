/* ArcDroid libretro-to-KART input backend. GPL-2.0-only. */
#include "arcdroid_platform.h"
#include "arc.h"
#include "plat_input.h"
#include "timer.h"

#include <string.h>

int key[512];
static int arc_mouse_x;
static int arc_mouse_y;
static int arc_mouse_buttons;
static ArcInputState *arc_input_service;
extern int mouseena, keyena;

#if defined(ARCDROID_TEST_FAULTS)
#define ARC_TEST_KART_EVENT_CAPACITY 4096u
static ArcKartEvent arc_test_events[ARC_TEST_KART_EVENT_CAPACITY];
static size_t arc_test_event_count;

void arc_platform_test_input_events_reset(void)
{
   arc_test_event_count = 0u;
}

size_t arc_platform_test_input_events(ArcKartEvent *events, size_t capacity)
{
   size_t count = arc_test_event_count < capacity ? arc_test_event_count : capacity;
   if (events && count) memcpy(events, arc_test_events, count * sizeof(*events));
   return arc_test_event_count;
}

static void arc_test_record(const ArcInputEvent *event)
{
   if (!event || arc_test_event_count >= ARC_TEST_KART_EVENT_CAPACITY) return;
   ArcKartEvent *record = &arc_test_events[arc_test_event_count++];
   record->type = event->type;
   record->code = event->code;
   record->down = event->down;
   record->dx = (int)event->dx;
   record->dy = (int)event->dy;
   record->guest_us = speed_mhz > 0 ? (tsc >> 32) / (uint64_t)speed_mhz : 0u;
}
#endif

void input_init(void)
{
   memset(key, 0, sizeof(key));
   arc_mouse_x = arc_mouse_y = arc_mouse_buttons = 0;
   arc_input_service = NULL;
   /* Arculator keeps these KART enable latches outside keyboard_init(). Clear
    * them with the rest of the platform input state so a recreated machine
    * cannot consume queued input between RAK2 and the guest's enable byte. */
   mouseena = keyena = 0;
#if defined(ARCDROID_TEST_FAULTS)
   arc_platform_test_input_events_reset();
#endif
}
void input_close(void) { input_init(); }
void mouse_capture_enable(void) {}
void mouse_capture_disable(void) {}
void mouse_poll_host(void) {}
void keyboard_poll_host(void) {}

void arc_platform_input_apply(ArcInputState *input)
{
   arc_input_service = input;
}

void arc_platform_input_consume(int mouse_enabled)
{
   ArcInputEvent event;
   if (!arc_input_consume(arc_input_service, mouse_enabled != 0, &event)) return;
   switch (event.type)
   {
      case ARC_INPUT_EVENT_KEY:
         key[event.code] = event.down ? 1 : 0;
         break;
      case ARC_INPUT_EVENT_BUTTON:
         if (event.down) arc_mouse_buttons |= (int)event.code;
         else arc_mouse_buttons &= ~(int)event.code;
         break;
      case ARC_INPUT_EVENT_MOTION:
         arc_mouse_x += (int)event.dx;
         arc_mouse_y += (int)event.dy;
         break;
      default:
         return;
   }
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_record(&event);
#endif
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
