/* ArcDroid source-aware keyboard/mouse input service. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <limits.h>
#include <string.h>

enum {
   ARC_INPUT_ACT_IDLE = 0,
   ARC_INPUT_ACT_MODIFIERS_DOWN,
   ARC_INPUT_ACT_MODIFIERS_ACK,
   ARC_INPUT_ACT_MODIFIERS_HELD,
   ARC_INPUT_ACT_KEY_ACK,
   ARC_INPUT_ACT_KEY_HELD,
   ARC_INPUT_ACT_KEY_UP_ACK,
   ARC_INPUT_ACT_KEY_RELEASED,
   ARC_INPUT_ACT_MODIFIERS_UP_ACK,
   ARC_INPUT_ACT_HOLD_US = 60000u
};

static bool arc_effective_key(const ArcInputState *state, unsigned code)
{
   for (unsigned source = 0u; source < ARC_INPUT_SOURCE_COUNT; ++source)
      if (state->source_keys[source][code]) return true;
   return false;
}

static unsigned arc_effective_buttons(const ArcInputState *state)
{
   unsigned buttons = 0u;
   for (unsigned source = 0u; source < ARC_INPUT_SOURCE_COUNT; ++source)
      buttons |= state->source_buttons[source];
   return buttons & 7u;
}

static void arc_fifo_append_unchecked(ArcInputState *state, ArcInputEvent event)
{
   if (state->fifo_count < ARC_INPUT_FIFO_CAPACITY)
      state->fifo[state->fifo_count++] = event;
}

static void arc_reconcile(ArcInputState *state)
{
   ++state->reconcile_generation;
   state->fifo_count = 0u;
   state->discrete_since_motion = 0u;
   /* Releases are deliberately reserved ahead of presses so overflow recovery
    * can never strand an engine-visible level. The shipping adapter's static
    * capacity assertion guarantees the rebuilt transition set fits; omitted
    * presses would not be sampled again by this service. */
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (!arc_input_is_modifier(code) && state->applied_keys[code] &&
          !state->effective_keys[code])
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code, false, false, 0, 0});
   for (unsigned button = 1u; button <= 4u; button <<= 1u)
      if ((state->applied_buttons & button) && !(state->effective_buttons & button))
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_BUTTON, button, false, false, 0, 0});
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (arc_input_is_modifier(code) && !state->applied_keys[code] &&
          state->effective_keys[code])
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code, true, false, 0, 0});
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (!arc_input_is_modifier(code) && !state->applied_keys[code] &&
          state->effective_keys[code])
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code, true, false, 0, 0});
   for (unsigned button = 1u; button <= 4u; button <<= 1u)
      if (!(state->applied_buttons & button) && (state->effective_buttons & button))
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_BUTTON, button, true, false, 0, 0});
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (arc_input_is_modifier(code) && state->applied_keys[code] &&
          !state->effective_keys[code])
         arc_fifo_append_unchecked(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code, false, false, 0, 0});
}

static bool arc_fifo_append(ArcInputState *state, ArcInputEvent event)
{
   if (state->fifo_count >= ARC_INPUT_FIFO_CAPACITY)
   {
      state->overflow_report_pending = !state->overflow_reported;
      state->overflow_reported = true;
      arc_reconcile(state);
      return true;
   }
   arc_fifo_append_unchecked(state, event);
   return false;
}

void arc_input_reset(ArcInputState *state)
{
   if (state) memset(state, 0, sizeof(*state));
}

void arc_input_release_all(ArcInputState *state)
{
   if (!state) return;
   memset(state->source_keys, 0, sizeof(state->source_keys));
   memset(state->source_buttons, 0, sizeof(state->source_buttons));
   memset(state->effective_keys, 0, sizeof(state->effective_keys));
   state->effective_buttons = 0u;
   state->activation_claimed = false;
   arc_reconcile(state);
}

void arc_input_clear_source(ArcInputState *state, ArcInputSource source)
{
   if (!state || source >= ARC_INPUT_SOURCE_COUNT) return;
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (!arc_input_is_modifier(code) && state->source_keys[source][code])
         arc_input_source_key(state, source, code, false, true);
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (arc_input_is_modifier(code) && state->source_keys[source][code])
         arc_input_source_key(state, source, code, false, true);
   arc_input_source_buttons(state, source, 0u);
}

void arc_input_source_key(ArcInputState *state, ArcInputSource source,
                          unsigned scancode, bool down, bool supported)
{
   if (!state || source >= ARC_INPUT_SOURCE_COUNT || !supported ||
       !scancode || scancode >= ARC_INPUT_KEY_COUNT) return;
   if (state->source_keys[source][scancode] == down) return;
   state->source_keys[source][scancode] = down;
   bool effective = arc_effective_key(state, scancode);
   if (effective == state->effective_keys[scancode]) return;
   state->effective_keys[scancode] = effective;
   arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, scancode,
                                          effective, false, 0, 0});
}

void arc_input_source_buttons(ArcInputState *state, ArcInputSource source,
                              unsigned buttons)
{
   if (!state || source >= ARC_INPUT_SOURCE_COUNT) return;
   buttons &= 7u;
   if (state->source_buttons[source] == buttons) return;
   state->source_buttons[source] = (uint8_t)buttons;
   unsigned effective = arc_effective_buttons(state);
   unsigned changed = effective ^ state->effective_buttons;
   state->effective_buttons = (uint8_t)effective;
   for (unsigned button = 1u; button <= 4u; button <<= 1u)
      if (changed & button)
         if (arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_BUTTON, button,
                     (effective & button) != 0u, false, 0, 0}))
            break;
}

void arc_input_source_click(ArcInputState *state, ArcInputSource source,
                            unsigned button)
{
   if (!state || source >= ARC_INPUT_SOURCE_COUNT ||
       (button != 1u && button != 2u && button != 4u)) return;
   unsigned before = state->source_buttons[source];
   arc_input_source_buttons(state, source, before | button);
   arc_input_source_buttons(state, source, before & ~button);
}

static int64_t arc_saturating_add(int64_t left, int64_t right)
{
   if (right > 0 && left > INT64_MAX - right) return INT64_MAX;
   if (right < 0 && left < INT64_MIN - right) return INT64_MIN;
   return left + right;
}

void arc_input_motion(ArcInputState *state, int64_t dx, int64_t dy,
                      bool velocity)
{
   if (!state || (!dx && !dy)) return;
   if (velocity)
   {
      if (dx > 63) dx = 63; else if (dx < -63) dx = -63;
      if (dy > 63) dy = 63; else if (dy < -63) dy = -63;
      for (unsigned i = 0u; i < state->fifo_count; ++i)
         if (state->fifo[i].type == ARC_INPUT_EVENT_MOTION && state->fifo[i].velocity)
         {
            state->fifo[i].dx = dx;
            state->fifo[i].dy = dy;
            return;
         }
   }
   else if (state->fifo_count &&
            state->fifo[state->fifo_count - 1u].type == ARC_INPUT_EVENT_MOTION &&
            !state->fifo[state->fifo_count - 1u].velocity)
   {
      ArcInputEvent *tail = &state->fifo[state->fifo_count - 1u];
      tail->dx = arc_saturating_add(tail->dx, dx);
      tail->dy = arc_saturating_add(tail->dy, dy);
      return;
   }
   arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_MOTION, 0u, false,
                                          velocity, dx, dy});
}

static int arc_take_axis(int64_t *axis)
{
   int value = *axis > 63 ? 63 : *axis < -63 ? -63 : (int)*axis;
   *axis -= value;
   return value;
}

static void arc_fifo_remove(ArcInputState *state, unsigned index)
{
   if (index + 1u < state->fifo_count)
      memmove(&state->fifo[index], &state->fifo[index + 1u],
              (state->fifo_count - index - 1u) * sizeof(state->fifo[0]));
   --state->fifo_count;
}

bool arc_input_consume(ArcInputState *state, bool mouse_enabled,
                       ArcInputEvent *event)
{
   if (!state || !event || !state->fifo_count) return false;
   while (!mouse_enabled && state->fifo_count &&
          state->fifo[0].type == ARC_INPUT_EVENT_MOTION)
   {
      arc_fifo_remove(state, 0u);
      state->discrete_since_motion = 0u;
   }
   if (!state->fifo_count) return false;
   unsigned index = 0u;
   if (state->discrete_since_motion >= ARC_INPUT_MOTION_STARVATION_BOUND)
      for (unsigned i = 0u; i < state->fifo_count; ++i)
      {
         if (state->fifo[i].type == ARC_INPUT_EVENT_BUTTON) break;
         if (state->fifo[i].type == ARC_INPUT_EVENT_MOTION && mouse_enabled)
         {
            index = i;
            break;
         }
      }
   ArcInputEvent queued = state->fifo[index];
   *event = queued;
   if (queued.type == ARC_INPUT_EVENT_MOTION)
   {
      event->dx = arc_take_axis(&state->fifo[index].dx);
      event->dy = arc_take_axis(&state->fifo[index].dy);
      state->discrete_since_motion = 0u;
      if (!state->fifo[index].dx && !state->fifo[index].dy)
         arc_fifo_remove(state, index);
   }
   else
   {
      arc_fifo_remove(state, index);
      ++state->discrete_since_motion;
      if (queued.type == ARC_INPUT_EVENT_KEY)
         state->applied_keys[queued.code] = queued.down;
      else if (queued.down)
         state->applied_buttons |= (uint8_t)queued.code;
      else
         state->applied_buttons &= (uint8_t)~queued.code;
   }
   return true;
}

bool arc_input_take_overflow_report(ArcInputState *state)
{
   if (!state || !state->overflow_report_pending) return false;
   state->overflow_report_pending = false;
   return true;
}

unsigned arc_input_pending(const ArcInputState *state)
{
   return state ? state->fifo_count : 0u;
}

unsigned arc_input_available(const ArcInputState *state)
{
   return state && state->fifo_count < ARC_INPUT_FIFO_CAPACITY ?
      ARC_INPUT_FIFO_CAPACITY - state->fifo_count : 0u;
}

uint64_t arc_input_reconcile_generation(const ArcInputState *state)
{
   return state ? state->reconcile_generation : 0u;
}

bool arc_input_source_level(const ArcInputState *state, ArcInputSource source,
                            unsigned key)
{
   return state && source < ARC_INPUT_SOURCE_COUNT && key < ARC_INPUT_KEY_COUNT &&
          state->source_keys[source][key];
}

bool arc_input_effective_level(const ArcInputState *state, unsigned key)
{
   return state && key < ARC_INPUT_KEY_COUNT && state->effective_keys[key];
}

bool arc_input_applied_level(const ArcInputState *state, unsigned key)
{
   return state && key < ARC_INPUT_KEY_COUNT && state->applied_keys[key];
}

void arc_input_pad_key_sets(ArcInputState *state,
                            const bool port0[ARC_INPUT_KEY_COUNT],
                            const bool port1[ARC_INPUT_KEY_COUNT])
{
   if (!state || !port0 || !port1) return;
   bool before[ARC_INPUT_KEY_COUNT];
   memcpy(before, state->effective_keys, sizeof(before));
   memcpy(state->source_keys[ARC_INPUT_SOURCE_PAD0], port0,
          sizeof(state->source_keys[0]));
   memcpy(state->source_keys[ARC_INPUT_SOURCE_PAD1], port1,
          sizeof(state->source_keys[0]));
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      state->effective_keys[code] = arc_effective_key(state, code);

   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (arc_input_is_modifier(code) && !before[code] &&
          state->effective_keys[code])
         if (arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code,
                                                    true, false, 0, 0}))
            return;
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (!arc_input_is_modifier(code) && !before[code] &&
          state->effective_keys[code])
         if (arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code,
                                                    true, false, 0, 0}))
            return;
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (!arc_input_is_modifier(code) && before[code] &&
          !state->effective_keys[code])
         if (arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code,
                                                    false, false, 0, 0}))
            return;
   for (unsigned code = 1u; code < ARC_INPUT_KEY_COUNT; ++code)
      if (arc_input_is_modifier(code) && before[code] &&
          !state->effective_keys[code])
         if (arc_fifo_append(state, (ArcInputEvent){ARC_INPUT_EVENT_KEY, code,
                                                    false, false, 0, 0}))
            return;
}

bool arc_input_activation_claim(ArcInputState *state, ArcInputSource source)
{
   if (!state || source >= ARC_INPUT_SOURCE_COUNT || state->activation_claimed)
      return false;
   state->activation_claimed = true;
   state->activation_source = source;
   return true;
}

void arc_input_activation_release(ArcInputState *state, ArcInputSource source)
{
   if (state && state->activation_claimed && state->activation_source == source)
      state->activation_claimed = false;
}

static uint64_t arc_input_activation_deadline(uint64_t guest_us)
{
   return guest_us > UINT64_MAX - ARC_INPUT_ACT_HOLD_US ?
      UINT64_MAX : guest_us + ARC_INPUT_ACT_HOLD_US;
}

bool arc_input_activation_begin(ArcInputActivation *activation,
                                ArcInputState *state, ArcInputSource source,
                                unsigned key, const unsigned *modifiers,
                                unsigned modifier_count)
{
   if (!activation || !state || !key || activation->phase ||
       modifier_count > ARC_INPUT_ACTIVATION_MODIFIERS ||
       (modifier_count && !modifiers) ||
       arc_input_available(state) < modifier_count * 2u + 2u ||
       !arc_input_activation_claim(state, source))
      return false;
   memset(activation, 0, sizeof(*activation));
   activation->key = key;
   activation->modifier_count = modifier_count;
   if (modifier_count)
      memcpy(activation->modifiers, modifiers,
             modifier_count * sizeof(modifiers[0]));
   activation->generation = arc_input_reconcile_generation(state);
   activation->phase = ARC_INPUT_ACT_MODIFIERS_DOWN;
   return true;
}

void arc_input_activation_cancel(ArcInputActivation *activation,
                                 ArcInputState *state, ArcInputSource source)
{
   if (!activation || !state) return;
   arc_input_clear_source(state, source);
   memset(activation, 0, sizeof(*activation));
   arc_input_activation_release(state, source);
}

bool arc_input_activation_active(const ArcInputActivation *activation)
{
   return activation && activation->phase != ARC_INPUT_ACT_IDLE;
}

ArcInputActivationResult arc_input_activation_update(
   ArcInputActivation *activation, ArcInputState *state,
   ArcInputSource source, uint64_t guest_us)
{
   if (!activation || !state || !activation->phase)
      return ARC_INPUT_ACTIVATION_COMPLETE;
   if (arc_input_reconcile_generation(state) != activation->generation)
   {
      arc_input_activation_cancel(activation, state, source);
      return ARC_INPUT_ACTIVATION_INTERRUPTED;
   }
   if (activation->phase == ARC_INPUT_ACT_MODIFIERS_DOWN)
   {
      for (unsigned i = 0u; i < activation->modifier_count; ++i)
         arc_input_source_key(state, source, activation->modifiers[i], true,
                              true);
      if (activation->modifier_count)
         activation->phase = ARC_INPUT_ACT_MODIFIERS_ACK;
      else
      {
         arc_input_source_key(state, source, activation->key, true, true);
         activation->phase = ARC_INPUT_ACT_KEY_ACK;
      }
      return ARC_INPUT_ACTIVATION_RUNNING;
   }
   if (activation->phase == ARC_INPUT_ACT_MODIFIERS_ACK)
   {
      for (unsigned i = 0u; i < activation->modifier_count; ++i)
         if (!arc_input_applied_level(state, activation->modifiers[i]))
            return ARC_INPUT_ACTIVATION_RUNNING;
      activation->deadline_us = arc_input_activation_deadline(guest_us);
      activation->phase = ARC_INPUT_ACT_MODIFIERS_HELD;
   }
   if (activation->phase == ARC_INPUT_ACT_MODIFIERS_HELD)
   {
      if (guest_us < activation->deadline_us)
         return ARC_INPUT_ACTIVATION_RUNNING;
      arc_input_source_key(state, source, activation->key, true, true);
      activation->phase = ARC_INPUT_ACT_KEY_ACK;
      return ARC_INPUT_ACTIVATION_RUNNING;
   }
   if (activation->phase == ARC_INPUT_ACT_KEY_ACK)
   {
      if (!arc_input_applied_level(state, activation->key))
         return ARC_INPUT_ACTIVATION_RUNNING;
      activation->deadline_us = arc_input_activation_deadline(guest_us);
      activation->phase = ARC_INPUT_ACT_KEY_HELD;
   }
   if (activation->phase == ARC_INPUT_ACT_KEY_HELD)
   {
      if (guest_us < activation->deadline_us)
         return ARC_INPUT_ACTIVATION_RUNNING;
      arc_input_source_key(state, source, activation->key, false, true);
      activation->phase = ARC_INPUT_ACT_KEY_UP_ACK;
      return ARC_INPUT_ACTIVATION_RUNNING;
   }
   if (activation->phase == ARC_INPUT_ACT_KEY_UP_ACK)
   {
      if (arc_input_applied_level(state, activation->key) !=
          arc_input_effective_level(state, activation->key))
         return ARC_INPUT_ACTIVATION_RUNNING;
      activation->deadline_us = arc_input_activation_deadline(guest_us);
      activation->phase = ARC_INPUT_ACT_KEY_RELEASED;
   }
   if (activation->phase == ARC_INPUT_ACT_KEY_RELEASED)
   {
      if (guest_us < activation->deadline_us)
         return ARC_INPUT_ACTIVATION_RUNNING;
      for (unsigned i = activation->modifier_count; i > 0u; --i)
         arc_input_source_key(state, source, activation->modifiers[i - 1u],
                              false, true);
      activation->phase = ARC_INPUT_ACT_MODIFIERS_UP_ACK;
      if (activation->modifier_count)
         return ARC_INPUT_ACTIVATION_RUNNING;
   }
   if (activation->phase == ARC_INPUT_ACT_MODIFIERS_UP_ACK)
   {
      bool settled = arc_input_applied_level(state, activation->key) ==
                     arc_input_effective_level(state, activation->key);
      for (unsigned i = 0u; i < activation->modifier_count; ++i)
         settled = settled &&
            arc_input_applied_level(state, activation->modifiers[i]) ==
            arc_input_effective_level(state, activation->modifiers[i]);
      if (!settled) return ARC_INPUT_ACTIVATION_RUNNING;
      memset(activation, 0, sizeof(*activation));
      arc_input_activation_release(state, source);
      return ARC_INPUT_ACTIVATION_COMPLETE;
   }
   return ARC_INPUT_ACTIVATION_RUNNING;
}
