/* ArcDroid pre-boot launch chooser model, record and renderer. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "osk_font.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
   ARC_CHOOSER_DESKTOP = 0xffu,
   ARC_CHOOSER_TOP = 92u,
   ARC_CHOOSER_ROW_HEIGHT = 20u,
   ARC_CHOOSER_LEFT = 64u,
   ARC_CHOOSER_RIGHT = 576u,
   ARC_CHOOSER_BOTTOM = 452u
};

typedef struct {
   const uint8_t *data;
   size_t size;
   size_t at;
} ArcChoiceCursor;

static bool arc_choice_line(ArcChoiceCursor *cursor, const uint8_t **line,
                            size_t *length)
{
   if (!cursor || !line || !length || cursor->at >= cursor->size) return false;
   size_t start = cursor->at;
   while (cursor->at < cursor->size && cursor->data[cursor->at] != '\n')
      ++cursor->at;
   if (cursor->at >= cursor->size) return false;
   *line = cursor->data + start;
   *length = cursor->at++ - start;
   return true;
}

static int arc_choice_hex(uint8_t value)
{
   if (value >= '0' && value <= '9') return value - '0';
   if (value >= 'a' && value <= 'f') return value - 'a' + 10;
   return -1;
}

static bool arc_choice_fingerprint(const uint8_t *data, size_t size,
                                   char output[41])
{
   if (!data || size != 40u) return false;
   for (size_t i = 0u; i < size; ++i)
      if (arc_choice_hex(data[i]) < 0) return false;
   memcpy(output, data, size);
   output[size] = '\0';
   return true;
}

static bool arc_choice_component(const uint8_t *line, size_t length,
                                  ArcGuestName *name, bool bang)
{
   if (!length || length > 2u * ARC_GUEST_NAME_MAX || (length & 1u))
      return false;
   name->length = (uint8_t)(length / 2u);
   for (unsigned i = 0u; i < name->length; ++i)
   {
      int high = arc_choice_hex(line[2u * i]);
      int low = arc_choice_hex(line[2u * i + 1u]);
      if (high < 0 || low < 0) return false;
      name->bytes[i] = (uint8_t)((unsigned)high << 4u | (unsigned)low);
   }
   return bang ? name->bytes[0] == '!' : name->bytes[0] != '!';
}

bool arc_launch_choice_parse(const void *data, size_t size,
                             ArcLaunchChoiceRecord *choice)
{
   if (!data || !choice || !size || size > ARC_LAUNCH_CHOICE_MAX_SIZE)
      return false;
   ArcChoiceCursor cursor = {(const uint8_t *)data, size, 0u};
   const uint8_t *version, *kind, *target, *fingerprint, *leaf = NULL;
   size_t version_size, kind_size, target_size, fingerprint_size,
      leaf_size = 0u;
   if (!arc_choice_line(&cursor, &version, &version_size) ||
       !arc_choice_line(&cursor, &kind, &kind_size) ||
       !arc_choice_line(&cursor, &target, &target_size) ||
       version_size != 2u) return false;
   bool nested = !memcmp(version, "v3", 2u);
   if (nested && !arc_choice_line(&cursor, &leaf, &leaf_size)) return false;
   if (!nested && memcmp(version, "v1", 2u)) return false;
   if (!arc_choice_line(&cursor, &fingerprint, &fingerprint_size) ||
       cursor.at != cursor.size) return false;
   ArcLaunchChoiceRecord parsed;
   memset(&parsed, 0, sizeof(parsed));
   parsed.version = nested ? 3u : 1u;
   if (kind_size == 7u && !memcmp(kind, "desktop", 7u))
   {
      if (target_size != 1u || target[0] != '-' ||
          (nested && (leaf_size != 1u || leaf[0] != '-'))) return false;
      parsed.desktop = true;
   }
   else if (kind_size == 3u && !memcmp(kind, "app", 3u))
   {
      if (nested)
      {
         if (!arc_choice_component(target, target_size, &parsed.parent, false) ||
             !arc_choice_component(leaf, leaf_size, &parsed.target, true))
            return false;
      }
      else
      {
      if (!target_size || target_size > ARC_GUEST_NAME_MAX * 2u ||
          (target_size & 1u)) return false;
      parsed.target.length = (uint8_t)(target_size / 2u);
      for (unsigned i = 0u; i < parsed.target.length; ++i)
      {
         int high = arc_choice_hex(target[i * 2u]);
         int low = arc_choice_hex(target[i * 2u + 1u]);
         if (high < 0 || low < 0) return false;
         parsed.target.bytes[i] = (uint8_t)((unsigned)high << 4u | (unsigned)low);
      }
      if (parsed.target.bytes[0] != '!') return false;
      }
   }
   else return false;
   if (!arc_choice_fingerprint(fingerprint, fingerprint_size,
                               parsed.candidate_fingerprint)) return false;
   *choice = parsed;
   return true;
}

bool arc_launch_choice_format_nested(const ArcGuestName *parent,
                              const ArcGuestName *target, bool desktop,
                              const char candidate_fingerprint[41],
                              uint8_t *output, size_t capacity,
                              size_t *output_size)
{
   static const char hex[] = "0123456789abcdef";
   if (output_size) *output_size = 0u;
   if ((!desktop && (!parent || !parent->length ||
                     parent->length > ARC_GUEST_NAME_MAX ||
                     parent->bytes[0] == '!' || !target || !target->length ||
                     target->length > ARC_GUEST_NAME_MAX ||
                     target->bytes[0] != '!')) ||
       !candidate_fingerprint || candidate_fingerprint[40] != '\0') return false;
   for (unsigned i = 0u; i < 40u; ++i)
      if (arc_choice_hex((uint8_t)candidate_fingerprint[i]) < 0) return false;
   size_t required = desktop ? 3u + 8u + 2u + 2u + 41u :
      3u + 4u + 2u * parent->length + 1u + 2u * target->length + 1u + 41u;
   if (output_size) *output_size = required;
   if (!output) return capacity == 0u;
   if (capacity < required) return false;
   size_t at = 0u;
   memcpy(output + at, "v3\n", 3u); at += 3u;
   if (desktop)
   {
      memcpy(output + at, "desktop\n-\n-\n", 12u); at += 12u;
   }
   else
   {
      memcpy(output + at, "app\n", 4u); at += 4u;
      for (unsigned part = 0u; part < 2u; ++part)
      {
         const ArcGuestName *name = part ? target : parent;
         for (unsigned i = 0u; i < name->length; ++i)
         {
            output[at++] = (uint8_t)hex[name->bytes[i] >> 4u];
            output[at++] = (uint8_t)hex[name->bytes[i] & 15u];
         }
         output[at++] = '\n';
      }
   }
   memcpy(output + at, candidate_fingerprint, 40u); at += 40u;
   output[at++] = '\n';
   return at == required;
}

bool arc_launch_choice_format(const ArcGuestName *target, bool desktop,
                              const char candidate_fingerprint[41],
                              uint8_t *output, size_t capacity,
                              size_t *output_size)
{
   static const char hex[] = "0123456789abcdef";
   if (output_size) *output_size = 0u;
   if ((!desktop && (!target || !target->length ||
                     target->length > ARC_GUEST_NAME_MAX ||
                     target->bytes[0] != '!')) ||
       !candidate_fingerprint || candidate_fingerprint[40] != '\0') return false;
   for (unsigned i = 0u; i < 40u; ++i)
      if (arc_choice_hex((uint8_t)candidate_fingerprint[i]) < 0) return false;
   size_t required = desktop ? 2u + 1u + 7u + 1u + 1u + 1u + 40u + 1u :
      2u + 1u + 3u + 1u + (size_t)target->length * 2u + 1u + 40u + 1u;
   if (output_size) *output_size = required;
   if (!output) return capacity == 0u;
   if (capacity < required) return false;
   size_t at = 0u;
   memcpy(output + at, "v1\n", 3u); at += 3u;
   if (desktop)
   {
      memcpy(output + at, "desktop\n-\n", 10u); at += 10u;
   }
   else
   {
      memcpy(output + at, "app\n", 4u); at += 4u;
      for (unsigned i = 0u; i < target->length; ++i)
      {
         output[at++] = (uint8_t)hex[target->bytes[i] >> 4u];
         output[at++] = (uint8_t)hex[target->bytes[i] & 15u];
      }
      output[at++] = '\n';
   }
   memcpy(output + at, candidate_fingerprint, 40u); at += 40u;
   output[at++] = '\n';
   return at == required;
}

int arc_launch_choice_match(const ArcLaunchDiscovery *discovery,
                            const ArcLaunchChoiceRecord *choice)
{
   if (!discovery || !choice) return INT_MIN;
   if (choice->desktop) return -1;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
      if (arc_launch_candidate_is_target(&discovery->candidates[i]) &&
          ((choice->version == 3u) ==
             (discovery->candidates[i].parent.length != 0u)) &&
          discovery->candidates[i].parent.length == choice->parent.length &&
          !memcmp(discovery->candidates[i].parent.bytes, choice->parent.bytes,
                  choice->parent.length) &&
          discovery->candidates[i].name.length == choice->target.length &&
          !memcmp(discovery->candidates[i].name.bytes, choice->target.bytes,
                  choice->target.length)) return (int)i;
   return INT_MIN;
}

static unsigned arc_chooser_visible_rows(const ArcLaunchChooser *chooser)
{
   return ((chooser->nested_scope ? 392u : ARC_CHOOSER_BOTTOM) -
           ARC_CHOOSER_TOP) / ARC_CHOOSER_ROW_HEIGHT;
}

static void arc_chooser_scroll(ArcLaunchChooser *chooser)
{
   unsigned visible = arc_chooser_visible_rows(chooser);
   if (chooser->focus < chooser->first_visible)
      chooser->first_visible = chooser->focus;
   else if (chooser->focus >= chooser->first_visible + visible)
      chooser->first_visible = chooser->focus + 1u - visible;
}

bool arc_launch_chooser_init(ArcLaunchChooser *chooser,
                             const ArcLaunchDiscovery *discovery,
                             bool remembered, int remembered_candidate)
{
   if (!chooser || !discovery) return false;
   memset(chooser, 0, sizeof(*chooser));
   chooser->inspection_incomplete =
      discovery->reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE;
   chooser->nested_scope = discovery->nested_scope;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
      if (arc_launch_candidate_is_target(&discovery->candidates[i]))
         chooser->candidate_index[chooser->row_count++] = (uint8_t)i;
   if (!chooser->row_count) return false;
   chooser->candidate_index[chooser->row_count++] = ARC_CHOOSER_DESKTOP;
   if (remembered)
      for (unsigned row = 0u; row < chooser->row_count; ++row)
         if ((remembered_candidate < 0 &&
              chooser->candidate_index[row] == ARC_CHOOSER_DESKTOP) ||
             (remembered_candidate >= 0 &&
              chooser->candidate_index[row] == (unsigned)remembered_candidate))
            chooser->focus = row;
   arc_chooser_scroll(chooser);
   return true;
}

bool arc_launch_chooser_install_init(ArcLaunchChooser *chooser,
                                    const char *const *labels, unsigned count,
                                    int remembered_candidate)
{
   if (!chooser || !labels || !count || count > 2u) return false;
   memset(chooser, 0, sizeof(*chooser));
   chooser->install_mode = true;
   chooser->row_count = count;
   for (unsigned i = 0u; i < count; ++i)
   {
      if (!labels[i] || strlen(labels[i]) >= sizeof(chooser->install_labels[i]))
         return false;
      chooser->candidate_index[i] = (uint8_t)i;
      strcpy(chooser->install_labels[i], labels[i]);
   }
   if (remembered_candidate >= 0 && (unsigned)remembered_candidate < count)
      chooser->focus = (unsigned)remembered_candidate;
   return true;
}

void arc_launch_chooser_move(ArcLaunchChooser *chooser, int direction)
{
   if (!chooser || !chooser->row_count || !direction) return;
   if (direction < 0)
      chooser->focus = chooser->focus ? chooser->focus - 1u :
                                        chooser->row_count - 1u;
   else
      chooser->focus = chooser->focus + 1u < chooser->row_count ?
         chooser->focus + 1u : 0u;
   arc_chooser_scroll(chooser);
}

int arc_launch_chooser_selection(const ArcLaunchChooser *chooser)
{
   if (!chooser || chooser->focus >= chooser->row_count) return INT_MIN;
   uint8_t selected = chooser->candidate_index[chooser->focus];
   return selected == ARC_CHOOSER_DESKTOP ? -1 : (int)selected;
}

bool arc_launch_chooser_focus(ArcLaunchChooser *chooser, unsigned row)
{
   if (!chooser || row >= chooser->row_count) return false;
   chooser->focus = row;
   arc_chooser_scroll(chooser);
   return true;
}

int arc_launch_chooser_hit(const ArcLaunchChooser *chooser,
                           unsigned x, unsigned y)
{
   if (!chooser || x < ARC_CHOOSER_LEFT || x >= ARC_CHOOSER_RIGHT ||
       y < ARC_CHOOSER_TOP ||
       y >= (chooser->nested_scope ? 392u : ARC_CHOOSER_BOTTOM)) return -1;
   unsigned row = chooser->first_visible +
      (y - ARC_CHOOSER_TOP) / ARC_CHOOSER_ROW_HEIGHT;
   return row < chooser->row_count ? (int)row : -1;
}

static void arc_fill(uint32_t *pixels, unsigned width, unsigned height,
                     size_t stride, unsigned x, unsigned y, unsigned w,
                     unsigned h, uint32_t colour)
{
   if (x >= width || y >= height) return;
   if (w > width - x) w = width - x;
   if (h > height - y) h = height - y;
   for (unsigned row = 0u; row < h; ++row)
      for (unsigned column = 0u; column < w; ++column)
         pixels[(size_t)(y + row) * stride + x + column] = colour;
}

static void arc_glyph(uint32_t *pixels, unsigned width, unsigned height,
                      size_t stride, unsigned x, unsigned y, uint8_t c,
                      uint32_t colour)
{
   if (c < 0x20u || c > 0x7eu) c = '?';
   for (unsigned row = 0u; row < 8u; ++row)
      for (unsigned bit = 0u; bit < 8u; ++bit)
         if ((arc_osk_font[c][row] >> (7u - bit)) & 1u)
            arc_fill(pixels, width, height, stride, x + bit * 2u,
                     y + row * 2u, 2u, 2u, colour);
}

static unsigned arc_text(uint32_t *pixels, unsigned width, unsigned height,
                         size_t stride, unsigned x, unsigned y,
                         const char *text, uint32_t colour)
{
   for (; text && *text; ++text, x += 16u)
      arc_glyph(pixels, width, height, stride, x, y, (uint8_t)*text, colour);
   return x;
}

static unsigned arc_name_characters(const ArcGuestName *name)
{
   unsigned characters = 0u;
   for (unsigned i = 0u; name && i < name->length; ++i)
      characters += name->bytes[i] < 0x20u || name->bytes[i] > 0x7eu ? 4u :
         (name->bytes[i] == '"' || name->bytes[i] == '\\' ? 2u : 1u);
   return characters;
}

static void arc_name(uint32_t *pixels, unsigned width, unsigned height,
                     size_t stride, unsigned x, unsigned y,
                     const ArcGuestName *name, unsigned scale,
                     uint32_t colour)
{
   static const char hex[] = "0123456789ABCDEF";
   for (unsigned i = 0u; name && i < name->length; ++i)
   {
      uint8_t c = name->bytes[i];
      char rendered[5] = {(char)c, '\0', '\0', '\0', '\0'};
      uint32_t rendered_colour = colour;
      if (c < 0x20u || c > 0x7eu)
      {
         rendered[0] = '\\'; rendered[1] = 'x';
         rendered[2] = hex[c >> 4u]; rendered[3] = hex[c & 15u];
         rendered_colour = UINT32_C(0x00ffd060);
      }
      else if (c == '"' || c == '\\')
      {
         rendered[0] = '\\'; rendered[1] = (char)c;
      }
      for (unsigned character = 0u; rendered[character];
           ++character, x += 8u * scale)
         for (unsigned row = 0u; row < 8u; ++row)
            for (unsigned bit = 0u; bit < 8u; ++bit)
               if ((arc_osk_font[(uint8_t)rendered[character]][row] >>
                    (7u - bit)) & 1u)
                  arc_fill(pixels, width, height, stride, x + bit * scale,
                           y + row * scale, scale, scale, rendered_colour);
   }
}

bool arc_launch_chooser_render(const ArcLaunchChooser *chooser,
                               const ArcLaunchDiscovery *discovery,
                               uint32_t *pixels, unsigned width,
                               unsigned height, size_t pitch)
{
   if (!chooser || !discovery || !pixels || width != 640u || height != 480u ||
       pitch < (size_t)width * sizeof(*pixels) ||
       pitch % sizeof(*pixels)) return false;
   size_t stride = pitch / sizeof(*pixels);
   arc_fill(pixels, width, height, stride, 0u, 0u, width, height,
            UINT32_C(0x00101a2a));
   (void)arc_text(pixels, width, height, stride, 64u, 28u,
                  chooser->install_mode ? "Choose game" : "Choose application",
                  UINT32_C(0x00ffffff));
   if (chooser->inspection_incomplete)
      (void)arc_text(pixels, width, height, stride, 64u, 56u,
                     "Inspection incomplete - verified applications only",
                     UINT32_C(0x00ffd060));
   unsigned visible = arc_chooser_visible_rows(chooser);
   unsigned end = chooser->first_visible + visible;
   if (end > chooser->row_count) end = chooser->row_count;
   for (unsigned row = chooser->first_visible; row < end; ++row)
   {
      unsigned y = ARC_CHOOSER_TOP +
         (row - chooser->first_visible) * ARC_CHOOSER_ROW_HEIGHT;
      bool focused = row == chooser->focus;
      uint32_t foreground = focused ? UINT32_C(0x00101a2a) :
                                      UINT32_C(0x00e8edf5);
      if (focused)
         arc_fill(pixels, width, height, stride, ARC_CHOOSER_LEFT - 8u,
                  y - 2u, ARC_CHOOSER_RIGHT - ARC_CHOOSER_LEFT + 16u,
                  ARC_CHOOSER_ROW_HEIGHT, UINT32_C(0x00a8d8ff));
      char number[8];
      (void)snprintf(number, sizeof(number), "%u.", row + 1u);
      unsigned x = arc_text(pixels, width, height, stride,
                            ARC_CHOOSER_LEFT, y, number, foreground);
      x += 8u;
      uint8_t candidate = chooser->candidate_index[row];
      if (chooser->install_mode)
         (void)arc_text(pixels, width, height, stride, x, y,
                        chooser->install_labels[candidate], foreground);
      else if (candidate == ARC_CHOOSER_DESKTOP)
         (void)arc_text(pixels, width, height, stride, x, y,
                        "Desktop (no launch)", foreground);
      else
      {
         const ArcLaunchCandidate *entry = &discovery->candidates[candidate];
         const ArcGuestName *name = &entry->name;
         unsigned characters = arc_name_characters(name) +
            (entry->parent.length ? arc_name_characters(&entry->parent) + 1u : 0u);
         unsigned scale = characters * 16u <=
            ARC_CHOOSER_RIGHT - x ? 2u : 1u;
         if (entry->parent.length)
         {
            arc_name(pixels, width, height, stride, x, y,
                     &entry->parent, scale, foreground);
            x += arc_name_characters(&entry->parent) * 8u * scale;
            (void)arc_text(pixels, width, height, stride, x, y, ".",
                           foreground);
            x += 16u;
         }
         arc_name(pixels, width, height, stride, x, y, name, scale, foreground);
      }
   }
   if (chooser->first_visible)
      (void)arc_text(pixels, width, height, stride, 584u, 92u, "^",
                     UINT32_C(0x00ffffff));
   if (end < chooser->row_count)
      (void)arc_text(pixels, width, height, stride, 584u,
                     chooser->nested_scope ? 372u : 432u, "v",
                     UINT32_C(0x00ffffff));
   if (chooser->nested_scope && !chooser->install_mode &&
       chooser->focus < chooser->row_count)
   {
      uint8_t selected = chooser->candidate_index[chooser->focus];
      if (selected != ARC_CHOOSER_DESKTOP)
      {
         const ArcLaunchCandidate *entry = &discovery->candidates[selected];
         (void)arc_text(pixels, width, height, stride, 64u, 404u,
                        "Selected:", UINT32_C(0x00ffffff));
         if (entry->parent.length)
         {
            arc_name(pixels, width, height, stride, 64u, 424u,
                     &entry->parent, 1u, UINT32_C(0x00ffd060));
            (void)arc_text(pixels, width, height, stride,
                           64u + arc_name_characters(&entry->parent) * 8u,
                           424u, ".", UINT32_C(0x00ffd060));
            arc_name(pixels, width, height, stride, 64u, 444u,
                     &entry->name, 1u, UINT32_C(0x00ffd060));
         }
         else arc_name(pixels, width, height, stride, 64u, 424u,
                       &entry->name, 1u, UINT32_C(0x00ffd060));
      }
   }
   return true;
}
