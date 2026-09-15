/* ArcDroid evidence-backed input profile table. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <string.h>

/* Real-title entries are added only with locally verified D-029 evidence. */
static const ArcInputProfile arc_profiles[] = {{0}};

const ArcInputProfile *arc_input_profiles(size_t *count)
{
   if (count) *count = 0u;
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
