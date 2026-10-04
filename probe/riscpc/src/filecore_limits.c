/* ArcDroid launch planning. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
/* ARCDROID-PROBE BEGIN: verbatim pure predicate copied from core/launch.c
 * at fe75401; whole source SHA256 a5ec118f842047327bb9f746c83c5e01c708248c3b31dfd0466af1d7e8851888. */
#include "arcdroid_core.h"
bool arc_filecore_e_map_layout_valid(unsigned id_length,
                                     unsigned log2_bytes_per_map_bit,
                                     unsigned zone_spare,
                                     size_t disc_size)
{
   const unsigned sector_bits = 1024u * 8u;
   if (id_length < 13u || id_length > 15u ||
       log2_bytes_per_map_bit < 7u || log2_bytes_per_map_bit > 10u ||
       zone_spare > sector_bits - 480u)
      return false;
   uint64_t allocation_bits = sector_bits - zone_spare - 480u;
   uint64_t required_bits = (uint64_t)disc_size >> log2_bytes_per_map_bit;
   uint64_t ids_per_zone = (sector_bits - zone_spare) / (id_length + 1u);
   return allocation_bits >= required_bits &&
          allocation_bits - required_bits < ids_per_zone &&
          ids_per_zone >= 3u && ids_per_zone <= (UINT64_C(1) << 15u);
}

/* ARCDROID-PROBE END */
