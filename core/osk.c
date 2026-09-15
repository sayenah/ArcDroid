/* ArcDroid on-screen keyboard model and clipped XRGB8888 renderer.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "libretro.h"
#include "osk_font.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { ARC_OSK_REPEAT_DELAY_US = 300000u, ARC_OSK_REPEAT_US = 90000u };

static const ArcInputKey *arc_osk_catalogue_key(const ArcInputKey *shared,
                                                size_t shared_count,
                                                const ArcInputKey *osk_only,
                                                size_t index)
{
   return index < shared_count ? &shared[index] :
      &osk_only[index - shared_count];
}

static int arc_osk_modifier_index(unsigned key)
{
   if (key == RETROK_LSHIFT) return 0;
   if (key == RETROK_RSHIFT) return 1;
   if (key == RETROK_LCTRL) return 2;
   if (key == RETROK_RCTRL) return 3;
   if (key == RETROK_LALT) return 4;
   if (key == RETROK_RALT) return 5;
   return -1;
}

static const char *arc_osk_key_label(const ArcInputKey *key, bool compact)
{
   switch (key->key)
   {
      case RETROK_BACKSPACE: return "Bks";
      case RETROK_ESCAPE: return "Esc";
      case RETROK_RETURN: return "Ret";
      case RETROK_INSERT: return "Ins";
      case RETROK_DELETE: return "Del";
      case RETROK_HOME: return compact ? "Hom" : "Home";
      case RETROK_END: return compact ? "Cpy" : "Copy";
      case RETROK_PAGEUP: return compact ? "PgU" : "PgUp";
      case RETROK_PAGEDOWN: return compact ? "PgD" : "PgDn";
      case RETROK_SPACE: return compact ? "Spc" : "Space";
      case RETROK_UP: return "\x7d";
      case RETROK_DOWN: return "\x7e";
      case RETROK_LEFT: return "<";
      case RETROK_RIGHT: return ">";
      case RETROK_LSHIFT: return compact ? "LSh" : "LShft";
      case RETROK_RSHIFT: return compact ? "RSh" : "RShft";
      case RETROK_LCTRL: return compact ? "LCt" : "LCtrl";
      case RETROK_RCTRL: return compact ? "RCt" : "RCtrl";
      case RETROK_LALT: return compact ? "LAl" : "LAlt";
      case RETROK_RALT: return compact ? "RAl" : "RAlt";
      case RETROK_CAPSLOCK: return compact ? "Cps" : "Caps";
      case RETROK_NUMLOCK: return "Num";
      case RETROK_SCROLLOCK: return compact ? "Scr" : "Scrl";
      case RETROK_PAUSE: return compact ? "Pau" : "Pause";
      case RETROK_BACKSLASH: return compact ? "Bsl" : "Bslsh";
      case RETROK_KP0: return compact ? "K0" : "KP0";
      case RETROK_KP1: return compact ? "K1" : "KP1";
      case RETROK_KP2: return compact ? "K2" : "KP2";
      case RETROK_KP3: return compact ? "K3" : "KP3";
      case RETROK_KP4: return compact ? "K4" : "KP4";
      case RETROK_KP5: return compact ? "K5" : "KP5";
      case RETROK_KP6: return compact ? "K6" : "KP6";
      case RETROK_KP7: return compact ? "K7" : "KP7";
      case RETROK_KP8: return compact ? "K8" : "KP8";
      case RETROK_KP9: return compact ? "K9" : "KP9";
      case RETROK_KP_DIVIDE: return compact ? "K/" : "KP/";
      case RETROK_KP_MULTIPLY: return compact ? "K*" : "KP*";
      case RETROK_KP_MINUS: return compact ? "K-" : "KP-";
      case RETROK_KP_PLUS: return compact ? "K+" : "KP+";
      case RETROK_KP_PERIOD: return compact ? "K." : "KP.";
      case RETROK_KP_ENTER: return compact ? "KEn" : "KPEn";
      default: return key->label;
   }
}

static void arc_osk_add(ArcOsk *osk, ArcOskAction action, unsigned key,
                        const char *label, unsigned slot, unsigned columns,
                        unsigned panel_y, unsigned cell_h)
{
   if (osk->key_count >= ARC_OSK_MAX_KEYS || !columns) return;
   unsigned column = slot % columns;
   unsigned row = slot / columns;
   unsigned left = column * osk->width / columns;
   unsigned right = (column + 1u) * osk->width / columns;
   ArcOskKey *item = &osk->keys[osk->key_count++];
   *item = (ArcOskKey){action, key, label, left + 1u,
                       panel_y + 17u + row * cell_h,
                       right > left + 2u ? right - left - 2u : 1u,
                       cell_h > 2u ? cell_h - 2u : 1u};
}

bool arc_osk_layout(ArcOsk *osk, unsigned width, unsigned height)
{
   if (!osk || !width || !height) return false;
   osk->width = width;
   osk->height = height;
   osk->key_count = 0u;
   if (width < 320u)
   {
      osk->visible = false;
      return false;
   }
   osk->disabled = false;
   osk->compact = width < 640u;
   osk->page_count = osk->compact ? 2u : 1u;
   if (osk->page >= osk->page_count) osk->page = 0u;
   size_t shared_count = 0u;
   const ArcInputKey *catalogue = arc_input_supported_keys(&shared_count);
   size_t osk_only_count = 0u;
   const ArcInputKey *osk_only = arc_input_osk_only_keys(&osk_only_count);
   size_t count = shared_count + osk_only_count;
   size_t first = osk->compact ? (count + 1u) / 2u * osk->page : 0u;
   size_t last = osk->compact ? first + (count + 1u) / 2u : count;
   if (last > count) last = count;
   unsigned columns = osk->compact ? 10u : 14u;
   unsigned rows = (unsigned)((last - first + columns - 1u) / columns);
   if (osk->compact) ++rows;
   unsigned panel_h = height * 45u / 100u;
   if (panel_h < 48u) panel_h = 48u;
   if (panel_h > height) panel_h = height;
   unsigned panel_y = height - panel_h;
   unsigned cell_h = panel_h > 17u ? (panel_h - 17u) / (rows ? rows : 1u) : 1u;
   unsigned slot = 0u;
   for (size_t i = first; i < last; ++i)
   {
      const ArcInputKey *item = arc_osk_catalogue_key(catalogue, shared_count,
                                                       osk_only, i);
      int modifier = arc_osk_modifier_index(item->key);
      arc_osk_add(osk, modifier >= 0 ? ARC_OSK_ACTION_MODIFIER : ARC_OSK_ACTION_KEY,
                  item->key, arc_osk_key_label(item, osk->compact), slot++, columns,
                  panel_y, cell_h);
   }
   if (osk->compact)
      arc_osk_add(osk, ARC_OSK_ACTION_PAGE, 0u,
                  osk->page ? "PAGE 1" : "PAGE 2", slot, columns,
                  panel_y, cell_h);
   if (osk->focus >= osk->key_count) osk->focus = 0u;
   osk->contact = false;
   osk->contact_valid = false;
   return true;
}

bool arc_osk_open(ArcOsk *osk, unsigned width, unsigned height)
{
   if (!osk || !arc_osk_layout(osk, width, height)) return false;
   osk->visible = true;
   osk->busy = false;
   osk->interrupted_pending = false;
   osk->interrupted_visible = false;
   osk->repeat_dx = osk->repeat_dy = 0;
   return true;
}

void arc_osk_reset(ArcOsk *osk, ArcInputState *input)
{
   if (!osk) return;
   if (input) arc_input_clear_source(input, ARC_INPUT_SOURCE_OSK);
   memset(osk, 0, sizeof(*osk));
}

void arc_osk_close(ArcOsk *osk, ArcInputState *input)
{
   if (!osk) return;
   if (input) arc_input_clear_source(input, ARC_INPUT_SOURCE_OSK);
   osk->visible = false;
   osk->activation_phase = 0u;
   osk->activation_held = false;
   osk->busy = false;
   osk->interrupted_pending = false;
   osk->interrupted_visible = false;
   osk->contact = false;
   osk->contact_valid = false;
   memset(osk->modifiers, 0, sizeof(osk->modifiers));
}

static unsigned arc_osk_center_x(const ArcOskKey *key) { return key->x + key->width / 2u; }
static unsigned arc_osk_center_y(const ArcOskKey *key) { return key->y + key->height / 2u; }

void arc_osk_move(ArcOsk *osk, int dx, int dy, uint64_t guest_us, bool initial)
{
   if (!osk || !osk->visible || !osk->key_count || osk->activation_held ||
       (!dx && !dy)) return;
   if (!initial)
   {
      if (guest_us < osk->repeat_at_us) return;
      osk->repeat_at_us = guest_us + ARC_OSK_REPEAT_US;
   }
   else
      osk->repeat_at_us = guest_us + ARC_OSK_REPEAT_DELAY_US;
   osk->repeat_dx = dx;
   osk->repeat_dy = dy;
   const ArcOskKey *from = &osk->keys[osk->focus];
   int64_t best = INT64_MAX;
   unsigned selected = osk->focus;
   for (unsigned i = 0u; i < osk->key_count; ++i)
   {
      if (i == osk->focus) continue;
      int x = (int)arc_osk_center_x(&osk->keys[i]) - (int)arc_osk_center_x(from);
      int y = (int)arc_osk_center_y(&osk->keys[i]) - (int)arc_osk_center_y(from);
      if ((dx < 0 && x >= 0) || (dx > 0 && x <= 0) ||
          (dy < 0 && y >= 0) || (dy > 0 && y <= 0)) continue;
      int64_t primary = dx ? (x < 0 ? -x : x) : (y < 0 ? -y : y);
      int64_t secondary = dx ? (y < 0 ? -y : y) : (x < 0 ? -x : x);
      int64_t score = primary * primary + secondary * secondary * 4;
      if (score < best) { best = score; selected = i; }
   }
   osk->focus = selected;
}

static bool arc_osk_start(ArcOsk *osk, ArcInputState *input, unsigned key,
                          bool held)
{
   if (!osk || !input || osk->activation_phase || !key) return false;
   unsigned modifier_count = 0u;
   static const unsigned modifier_keys[ARC_OSK_MODIFIER_COUNT] = {
      RETROK_LSHIFT, RETROK_RSHIFT, RETROK_LCTRL,
      RETROK_RCTRL, RETROK_LALT, RETROK_RALT
   };
   for (unsigned i = 0u; i < ARC_OSK_MODIFIER_COUNT; ++i)
      if (osk->modifiers[i] != ARC_OSK_MOD_OFF)
         osk->activation_modifiers[modifier_count++] = modifier_keys[i];
   if (arc_input_available(input) < modifier_count * 2u + 2u)
   {
      osk->busy = true;
      return false;
   }
   osk->busy = false;
   osk->activation_key = key;
   osk->activation_modifier_count = modifier_count;
   osk->activation_held = held;
   osk->activation_generation = arc_input_reconcile_generation(input);
   osk->activation_phase = 1u;
   return true;
}

bool arc_osk_press_focused(ArcOsk *osk, ArcInputState *input)
{
   if (!osk || !osk->visible || osk->focus >= osk->key_count) return false;
   ArcOskKey *key = &osk->keys[osk->focus];
   if (key->action == ARC_OSK_ACTION_PAGE)
   {
      osk->page = (osk->page + 1u) % osk->page_count;
      return arc_osk_layout(osk, osk->width, osk->height);
   }
   if (key->action == ARC_OSK_ACTION_MODIFIER)
   {
      int index = arc_osk_modifier_index(key->key);
      if (index < 0) return false;
      osk->modifiers[index] = (ArcOskModifier)((osk->modifiers[index] + 1u) % 3u);
      return true;
   }
   return key->action == ARC_OSK_ACTION_KEY &&
          arc_osk_start(osk, input, key->key, true);
}

bool arc_osk_shortcut(ArcOsk *osk, ArcInputState *input, unsigned key)
{
   return osk && osk->visible && arc_input_key(key) &&
          arc_osk_start(osk, input, key, false);
}

void arc_osk_release_activation(ArcOsk *osk)
{
   if (osk) osk->activation_held = false;
}

void arc_osk_update(ArcOsk *osk, ArcInputState *input)
{
   if (!osk || !input || !osk->activation_phase) return;
   if (arc_input_reconcile_generation(input) != osk->activation_generation)
   {
      arc_input_clear_source(input, ARC_INPUT_SOURCE_OSK);
      osk->activation_phase = 0u;
      osk->activation_held = false;
      osk->interrupted_pending = true;
      osk->interrupted_visible = true;
      return;
   }
   if (osk->activation_phase == 1u)
   {
      for (unsigned i = 0u; i < osk->activation_modifier_count; ++i)
         arc_input_source_key(input, ARC_INPUT_SOURCE_OSK,
                              osk->activation_modifiers[i], true, true);
      arc_input_source_key(input, ARC_INPUT_SOURCE_OSK,
                           osk->activation_key, true, true);
      osk->activation_phase = 2u;
   }
   if (osk->activation_phase == 2u &&
       arc_input_applied_level(input, osk->activation_key) &&
       !osk->activation_held)
   {
      arc_input_source_key(input, ARC_INPUT_SOURCE_OSK,
                           osk->activation_key, false, true);
      for (unsigned i = osk->activation_modifier_count; i > 0u; --i)
         arc_input_source_key(input, ARC_INPUT_SOURCE_OSK,
                              osk->activation_modifiers[i - 1u], false, true);
      osk->activation_phase = 3u;
   }
   if (osk->activation_phase == 3u)
   {
      bool settled = arc_input_applied_level(input, osk->activation_key) ==
                     arc_input_effective_level(input, osk->activation_key);
      for (unsigned i = 0u; i < osk->activation_modifier_count; ++i)
         settled = settled &&
            arc_input_applied_level(input, osk->activation_modifiers[i]) ==
            arc_input_effective_level(input, osk->activation_modifiers[i]);
      if (settled)
      {
         for (unsigned i = 0u; i < ARC_OSK_MODIFIER_COUNT; ++i)
            if (osk->modifiers[i] == ARC_OSK_MOD_ONESHOT)
               osk->modifiers[i] = ARC_OSK_MOD_OFF;
         osk->activation_phase = 0u;
      }
   }
}

bool arc_osk_touch(ArcOsk *osk, int x, int y, bool down, ArcInputState *input)
{
   if (!osk || !osk->visible) return false;
   if (down && !osk->contact)
   {
      osk->contact = true;
      osk->contact_valid = false;
      for (unsigned i = 0u; i < osk->key_count; ++i)
      {
         const ArcOskKey *key = &osk->keys[i];
         if (x >= (int)key->x && y >= (int)key->y &&
             x < (int)(key->x + key->width) && y < (int)(key->y + key->height))
         {
            osk->contact_key = i;
            osk->contact_valid = true;
            osk->focus = i;
            break;
         }
      }
      return osk->contact_valid;
   }
   if (down && osk->contact && osk->contact_valid)
   {
      const ArcOskKey *key = &osk->keys[osk->contact_key];
      if (x < (int)key->x || y < (int)key->y ||
          x >= (int)(key->x + key->width) || y >= (int)(key->y + key->height))
         osk->contact_valid = false;
      return osk->contact_valid;
   }
   if (!down && osk->contact)
   {
      bool activate = osk->contact_valid;
      osk->contact = false;
      osk->contact_valid = false;
      bool started = activate && arc_osk_press_focused(osk, input);
      if (started) arc_osk_release_activation(osk);
      return started;
   }
   return false;
}

bool arc_osk_take_interrupted(ArcOsk *osk)
{
   if (!osk || !osk->interrupted_pending) return false;
   osk->interrupted_pending = false;
   return true;
}

void arc_osk_acknowledge_render(ArcOsk *osk)
{
   if (osk) osk->interrupted_visible = false;
}

static uint32_t arc_osk_blend(uint32_t under, uint32_t over, unsigned alpha)
{
   uint32_t rb = (uint32_t)((((uint64_t)(under & UINT32_C(0x00ff00ff)) *
                              (255u - alpha)) +
                             ((uint64_t)(over & UINT32_C(0x00ff00ff)) * alpha)) >> 8u);
   uint32_t g = (uint32_t)((((uint64_t)(under & UINT32_C(0x0000ff00)) *
                             (255u - alpha)) +
                            ((uint64_t)(over & UINT32_C(0x0000ff00)) * alpha)) >> 8u);
   return (rb & UINT32_C(0x00ff00ff)) | (g & UINT32_C(0x0000ff00));
}

static void arc_osk_rect(uint32_t *pixels, unsigned width, unsigned height,
                         size_t stride, unsigned x, unsigned y, unsigned w,
                         unsigned h, uint32_t colour, unsigned alpha)
{
   if (x >= width || y >= height) return;
   if (w > width - x) w = width - x;
   if (h > height - y) h = height - y;
   for (unsigned row = 0u; row < h; ++row)
      for (unsigned column = 0u; column < w; ++column)
      {
         uint32_t *pixel = &pixels[(y + row) * stride + x + column];
         *pixel = arc_osk_blend(*pixel, colour, alpha);
      }
}

static void arc_osk_text(uint32_t *pixels, unsigned width, unsigned height,
                         size_t stride, unsigned x, unsigned y,
                         unsigned clip_right, const char *text, uint32_t colour)
{
   if (clip_right > width) clip_right = width;
   while (text && *text && x + 8u <= clip_right)
   {
      unsigned glyph;
      glyph = (unsigned char)*text++;
      if (glyph >= 'a' && glyph <= 'z') glyph -= 'a' - 'A';
      if (glyph >= 128u) glyph = '?';
      for (unsigned row = 0u; row < 8u && y + row < height; ++row)
         for (unsigned column = 0u; column < 8u; ++column)
            if (arc_osk_font[glyph][row] & (UINT8_C(1) << (7u - column)))
               pixels[(y + row) * stride + x + column] = colour;
      x += 8u;
   }
}

bool arc_osk_render(const ArcOsk *osk, uint32_t *pixels, unsigned width,
                    unsigned height, size_t pitch, const char *profile)
{
   if (!osk || !osk->visible || !pixels || width != osk->width ||
       height != osk->height || pitch < (size_t)width * sizeof(uint32_t) ||
       pitch % sizeof(uint32_t)) return false;
   size_t stride = pitch / sizeof(uint32_t);
   unsigned panel_h = height * 45u / 100u;
   if (panel_h < 48u) panel_h = 48u;
   if (panel_h > height) panel_h = height;
   unsigned panel_y = height - panel_h;
   unsigned alpha = osk->transparent ? 105u : 205u;
   arc_osk_rect(pixels, width, height, stride, 0u, panel_y, width, panel_h,
                UINT32_C(0x00101824), alpha);
   char header[192];
   snprintf(header, sizeof(header), "ARCDROID OSK - %s%s%s", profile ? profile : "Generic",
            osk->busy ? " - BUSY" : "", osk->interrupted_visible ? " - INTERRUPTED" : "");
   arc_osk_text(pixels, width, height, stride, 4u, panel_y + 4u, width, header,
                UINT32_C(0x00ffffff));
   for (unsigned i = 0u; i < osk->key_count; ++i)
   {
      const ArcOskKey *key = &osk->keys[i];
      uint32_t colour = i == osk->focus ? UINT32_C(0x00e0a020) : UINT32_C(0x00405060);
      int modifier = arc_osk_modifier_index(key->key);
      if (modifier >= 0 && osk->modifiers[modifier] != ARC_OSK_MOD_OFF)
         colour = osk->modifiers[modifier] == ARC_OSK_MOD_LOCKED ?
            UINT32_C(0x00c05050) : UINT32_C(0x0050a060);
      arc_osk_rect(pixels, width, height, stride, key->x, key->y,
                   key->width, key->height, colour, 225u);
      if (key->height >= 8u && key->width >= 8u)
         arc_osk_text(pixels, width, height, stride, key->x + 2u, key->y + 1u,
                      key->x + key->width, key->label,
                      UINT32_C(0x00ffffff));
   }
   return true;
}
