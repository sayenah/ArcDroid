/* ArcDroid remembered boot observation. GPL-2.0-only. */
#include "boot_observation.h"

#include <string.h>

/* Fixed binary grammar: marker, version, compatibility revision, three
 * lowercase SHA-1 hex stamps, six resolved scalars, a SHA-256 hard-disc
 * digest and one packed policy/attachment/writability word. */
static void put32(uint8_t *p, uint32_t n)
{
   for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static uint32_t get32(const uint8_t *p)
{
   uint32_t n = 0;
   for (unsigned i = 0; i < 4; ++i) n |= (uint32_t)p[i] << (8u * i);
   return n;
}
static bool digest_valid(const char *s)
{
   if (s[40] != '\0') return false;
   for (unsigned i = 0; i < 40; ++i)
      if (!((s[i] >= '0' && s[i] <= '9') ||
            (s[i] >= 'a' && s[i] <= 'f'))) return false;
   return true;
}
static bool disc_digest_valid(const char *s)
{
   if (s[64] != '\0') return false;
   for (unsigned i = 0; i < 64; ++i)
      if (!((s[i] >= '0' && s[i] <= '9') ||
            (s[i] >= 'a' && s[i] <= 'f'))) return false;
   return true;
}
bool arc_boot_observation_format(const ArcBootObservation *s,
                                 uint8_t *out, size_t capacity)
{
   if (!s || !out || capacity < ARC_BOOT_OBSERVATION_SIZE ||
       !digest_valid(s->firmware) || !digest_valid(s->media) ||
       !digest_valid(s->cmos) || !disc_digest_valid(s->hard_disc)) return false;
   memcpy(out, "ARCBOOT1", 8);
   put32(out + 8, 1u);
   put32(out + 12, ARC_BOOT_OBSERVATION_REVISION);
   memcpy(out + 16, s->firmware, 40);
   memcpy(out + 56, s->media, 40);
   memcpy(out + 96, s->cmos, 40);
   put32(out + 136, s->machine);
   put32(out + 140, s->cpu);
   put32(out + 144, s->memory);
   put32(out + 148, s->jfd_boot);
   put32(out + 152, s->disk_writes);
   put32(out + 156, s->legacy_slot);
   memcpy(out + 160, s->hard_disc, 64);
   put32(out + 224, s->hard_disc_policy |
      (s->hard_disc_attached << 1u) | (s->hard_disc_writable << 2u));
   return true;
}
bool arc_boot_observation_parse(const uint8_t *data, size_t size,
                                ArcBootObservation *s)
{
   if (!data || !s || size != ARC_BOOT_OBSERVATION_SIZE ||
       memcmp(data, "ARCBOOT1", 8) || get32(data + 8) != 1u ||
       get32(data + 12) != ARC_BOOT_OBSERVATION_REVISION) return false;
   memset(s, 0, sizeof(*s));
   memcpy(s->firmware, data + 16, 40);
   memcpy(s->media, data + 56, 40);
   memcpy(s->cmos, data + 96, 40);
   s->machine = get32(data + 136);
   s->cpu = get32(data + 140);
   s->memory = get32(data + 144);
   s->jfd_boot = get32(data + 148);
   s->disk_writes = get32(data + 152);
   s->legacy_slot = get32(data + 156);
   memcpy(s->hard_disc, data + 160, 64);
   uint32_t disc_flags = get32(data + 224);
   s->hard_disc_policy = disc_flags & 1u;
   s->hard_disc_attached = (disc_flags >> 1u) & 1u;
   s->hard_disc_writable = (disc_flags >> 2u) & 1u;
   return digest_valid(s->firmware) && digest_valid(s->media) &&
      digest_valid(s->cmos) && disc_digest_valid(s->hard_disc) &&
      !(disc_flags & ~7u) &&
      (!s->hard_disc_writable || s->hard_disc_attached) &&
      s->machine < 4u && s->cpu < 2u &&
      s->memory >= 1024u && s->memory <= 65536u &&
      s->memory % 1024u == 0u && s->jfd_boot <= 2u &&
      s->disk_writes <= 2u && s->legacy_slot <= 1u;
}
bool arc_boot_observation_equal(const ArcBootObservation *a,
                                const ArcBootObservation *b)
{
   return a && b && !strcmp(a->firmware, b->firmware) &&
      !strcmp(a->media, b->media) && !strcmp(a->cmos, b->cmos) &&
      a->machine == b->machine && a->cpu == b->cpu &&
      a->memory == b->memory && a->jfd_boot == b->jfd_boot &&
      a->disk_writes == b->disk_writes &&
      !strcmp(a->hard_disc, b->hard_disc) &&
      a->hard_disc_policy == b->hard_disc_policy &&
      a->hard_disc_attached == b->hard_disc_attached &&
      a->hard_disc_writable == b->hard_disc_writable;
}
