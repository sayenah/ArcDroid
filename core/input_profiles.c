/* ArcDroid evidence-backed input profile table. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "libretro.h"

#include <stdio.h>
#include <string.h>

/* Real-title entries are added only with locally verified D-029 evidence. */
static const ArcInputProfile arc_profiles[] = {
   {
      .id = "elite",
      .name = "Elite (Hybrid Technology)",
      .digests = {"fde0f540e0d5732646c1eebb54629a162f40db91"},
      .digest_count = 1u,
      .mapping = {
         [0] = {
            [ARC_PAD_UP] = {true, RETROK_s},
            [ARC_PAD_DOWN] = {true, RETROK_x},
            [ARC_PAD_LEFT] = {true, RETROK_COMMA},
            [ARC_PAD_RIGHT] = {true, RETROK_PERIOD},
            [ARC_PAD_A] = {true, RETROK_SPACE},
            [ARC_PAD_B] = {true, RETROK_SLASH},
            [ARC_PAD_X] = {true, RETROK_a},
            [ARC_PAD_Y] = {true, RETROK_m},
            [ARC_PAD_START] = {true, RETROK_F1},
            [ARC_PAD_SELECT] = {true, RETROK_t}
         }
      },
      .has_port1 = false,
      .evidence = "Elite Flight Training Manual pp. 12–15, 20; Odin3 launch and key response observed by Fable 2026-09-15"
   }
};

const ArcInputProfile *arc_input_profiles(size_t *count)
{
   if (count) *count = sizeof(arc_profiles) / sizeof(arc_profiles[0]);
   return arc_profiles;
}

bool arc_input_profiles_validate_table(const ArcInputProfile *profiles,
                                       size_t count, char *error,
                                       size_t error_size)
{
   if (count && !profiles) goto invalid;
   for (size_t i = 0u; i < count; ++i)
   {
      if (!profiles[i].id || !profiles[i].id[0] || !profiles[i].name ||
          !profiles[i].name[0] || !profiles[i].digest_count ||
          profiles[i].digest_count > ARC_INPUT_PROFILE_MAX_DIGESTS ||
          !profiles[i].evidence || !profiles[i].evidence[0])
         goto invalid;
      for (size_t prior = 0u; prior < i; ++prior)
         if (!strcmp(profiles[i].id, profiles[prior].id)) goto invalid;
      for (unsigned digest = 0u; digest < profiles[i].digest_count; ++digest)
      {
         const char *value = profiles[i].digests[digest];
         if (!value || strlen(value) != 40u) goto invalid;
         for (unsigned character = 0u; character < 40u; ++character)
            if (!((value[character] >= '0' && value[character] <= '9') ||
                  (value[character] >= 'a' && value[character] <= 'f')))
               goto invalid;
         for (size_t prior = 0u; prior <= i; ++prior)
         {
            unsigned limit = prior == i ? digest : profiles[prior].digest_count;
            for (unsigned other = 0u; other < limit; ++other)
               if (!strcmp(value, profiles[prior].digests[other])) goto invalid;
         }
      }
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            if (profiles[i].mapping[port][input].bound &&
                !arc_input_key(profiles[i].mapping[port][input].key))
               goto invalid;
   }
   return true;
invalid:
   if (error && error_size)
      snprintf(error, error_size, "Invalid or duplicate ArcDroid input profile");
   return false;
}

bool arc_input_profiles_validate(char *error, size_t error_size)
{
   size_t count = 0u;
   const ArcInputProfile *profiles = arc_input_profiles(&count);
   return arc_input_profiles_validate_table(profiles, count, error, error_size);
}
