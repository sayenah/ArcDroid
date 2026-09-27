/* ArcDroid bounded FileCore launch discovery. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

typedef struct {
   const uint8_t *raw;
   size_t raw_size;
   const ArcJfdImage *jfd;
   ArcMediaGeometry geometry;
   unsigned sector_size;
   unsigned sectors_per_track;
   unsigned density;
   bool base_only;
   bool captured_only; /* Map parsing never consumes virtual fill. */
} ArcDiscReader;

typedef struct {
   ArcDiscReader reader;
   bool new_map;
   bool new_directory;
   size_t disc_size;
   unsigned boot_option;
   ArcGuestName disc_name;
   uint8_t map[8u * 1024u];
   unsigned zone_count;
   bool backup_map_used;
   bool old_map_clamped;
   unsigned id_length;
   unsigned log2_bytes_per_map_bit;
   unsigned zone_spare;
   uint32_t root_address;
} ArcFileCore;

typedef struct {
   ArcGuestName name;
   uint32_t length;
   uint32_t address;
   bool directory;
} ArcDirectoryEntry;

static uint32_t arc_le24(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
          ((uint32_t)p[2] << 16u);
}

static uint32_t arc_le32(const uint8_t *p)
{
   return arc_le24(p) | ((uint32_t)p[3] << 24u);
}

static uint8_t arc_old_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static uint8_t arc_zone_checksum(const uint8_t *map)
{
   unsigned sum[4] = {0u, 0u, 0u, 0u};
   size_t rover = 1020u;
   while (rover > 0u)
   {
      sum[0] += map[rover] + (sum[3] >> 8u); sum[3] &= 0xffu;
      sum[1] += map[rover + 1u] + (sum[0] >> 8u); sum[0] &= 0xffu;
      sum[2] += map[rover + 2u] + (sum[1] >> 8u); sum[1] &= 0xffu;
      sum[3] += map[rover + 3u] + (sum[2] >> 8u); sum[2] &= 0xffu;
      rover -= 4u;
   }
   sum[0] += sum[3] >> 8u;
   sum[1] += map[1] + (sum[0] >> 8u);
   sum[2] += map[2] + (sum[1] >> 8u);
   sum[3] += map[3] + (sum[2] >> 8u);
   return (uint8_t)((sum[0] ^ sum[1] ^ sum[2] ^ sum[3]) & 0xffu);
}

static bool arc_reader_configure(ArcDiscReader *reader,
                                 ArcMediaGeometry geometry)
{
   reader->geometry = geometry;
   switch (geometry)
   {
      case ARC_GEOMETRY_640K_L:
         reader->sector_size = 256u;
         reader->sectors_per_track = 16u;
         reader->density = 2u;
         return true;
      case ARC_GEOMETRY_800K_D:
      case ARC_GEOMETRY_800K_E:
         reader->sector_size = 1024u;
         reader->sectors_per_track = 5u;
         reader->density = 2u;
         return true;
      case ARC_GEOMETRY_1600K_F:
         reader->sector_size = 1024u;
         reader->sectors_per_track = 10u;
         reader->density = 4u;
         return true;
      default:
         return false;
   }
}

static bool arc_reader_sector(const ArcDiscReader *reader,
                              size_t logical_sector, uint8_t *output)
{
   if (!reader || !output || !reader->sector_size) return false;
   if (reader->raw)
   {
      size_t physical_sector = logical_sector;
      if (reader->geometry == ARC_GEOMETRY_640K_L)
      {
         size_t sequential_track = logical_sector / 16u;
         size_t cylinder = sequential_track % 80u;
         size_t side = sequential_track / 80u;
         if (side > 1u) return false;
         physical_sector = (cylinder * 2u + side) * 16u +
                           logical_sector % 16u;
      }
      if (physical_sector > SIZE_MAX / reader->sector_size) return false;
      size_t offset = physical_sector * reader->sector_size;
      if (offset > reader->raw_size ||
          reader->sector_size > reader->raw_size - offset) return false;
      memcpy(output, reader->raw + offset, reader->sector_size);
      return true;
   }

   if (!reader->jfd) return false;
   size_t track_side;
   unsigned sector_id;
   if (reader->geometry == ARC_GEOMETRY_640K_L)
   {
      size_t sequential_track = logical_sector / 16u;
      size_t cylinder = sequential_track % 80u;
      size_t side = sequential_track / 80u;
      if (side > 1u) return false;
      track_side = cylinder * 2u + side;
      sector_id = (unsigned)(logical_sector % 16u);
   }
   else
   {
      size_t interleaved_track = logical_sector / reader->sectors_per_track;
      track_side = interleaved_track;
      sector_id = (unsigned)(logical_sector % reader->sectors_per_track);
   }
   if (track_side >= reader->jfd->track_count) return false;
   const ArcJfdTrack *track = &reader->jfd->tracks[track_side];
   ArcJfdView match = {0};
   bool found = false;
   unsigned limit = track->sector_count +
      (reader->base_only || reader->captured_only ? 0u : ARC_JFD_PDD5_IDS);
   for (unsigned i = 0u; i < limit; ++i)
   {
      ArcJfdView view;
      if (!arc_jfd_effective_sector(reader->jfd, (unsigned)track_side, i,
                                     false, &view) ||
          view.sector.id != sector_id ||
          view.sector.density != reader->density ||
          view.sector.size != reader->sector_size) continue;
      if (found) return false;
      match = view;
      found = true;
   }
   if (!found || !match.data ||
       (match.sector.flags & (ARC_JFD_ID_CRC_BAD | ARC_JFD_DATA_CRC_BAD)))
      return false;
   const uint8_t *bytes = match.data;
   if (reader->base_only)
   {
      if (match.created || match.sector.data_offset == UINT32_MAX ||
          match.sector.data_offset > reader->jfd->decoded_size ||
          match.sector.size > reader->jfd->decoded_size -
                              match.sector.data_offset) return false;
      bytes = reader->jfd->decoded + match.sector.data_offset;
   }
   memcpy(output, bytes, reader->sector_size);
   return true;
}

static bool arc_reader_read(const ArcDiscReader *reader, size_t offset,
                            void *output, size_t size)
{
   uint8_t sector[1024];
   uint8_t *out = output;
   while (size)
   {
      size_t sector_number = offset / reader->sector_size;
      size_t within = offset % reader->sector_size;
      size_t amount = reader->sector_size - within;
      if (amount > size) amount = size;
      if (!arc_reader_sector(reader, sector_number, sector)) return false;
      memcpy(out, sector + within, amount);
      out += amount;
      offset += amount;
      size -= amount;
   }
   return true;
}

static void arc_guest_name_trim(ArcGuestName *name, const uint8_t source[10])
{
   unsigned length = 10u;
   memcpy(name->bytes, source, 10u);
   while (length && source[length - 1u] <= 0x20u) --length;
   name->length = (uint8_t)length;
}

static bool arc_filecore_old(ArcFileCore *fs, ArcMediaGeometry geometry)
{
   if (!arc_reader_configure(&fs->reader, geometry) ||
       !arc_reader_read(&fs->reader, 0u, fs->map, 512u) ||
       fs->map[255] != arc_old_checksum(fs->map) ||
       fs->map[511] != arc_old_checksum(fs->map + 256u) ||
       fs->map[510] > 246u || fs->map[510] % 3u ||
       fs->map[509] > 3u) return false;
   uint32_t sectors = arc_le24(fs->map + 252u);
   uint32_t expected = geometry == ARC_GEOMETRY_640K_L ? 2560u : 3200u;
   uint32_t minimum = geometry == ARC_GEOMETRY_640K_L ? 7u : 12u;
   if (sectors < minimum) return false;
   if (sectors > expected)
   {
      fs->old_map_clamped = true;
      sectors = expected;
   }
   fs->new_map = false;
   fs->new_directory = geometry != ARC_GEOMETRY_640K_L;
   fs->disc_size = (size_t)sectors * 256u;
   fs->boot_option = fs->map[509];
   uint8_t disc_name[10];
   for (unsigned i = 0u; i < 5u; ++i)
   {
      disc_name[i * 2u] = fs->map[247u + i];
      disc_name[i * 2u + 1u] = fs->map[502u + i];
   }
   arc_guest_name_trim(&fs->disc_name, disc_name);
   fs->root_address = (uint32_t)(geometry == ARC_GEOMETRY_640K_L ? 512u : 1024u);
   return true;
}

static bool arc_filecore_new_at(ArcFileCore *fs, unsigned copy)
{
   if (!arc_reader_configure(&fs->reader, ARC_GEOMETRY_800K_E) ||
       !arc_reader_read(&fs->reader, (size_t)copy * 1024u,
                        fs->map, 1024u)) return false;
   const uint8_t *record = fs->map + 4u;
   if (fs->map[0] != arc_zone_checksum(fs->map) || fs->map[3] != 0xffu ||
       record[0] != 10u || record[1] != 5u || record[2] != 2u ||
       record[3] != 2u || record[7] > 3u || record[8] != 0u ||
       record[9] != 1u || arc_le32(record + 16u) != 819200u)
      return false;
   fs->id_length = record[4];
   fs->log2_bytes_per_map_bit = record[5];
   fs->zone_spare = (unsigned)record[10] | ((unsigned)record[11] << 8u);
   if (fs->id_length < 13u || fs->id_length > 15u ||
       fs->log2_bytes_per_map_bit < 7u ||
       fs->log2_bytes_per_map_bit > 10u || fs->zone_spare > 7712u)
      return false;
   if (!arc_filecore_e_map_layout_valid(fs->id_length,
                                        fs->log2_bytes_per_map_bit,
                                        fs->zone_spare, 819200u)) return false;
   for (unsigned i = 36u; i < 60u; ++i)
      if (record[i]) return false;
   fs->new_map = true;
   fs->new_directory = true;
   fs->disc_size = 819200u;
   fs->boot_option = record[7];
   fs->root_address = arc_le32(record + 12u) & 0x00ffffffu;
   arc_guest_name_trim(&fs->disc_name, record + 22u);
   return true;
}

static bool arc_filecore_new(ArcFileCore *fs)
{
   /* Launch discovery retains its primary-copy policy. C-11c's independent
    * backup parse is confined to the allocation view below. */
   return arc_filecore_new_at(fs, 0u);
}

static bool arc_f_record_valid(const uint8_t record[60])
{
   unsigned id_length = record[4];
   unsigned log2_bytes_per_map_bit = record[5];
   unsigned zone_count = record[9];
   unsigned zone_spare = (unsigned)record[10] | ((unsigned)record[11] << 8u);
   if (record[0] != 10u || record[1] != 10u || record[2] != 2u ||
       record[3] != 4u || record[7] > 3u ||
       arc_le32(record + 16u) != 1638400u ||
       id_length < 13u || id_length > 15u ||
       log2_bytes_per_map_bit < 6u || log2_bytes_per_map_bit > 10u ||
       zone_count < 1u || zone_count > 8u ||
       zone_spare < 32u ||
       zone_spare > 8192u - 480u - (id_length + 1u)) return false;
   unsigned zone_bits = 8192u - zone_spare;
   size_t disc_bits = 1638400u >> log2_bytes_per_map_bit;
   size_t preceding_bits = (size_t)(zone_count - 1u) * zone_bits;
   if (preceding_bits > disc_bits + 480u) return false;
   size_t last_bits = disc_bits + 480u - preceding_bits;
   unsigned last_start = zone_count == 1u ? 480u : 0u;
   if (last_bits < last_start + id_length + 1u ||
       last_bits > zone_bits) return false;
   unsigned middle = zone_count / 2u;
   size_t map_bits = middle ? (size_t)middle * zone_bits - 480u : 0u;
   size_t map_address = map_bits << log2_bytes_per_map_bit;
   size_t map_size = (size_t)zone_count * 1024u;
   return map_address <= 1638400u &&
          2u * map_size <= 1638400u - map_address;
}

static unsigned arc_f_zone_end(const ArcFileCore *fs, unsigned zone)
{
   unsigned zone_bits = 8192u - fs->zone_spare;
   if (zone + 1u != fs->zone_count) return 32u + zone_bits;
   size_t disc_bits = fs->disc_size >> fs->log2_bytes_per_map_bit;
   return 32u + (unsigned)(disc_bits + 480u -
      (size_t)(fs->zone_count - 1u) * zone_bits);
}

static bool arc_f_map_valid(const ArcFileCore *fs, const uint8_t record[60])
{
   uint8_t cross = 0u;
   for (unsigned zone = 0u; zone < fs->zone_count; ++zone)
   {
      const uint8_t *map = fs->map + (size_t)zone * 1024u;
      unsigned start = zone ? 32u : 512u;
      unsigned end = arc_f_zone_end(fs, zone);
      if (map[0] != arc_zone_checksum(map)) return false;
      cross ^= map[3];
      if (zone == 0u && (!arc_f_record_valid(map + 4u) ||
                         memcmp(map + 4u, record, 20u))) return false;
      unsigned link = 0u;
      for (unsigned bit = 0u; bit < fs->id_length; ++bit)
         link |= (unsigned)((map[(8u + bit) >> 3u] >> ((8u + bit) & 7u)) & 1u) << bit;
      unsigned free_at = link ? 8u + link : 0u;
      if (free_at && (free_at < start || free_at >= end)) return false;
      while (start < end)
      {
         if (start + fs->id_length >= end ||
             (free_at && free_at < start)) return false;
         unsigned id = 0u;
         for (unsigned bit = 0u; bit < fs->id_length; ++bit)
            id |= (unsigned)((map[(start + bit) >> 3u] >>
                         ((start + bit) & 7u)) & 1u) << bit;
         unsigned stop = start + fs->id_length;
         while (stop < end && !((map[stop >> 3u] >> (stop & 7u)) & 1u))
            ++stop;
         if (stop >= end) return false;
         if (free_at == start)
         {
            free_at = id ? start + id : 0u;
            if (free_at && free_at <= stop) return false;
         }
         start = stop + 1u;
      }
      if (free_at) return false;
   }
   return cross == 0xffu;
}

static bool arc_filecore_f(ArcFileCore *fs)
{
   uint8_t boot[60];
   if (!arc_reader_configure(&fs->reader, ARC_GEOMETRY_1600K_F) ||
       !arc_reader_read(&fs->reader, 0xdc0u, boot, sizeof(boot)) ||
       !arc_f_record_valid(boot)) return false;
   fs->id_length = boot[4];
   fs->log2_bytes_per_map_bit = boot[5];
   fs->zone_count = boot[9];
   fs->zone_spare = (unsigned)boot[10] | ((unsigned)boot[11] << 8u);
   fs->disc_size = 1638400u;
   unsigned zone_bits = 8192u - fs->zone_spare;
   unsigned middle = fs->zone_count / 2u;
   size_t preceding_bits = middle ? (size_t)middle * zone_bits - 480u : 0u;
   size_t map_address = preceding_bits << fs->log2_bytes_per_map_bit;
   size_t map_size = (size_t)fs->zone_count * 1024u;
   bool primary_read = arc_reader_read(&fs->reader, map_address,
                                        fs->map, map_size);
   if (!primary_read || !arc_f_map_valid(fs, boot))
   {
      if (!arc_reader_read(&fs->reader, map_address + map_size,
                           fs->map, map_size) ||
          !arc_f_map_valid(fs, boot)) return false;
      fs->backup_map_used = true;
   }
   fs->new_map = true;
   fs->new_directory = true;
   const uint8_t *map_record = fs->map + 4u;
   fs->boot_option = map_record[7];
   fs->root_address = arc_le32(map_record + 12u) & 0x00ffffffu;
   arc_guest_name_trim(&fs->disc_name, map_record + 22u);
   return true;
}

static bool arc_filecore_init(ArcFileCore *fs, const uint8_t *data, size_t size,
                              const ArcMediaInfo *media,
                              const ArcJfdImage *jfd)
{
   memset(fs, 0, sizeof(*fs));
   fs->reader.raw = data;
   fs->reader.raw_size = size;
   fs->reader.jfd = jfd;
   if (jfd)
   {
      if (arc_filecore_old(fs, ARC_GEOMETRY_640K_L)) return true;
      memset(fs->map, 0, sizeof(fs->map));
      if (arc_filecore_old(fs, ARC_GEOMETRY_800K_D)) return true;
      memset(fs->map, 0, sizeof(fs->map));
      if (arc_filecore_new(fs)) return true;
      return arc_filecore_f(fs);
   }
   if (!data || !media) return false;
   if (media->geometry == ARC_GEOMETRY_640K_L ||
       media->geometry == ARC_GEOMETRY_800K_D)
      return arc_filecore_old(fs, media->geometry);
   if (media->geometry == ARC_GEOMETRY_800K_E)
      return arc_filecore_new(fs);
   if (media->geometry == ARC_GEOMETRY_1600K_F)
      return arc_filecore_f(fs);
   return false;
}

static uint32_t arc_map_bits(const uint8_t *map, unsigned bit,
                             unsigned count)
{
   uint32_t value = 0u;
   for (unsigned i = 0u; i < count; ++i)
      value |= (uint32_t)((map[(bit + i) >> 3u] >> ((bit + i) & 7u)) & 1u) << i;
   return value;
}

static void arc_mark_free_tracks(bool free_track[ARC_JFD_PDD5_TRACKS],
                                 size_t first, size_t length)
{
   size_t last = first + length;
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      if (first < ((size_t)t + 1u) * 5120u &&
          (size_t)t * 5120u < last) free_track[t] = true;
}

static bool arc_jfd_new_free_copy(ArcFileCore *fs, unsigned copy,
                                  bool free_track[ARC_JFD_PDD5_TRACKS])
{
   if (!arc_filecore_new_at(fs, copy)) return false;
   unsigned start = 512u;
   unsigned end = 512u + (8192u - fs->zone_spare - 480u);
   unsigned link = arc_map_bits(fs->map, 8u, fs->id_length) & 0x7fffu;
   unsigned free_at = link ? 8u + link : 0u;
   if (free_at && (free_at < start || free_at >= end)) return false;
   while (start < end)
   {
      if (start + fs->id_length >= end || (free_at && free_at < start))
         return false;
      unsigned id = arc_map_bits(fs->map, start, fs->id_length);
      unsigned stop = start + fs->id_length;
      while (stop < end && !((fs->map[stop >> 3u] >> (stop & 7u)) & 1u))
         ++stop;
      if (stop >= end) return false;
      size_t length = (size_t)stop + 1u - start;
      if (start == free_at)
      {
         size_t first = ((size_t)start - 512u) <<
                         fs->log2_bytes_per_map_bit;
         size_t bytes = length << fs->log2_bytes_per_map_bit;
         if (first > fs->disc_size || bytes > fs->disc_size - first)
            return false;
         arc_mark_free_tracks(free_track, first, bytes);
         free_at = id ? free_at + (id & 0x7fffu) : 0u;
         if (free_at && free_at <= stop) return false;
      }
      start = stop + 1u;
   }
   return !free_at;
}

bool arc_jfd_base_free_tracks(const ArcJfdImage *image,
                              bool free_track[ARC_JFD_PDD5_TRACKS])
{
   if (!image || !free_track) return false;
   memset(free_track, 0, sizeof(bool) * ARC_JFD_PDD5_TRACKS);
   ArcFileCore fs = {0};
   fs.reader.jfd = image;
   fs.reader.base_only = true;
   if (arc_filecore_old(&fs, ARC_GEOMETRY_800K_D))
   {
      unsigned previous_end = 0u;
      unsigned total = (unsigned)(fs.disc_size / 256u);
      for (unsigned at = 0u; at < fs.map[510]; at += 3u)
      {
         unsigned first = arc_le24(fs.map + at);
         unsigned length = arc_le24(fs.map + 256u + at);
         if (!length || first < previous_end || first > total ||
             length > total - first) return false;
         arc_mark_free_tracks(free_track, (size_t)first * 256u,
                              (size_t)length * 256u);
         previous_end = first + length;
      }
      return true;
   }
   memset(fs.map, 0, sizeof(fs.map));
   return arc_jfd_new_free_copy(&fs, 0u, free_track);
}

/* C-11c uses FileCore's bounded map checks, but only captured sectors and
 * committed replacements may supply a map. A virtual zero sector is never
 * input to this parser. The 160 byte result is an in-memory view, not a store
 * format field. */
static void arc_map_mark_bytes(uint8_t mask[ARC_JFD_PDD5_TRACKS],
                               size_t first, size_t length)
{
   if (!length) return;
   size_t last = first + length - 1u;
   size_t start_slot = first / 1024u, end_slot = last / 1024u;
   if (end_slot >= ARC_JFD_PDD5_SLOTS) end_slot = ARC_JFD_PDD5_SLOTS - 1u;
   for (size_t slot = start_slot; slot <= end_slot; ++slot)
      mask[slot / ARC_JFD_PDD5_IDS] |=
         (uint8_t)(1u << (slot % ARC_JFD_PDD5_IDS));
}

static bool arc_jfd_old_allocation(const uint8_t map[1024],
                                   uint8_t mask[ARC_JFD_PDD5_TRACKS])
{
   ArcFileCore fs = {0};
   fs.reader.raw = map; fs.reader.raw_size = 1024u;
   if (!arc_filecore_old(&fs, ARC_GEOMETRY_800K_D)) return false;
   uint16_t free_bytes[ARC_JFD_PDD5_SLOTS] = {0};
   unsigned previous_end = 0u;
   unsigned total = (unsigned)(fs.disc_size / 256u);
   for (unsigned at = 0u; at < fs.map[510]; at += 3u)
   {
      unsigned first = arc_le24(fs.map + at);
      unsigned count = arc_le24(fs.map + 256u + at);
      if (!count || first < previous_end || first > total ||
          count > total - first) return false;
      size_t begin = (size_t)first * 256u;
      size_t end = begin + (size_t)count * 256u;
      for (size_t slot = begin / 1024u; slot < (end + 1023u) / 1024u &&
           slot < ARC_JFD_PDD5_SLOTS; ++slot)
      {
         size_t lo = slot * 1024u > begin ? slot * 1024u : begin;
         size_t hi = (slot + 1u) * 1024u < end ?
                     (slot + 1u) * 1024u : end;
         if (hi > lo) free_bytes[slot] += (uint16_t)(hi - lo);
      }
      previous_end = first + count;
   }
   memset(mask, 0, ARC_JFD_PDD5_TRACKS);
   for (size_t slot = 0u; slot < ARC_JFD_PDD5_SLOTS; ++slot)
   {
      size_t begin = slot * 1024u;
      size_t in_disc = begin < fs.disc_size ? fs.disc_size - begin : 0u;
      if (in_disc > 1024u) in_disc = 1024u;
      if (in_disc > free_bytes[slot])
         mask[slot / ARC_JFD_PDD5_IDS] |=
            (uint8_t)(1u << (slot % ARC_JFD_PDD5_IDS));
   }
   return true;
}

static bool arc_jfd_new_allocation(const uint8_t map[1024],
                                   const uint8_t *base_record,
                                   uint8_t mask[ARC_JFD_PDD5_TRACKS])
{
   ArcFileCore fs = {0};
   fs.reader.raw = map; fs.reader.raw_size = 1024u;
   /* The boot option at byte 7 can change and is range-checked by
    * arc_filecore_new. All other layout bytes 0–19 must match the base;
    * disc ID and name after byte 19 can change independently. */
   if (!arc_filecore_new(&fs) ||
       (base_record &&
        (memcmp(map + 4u, base_record, 7u) ||
         memcmp(map + 12u, base_record + 8u, 12u)))) return false;
   unsigned start = 512u;
   unsigned end = 512u + 8192u - fs.zone_spare - 480u;
   unsigned disc_bits = (unsigned)(fs.disc_size >> fs.log2_bytes_per_map_bit);
   unsigned link = arc_map_bits(map, 8u, fs.id_length) & 0x7fffu;
   unsigned free_at = link ? 8u + link : 0u;
   if (free_at && (free_at < start || free_at >= end)) return false;
   memset(mask, 0, ARC_JFD_PDD5_TRACKS);
   while (start < end)
   {
      if (start + fs.id_length >= end || (free_at && free_at < start))
         return false;
      unsigned id = arc_map_bits(map, start, fs.id_length);
      unsigned stop = start + fs.id_length;
      while (stop < end && !((map[stop >> 3u] >> (stop & 7u)) & 1u)) ++stop;
      if (stop >= end) return false;
      unsigned count = stop + 1u - start;
      if (start == free_at)
      {
         free_at = id ? free_at + (id & 0x7fffu) : 0u;
         if (free_at && free_at <= stop) return false;
      }
      else if (id >= 2u)
      {
         unsigned first_bit = start - 512u;
         if (first_bit >= disc_bits || count > disc_bits - first_bit)
            return false;
         arc_map_mark_bytes(mask,
            (size_t)first_bit << fs.log2_bytes_per_map_bit,
            (size_t)count << fs.log2_bytes_per_map_bit);
      }
      /* ID 1 is a defect or beyond-disc extent, never an object. */
      start = stop + 1u;
   }
   return !free_at;
}

static int arc_jfd_map_copy_index(const ArcJfdImage *image, size_t index)
{
   if (!image || !image->track_count) return -1;
   const ArcJfdTrack *track = &image->tracks[0];
   if (index < track->first_sector ||
       index >= (size_t)track->first_sector + track->sector_count) return -1;
   const ArcJfdSector *sector = &image->sectors[index];
   return sector->density == 2u && sector->size == 1024u &&
          sector->id <= 1u ? sector->id : -1;
}

bool arc_jfd_map_target(const ArcJfdImage *image, size_t index)
{
   int copy = arc_jfd_map_copy_index(image, index);
   return image && image->overlay && image->overlay->map_kind &&
      (copy == 0 || (copy == 1 && image->overlay->map_kind == 2u));
}

static bool arc_jfd_map_read(const ArcJfdImage *image, unsigned copy,
                             bool base_only, size_t pending_index,
                             const uint8_t *pending, uint8_t map[1024])
{
   ArcDiscReader reader = {0};
   reader.jfd = image;
   reader.base_only = base_only;
   reader.captured_only = true;
   if (!arc_reader_configure(&reader, ARC_GEOMETRY_800K_E)) return false;
   if (pending && arc_jfd_map_copy_index(image, pending_index) == (int)copy)
   { memcpy(map, pending, 1024u); return true; }
   return arc_reader_sector(&reader, copy, map);
}

static bool arc_jfd_map_collect(const ArcJfdImage *image, bool base_only,
                                size_t pending_index, const uint8_t *pending,
                                uint8_t mask[ARC_JFD_PDD5_TRACKS])
{
   const ArcJfdOverlay *overlay = image->overlay;
   uint8_t map[1024], copy_mask[ARC_JFD_PDD5_TRACKS];
   bool valid = false;
   memset(mask, 0, ARC_JFD_PDD5_TRACKS);
   unsigned copies = overlay->map_kind == 2u ? 2u : 1u;
   for (unsigned copy = 0u; copy < copies; ++copy)
   {
      if (!arc_jfd_map_read(image, copy, base_only, pending_index,
                            pending, map)) continue;
      bool okay = overlay->map_kind == 1u ?
         arc_jfd_old_allocation(map, copy_mask) :
         arc_jfd_new_allocation(map, overlay->map_record, copy_mask);
      if (!okay) continue;
      valid = true;
      for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
         mask[t] |= copy_mask[t];
   }
   return valid;
}

void arc_jfd_map_refresh(ArcJfdImage *image)
{
   if (!image || !image->overlay) return;
   ArcJfdOverlay *overlay = image->overlay;
   memset(overlay->allocation_mask, 0, sizeof(overlay->allocation_mask));
   overlay->map_valid = false;
   if (overlay->map_enabled && overlay->map_kind)
      overlay->map_valid = arc_jfd_map_collect(image, false, SIZE_MAX, NULL,
                                               overlay->allocation_mask);
   if (overlay->map_enabled && !overlay->map_valid &&
       !overlay->map_unavailable_logged)
   {
      overlay->map_unavailable_logged = true;
      if (overlay->log_map_unavailable)
         overlay->log_map_unavailable(overlay->image_number);
   }
}

void arc_jfd_map_prepare(ArcJfdImage *image)
{
   if (!image || !image->overlay) return;
   ArcJfdOverlay *overlay = image->overlay;
   overlay->map_kind = 0u;
   memset(overlay->map_record, 0, sizeof(overlay->map_record));
   if (overlay->map_enabled)
   {
      uint8_t map[1024], ignored[ARC_JFD_PDD5_TRACKS];
      if (arc_jfd_map_read(image, 0u, true, SIZE_MAX, NULL, map) &&
          arc_jfd_old_allocation(map, ignored)) overlay->map_kind = 1u;
      else for (unsigned copy = 0u; copy < 2u; ++copy)
         if (arc_jfd_map_read(image, copy, true, SIZE_MAX, NULL, map) &&
             arc_jfd_new_allocation(map, NULL, ignored))
         {
            overlay->map_kind = 2u;
            memcpy(overlay->map_record, map + 4u, 60u);
            break;
         }
   }
   arc_jfd_map_refresh(image);
}

bool arc_jfd_map_write_veto(const ArcJfdImage *image, size_t index,
                            const uint8_t *pending)
{
   if (!image || !image->overlay || !image->overlay->map_enabled ||
       !image->overlay->map_valid ||
       !arc_jfd_map_target(image, index) || !pending) return false;
   const ArcJfdOverlay *overlay = image->overlay;
   bool marked = false;
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      marked |= overlay->probe_mask[t] != 0u;
   if (!marked) return false;
   uint8_t candidate[ARC_JFD_PDD5_TRACKS];
   if (!arc_jfd_map_collect(image, false, index, pending, candidate))
      return false;
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      if (candidate[t] & overlay->probe_mask[t]) return true;
   return false;
}

static bool arc_f_map_resolve(const ArcFileCore *fs, uint32_t address,
                              size_t object_sector, size_t *disc_offset)
{
   unsigned fragment = (address >> 8u) & 0x7fffu;
   unsigned shared = address & 0xffu;
   if ((address & 0xe0000000u) || fragment < 2u) return false;
   size_t sector_offset = object_sector + (shared ? shared - 1u : 0u);
   unsigned bits_per_sector = 1u << (10u - fs->log2_bytes_per_map_bit);
   if (sector_offset > SIZE_MAX / bits_per_sector) return false;
   size_t wanted = sector_offset * bits_per_sector;
   unsigned zone_bits = 8192u - fs->zone_spare;
   unsigned ids_per_zone = zone_bits / (fs->id_length + 1u);
   if (!ids_per_zone || fragment >= ids_per_zone * fs->zone_count) return false;
   unsigned first_zone = fragment == 2u ? fs->zone_count / 2u :
                         fragment / ids_per_zone;
   for (unsigned step = 0u; step < fs->zone_count; ++step)
   {
      unsigned zone = (first_zone + step) % fs->zone_count;
      const uint8_t *map = fs->map + (size_t)zone * 1024u;
      unsigned start = zone ? 32u : 512u;
      unsigned end = arc_f_zone_end(fs, zone);
      unsigned link = arc_map_bits(map, 8u, fs->id_length);
      unsigned free_at = link ? 8u + link : 0u;
      size_t zone_base = zone ?
         (size_t)zone_bits - 480u + (size_t)(zone - 1u) * zone_bits : 0u;
      while (start < end)
      {
         if (start + fs->id_length >= end) return false;
         unsigned id = arc_map_bits(map, start, fs->id_length);
         unsigned stop = start + fs->id_length;
         while (stop < end && !((map[stop >> 3u] >> (stop & 7u)) & 1u))
            ++stop;
         if (stop >= end) return false;
         size_t length = (size_t)stop + 1u - start;
         if (start == free_at)
            free_at = id ? free_at + id : 0u;
         else if (id == fragment)
         {
            size_t allocation_bit = zone_base + (size_t)start -
               (zone ? 32u : 512u);
            size_t disc_bits = fs->disc_size >> fs->log2_bytes_per_map_bit;
            if (allocation_bit >= disc_bits) return false;
            if (length > disc_bits - allocation_bit)
               length = disc_bits - allocation_bit;
            if (wanted < length)
            {
               size_t offset = (allocation_bit + wanted) <<
                  fs->log2_bytes_per_map_bit;
               if (offset > fs->disc_size ||
                   1024u > fs->disc_size - offset) return false;
               *disc_offset = offset;
               return true;
            }
            wanted -= length;
         }
         start = stop + 1u;
      }
   }
   return false;
}

static bool arc_new_map_resolve(const ArcFileCore *fs, uint32_t address,
                                size_t object_sector, size_t *disc_offset)
{
   if (fs->reader.geometry == ARC_GEOMETRY_1600K_F)
      return arc_f_map_resolve(fs, address, object_sector, disc_offset);
   unsigned fragment = (address >> 8u) & 0x7fffu;
   unsigned shared = address & 0xffu;
   if ((address & 0xe0000000u) || fragment < 2u) return false;
   size_t sector_offset = object_sector + (shared ? shared - 1u : 0u);
   unsigned map_bits_per_sector = 1u << (10u - fs->log2_bytes_per_map_bit);
   if (sector_offset > SIZE_MAX / map_bits_per_sector) return false;
   size_t wanted = sector_offset * map_bits_per_sector;
   unsigned start = 512u;
   if (!arc_filecore_e_map_layout_valid(fs->id_length,
                                        fs->log2_bytes_per_map_bit,
                                        fs->zone_spare, fs->disc_size))
      return false;
   unsigned allocation_bits = 8192u - fs->zone_spare - 480u;
   unsigned end = start + allocation_bits;
   if (end > 8192u || start >= end) return false;
   unsigned free_link = arc_map_bits(fs->map, 8u, fs->id_length) & 0x7fffu;
   unsigned free_at = free_link ? 8u + free_link : 0u;
   while (start < end)
   {
      if (start + fs->id_length >= end) return false;
      unsigned id = arc_map_bits(fs->map, start, fs->id_length);
      unsigned stop = start + fs->id_length;
      while (stop < end && !((fs->map[stop >> 3u] >> (stop & 7u)) & 1u)) ++stop;
      if (stop >= end) return false;
      size_t length = (size_t)stop + 1u - start;
      if (start == free_at)
         free_at = id ? free_at + (id & 0x7fffu) : 0u;
      else if (id == fragment)
      {
         if (wanted < length)
         {
            size_t allocation_bit = (size_t)start - 512u + wanted;
            if (allocation_bit > SIZE_MAX >> fs->log2_bytes_per_map_bit) return false;
            size_t offset = allocation_bit << fs->log2_bytes_per_map_bit;
            if (offset >= fs->disc_size) return false;
            *disc_offset = offset;
            return true;
         }
         wanted -= length;
      }
      start = stop + 1u;
   }
   return false;
}

static bool arc_object_read(const ArcFileCore *fs, uint32_t address,
                            size_t offset, void *output, size_t size)
{
   uint8_t *out = output;
   if (!fs->new_map)
   {
      size_t base = (size_t)address * 256u;
      if (base > fs->disc_size || offset > fs->disc_size - base ||
          size > fs->disc_size - base - offset) return false;
      return arc_reader_read(&fs->reader, base + offset, output, size);
   }
   while (size)
   {
      size_t object_sector = offset / 1024u;
      size_t within = offset % 1024u;
      size_t amount = 1024u - within;
      size_t disc_offset;
      if (amount > size) amount = size;
      if (!arc_new_map_resolve(fs, address, object_sector, &disc_offset) ||
          !arc_reader_read(&fs->reader, disc_offset + within, out, amount))
         return false;
      out += amount;
      offset += amount;
      size -= amount;
   }
   return true;
}

static bool arc_entry_name(const uint8_t *source, bool old_directory,
                           ArcGuestName *output, unsigned *attributes)
{
   unsigned attr = 0u, length = 0u;
   bool terminated = false;
   for (unsigned i = 0u; i < 10u; ++i)
   {
      uint8_t c = source[i];
      if (old_directory)
      {
         if (i < 8u && (c & 0x80u)) attr |= 1u << i;
         c &= 0x7fu;
      }
      if (terminated) continue;
      if (c < 0x20u)
      {
         terminated = true;
         continue;
      }
      output->bytes[length++] = c;
   }
   output->length = (uint8_t)length;
   if (!length) return false;
   if (attributes) *attributes = attr;
   return true;
}

static bool arc_directory_read(const ArcFileCore *fs, uint32_t address,
                               bool root, ArcDirectoryEntry *entries,
                               unsigned *entry_count,
                               bool *ignored_attribute_bits)
{
   bool old = !fs->new_directory;
   size_t directory_size = old ? 1280u : 2048u;
   unsigned capacity = old ? 47u : 77u;
   uint8_t directory[2048];
   if (root)
   {
      if (fs->new_map)
      {
         if (!arc_object_read(fs, fs->root_address, 0u,
                              directory, directory_size)) return false;
      }
      else if (!arc_reader_read(&fs->reader, fs->root_address,
                                directory, directory_size)) return false;
   }
   else if (!arc_object_read(fs, address, 0u, directory, directory_size))
      return false;
   const uint8_t *marker = directory + 1u;
   size_t tail = old ? 1227u : 2007u;
   size_t end_sequence = tail + (old ? 47u : 35u);
   if ((old ? memcmp(marker, "Hugo", 4u) != 0 :
              (memcmp(marker, "Hugo", 4u) && memcmp(marker, "Nick", 4u))) ||
       directory[tail] != 0u || directory[0] != directory[end_sequence] ||
       memcmp(marker, directory + end_sequence + 1u, 4u)) return false;
   if (!old && (directory[tail + 1u] || directory[tail + 2u])) return false;

   unsigned count = 0u;
   for (unsigned i = 0u; i < capacity; ++i)
   {
      const uint8_t *raw = directory + 5u + i * 26u;
      if (!(raw[0] & (old ? 0x7fu : 0xffu))) break;
      ArcDirectoryEntry *entry = &entries[count];
      unsigned attributes = 0u;
      memset(entry, 0, sizeof(*entry));
      if (!arc_entry_name(raw, old, &entry->name, &attributes)) continue;
      entry->length = arc_le32(raw + 18u);
      entry->address = arc_le24(raw + 22u);
      entry->directory = old ? (attributes & 8u) != 0u :
                               (raw[25] & 8u) != 0u;
      if ((old ? attributes : raw[25]) & 0xc0u)
      {
         if (ignored_attribute_bits) *ignored_attribute_bits = true;
      }
      ++count;
   }
   *entry_count = count;
   return true;
}

static uint8_t arc_ascii_fold(uint8_t c)
{
   return c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + 'a') : c;
}

static bool arc_name_ascii_equal(const ArcGuestName *a, const ArcGuestName *b)
{
   if (!a || !b || a->length != b->length) return false;
   for (unsigned i = 0u; i < a->length; ++i)
      if (arc_ascii_fold(a->bytes[i]) != arc_ascii_fold(b->bytes[i]))
         return false;
   return true;
}

static bool arc_name_ascii_literal(const ArcGuestName *name,
                                   const char *literal)
{
   size_t length = strlen(literal);
   if (!name || length != name->length) return false;
   for (size_t i = 0u; i < length; ++i)
      if (arc_ascii_fold(name->bytes[i]) !=
          arc_ascii_fold((uint8_t)literal[i])) return false;
   return true;
}

static bool arc_candidate_name(const ArcGuestName *name)
{
   return name && name->length && name->bytes[0] == '!';
}

bool arc_launch_candidate_is_resource(const ArcLaunchCandidate *candidate)
{
   return candidate && candidate->resource;
}

bool arc_launch_candidate_is_target(const ArcLaunchCandidate *candidate)
{
   return candidate && !candidate->resource && candidate->launchable &&
          candidate->state == ARC_LAUNCH_CANDIDATE_APPLICATION;
}

unsigned arc_launch_candidate_index(const ArcLaunchDiscovery *discovery,
                                    unsigned ordinal)
{
   if (!discovery) return UINT_MAX;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      if (discovery->candidates[i].resource) continue;
      if (!ordinal) return i;
      --ordinal;
   }
   return UINT_MAX;
}

static bool arc_candidate_launchable(const ArcGuestName *name)
{
   if (!name || name->length < 2u || name->length > 10u ||
       name->bytes[0] != '!') return false;
   for (unsigned i = 1u; i < name->length; ++i)
   {
      uint8_t c = name->bytes[i];
      if (c < 0x20u || c == 0x20u || c == 0x7fu || c == '.' || c == ':' ||
          c == '*' || c == '#' || c == '$' || c == '&' || c == '@' ||
          c == '^' || c == '%' || c == '\\' || c == '|' || c == '"' ||
          c == '<') return false;
   }
   return true;
}

static bool arc_component_safe(uint8_t c)
{
   return c > 0x20u && c != 0x7fu && c != '.' && c != ':' &&
          c != '*' && c != '#' && c != '$' && c != '&' && c != '@' &&
          c != '^' && c != '%' && c != '\\' && c != '|' && c != '"' &&
          c != '<';
}

static bool arc_parent_address(const ArcDirectoryEntry root[77],
                               unsigned root_count, unsigned parent_index,
                               ArcGuestName *address)
{
   const ArcGuestName *parent = &root[parent_index].name;
   bool literal = true;
   for (unsigned j = 0u; j < parent->length; ++j)
      if (!arc_component_safe(parent->bytes[j])) literal = false;
   unsigned safe = 0u;
   if (literal) safe = parent->length;
   else
      while (safe < parent->length && arc_component_safe(parent->bytes[safe]) &&
             parent->bytes[safe] < 0x80u) ++safe;
   if (!safe) return false;
   /* Both literal and wildcard addresses must resolve to exactly one root
    * object. A wildcard also matches the empty suffix and files. */
   unsigned matches = 0u;
   for (unsigned i = 0u; i < root_count; ++i)
   {
      const ArcGuestName *other = &root[i].name;
      bool match = literal ? arc_name_ascii_equal(parent, other) :
                   other->length >= safe;
      if (!literal && match)
         for (unsigned j = 0u; j < safe; ++j)
            if (arc_ascii_fold(parent->bytes[j]) !=
                arc_ascii_fold(other->bytes[j])) { match = false; break; }
      if (match) ++matches;
   }
   if (matches != 1u) return false;
   memset(address, 0, sizeof(*address));
   address->length = (uint8_t)(literal ? safe : safe + 1u);
   memcpy(address->bytes, parent->bytes, safe);
   if (!literal) address->bytes[safe] = '*';
   return true;
}

static void arc_classify_candidate(const ArcFileCore *fs,
                                    const ArcDirectoryEntry *entry,
                                    ArcLaunchCandidate *candidate,
                                    bool *ignored_attribute_bits)
{
   candidate->name = entry->name;
   candidate->resource =
      arc_name_ascii_literal(&candidate->name, "!System") ||
      arc_name_ascii_literal(&candidate->name, "!Fonts") ||
      arc_name_ascii_literal(&candidate->name, "!Scrap");
   candidate->launchable = arc_candidate_launchable(&candidate->name);
   candidate->state = ARC_LAUNCH_CANDIDATE_UNKNOWN;
   size_t expected = fs->new_directory ? 2048u : 1280u;
   ArcDirectoryEntry child[77];
   unsigned child_count = 0u;
   if (entry->length != expected ||
       !arc_directory_read(fs, entry->address, false, child, &child_count,
                           ignored_attribute_bits)) return;
   unsigned run_count = 0u, boot_count = 0u;
   for (unsigned item = 0u; item < child_count; ++item)
   {
      if (child[item].directory) continue;
      if (arc_name_ascii_literal(&child[item].name, "!Run")) ++run_count;
      if (arc_name_ascii_literal(&child[item].name, "!Boot")) ++boot_count;
   }
   if (run_count > 1u || boot_count > 1u)
   { candidate->launchable = false; return; }
   candidate->has_boot = boot_count == 1u;
   candidate->state = run_count == 1u ? ARC_LAUNCH_CANDIDATE_APPLICATION :
                                        ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
}

static bool arc_normal_name(const ArcGuestName *source, bool strip_bang,
                            char output[11])
{
   size_t at = 0u;
   unsigned start = strip_bang && source->length && source->bytes[0] == '!' ? 1u : 0u;
   for (unsigned i = start; i < source->length && at < 10u; ++i)
   {
      uint8_t c = source->bytes[i];
      if (c > 0x7eu) { output[0] = '\0'; return false; }
      if (c >= 'A' && c <= 'Z') output[at++] = (char)(c - 'A' + 'a');
      else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
         output[at++] = (char)c;
   }
   output[at] = '\0';
   return true;
}

static void arc_fingerprint(ArcLaunchDiscovery *discovery)
{
   if (discovery->nested_scope)
   {
      uint8_t nested[32u + ARC_LAUNCH_MAX_CANDIDATES * 27u];
      size_t at = 0u;
      static const uint8_t domain[] = "ArcDroid nested v3";
      memcpy(nested, domain, sizeof(domain)); at += sizeof(domain);
      nested[at++] = (uint8_t)discovery->candidate_count;
      nested[at++] = discovery->scan_incomplete ? 1u : 0u;
      for (unsigned i = 0u; i < discovery->candidate_count; ++i)
      {
         const ArcLaunchCandidate *c = &discovery->candidates[i];
         nested[at++] = c->parent.length;
         memcpy(nested + at, c->parent.bytes, c->parent.length);
         at += c->parent.length;
         nested[at++] = c->name.length;
         memcpy(nested + at, c->name.bytes, c->name.length);
         at += c->name.length;
         nested[at++] = (uint8_t)c->state;
         nested[at++] = c->launchable ? 1u : 0u;
         nested[at++] = c->addressable ? 1u : 0u;
         nested[at++] = c->has_boot ? 1u : 0u;
      }
      arc_sha1_hex(nested, at, discovery->candidate_fingerprint);
      return;
   }
   uint8_t bytes[1u + ARC_LAUNCH_MAX_CANDIDATES * 14u];
   size_t at = 0u;
   bytes[at++] = (uint8_t)discovery->candidate_count;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      bytes[at++] = candidate->name.length;
      memcpy(bytes + at, candidate->name.bytes, candidate->name.length);
      at += candidate->name.length;
      bytes[at++] = (uint8_t)candidate->state;
      bytes[at++] = candidate->launchable ? 1u : 0u;
      bytes[at++] = candidate->has_boot ? 1u : 0u;
   }
   arc_sha1_hex(bytes, at, discovery->candidate_fingerprint);
}

bool arc_launch_discover(const uint8_t *data, size_t size,
                         const ArcMediaInfo *media, const ArcJfdImage *jfd,
                         ArcLaunchDiscovery *discovery)
{
   ArcFileCore fs;
   ArcDirectoryEntry root[77];
   unsigned root_count = 0u;
   if (!discovery || (!jfd && (!data || !media))) return false;
   memset(discovery, 0, sizeof(*discovery));
   discovery->target_index = -1;
   discovery->reason = ARC_LAUNCH_REASON_UNREADABLE;
   if (!arc_filecore_init(&fs, data, size, media, jfd))
   { discovery->scan_incomplete = true; return true; }
   discovery->backup_map_used = fs.backup_map_used;
   discovery->old_map_clamped = fs.old_map_clamped;
   discovery->readable = true;
   discovery->boot_option = fs.boot_option;
   discovery->geometry = fs.reader.geometry;
   discovery->disc_name = fs.disc_name;
   if (!arc_directory_read(&fs, 0u, true, root, &root_count,
                           &discovery->ignored_attribute_bits))
   { discovery->readable = false; discovery->scan_incomplete = true; return true; }

   bool has_root_candidate = false;
   for (unsigned i = 0u; i < root_count; ++i)
   {
      if (arc_name_ascii_literal(&root[i].name, "!Boot"))
         discovery->root_boot = true;
      if (root[i].directory && arc_candidate_name(&root[i].name))
         has_root_candidate = true;
   }
   discovery->nested_scope = !has_root_candidate;

   for (unsigned i = 0u; i < root_count && has_root_candidate; ++i)
   {
      if (!root[i].directory || !arc_candidate_name(&root[i].name)) continue;
      ArcLaunchCandidate *candidate =
         &discovery->candidates[discovery->candidate_count++];
      candidate->addressable = true;
      arc_classify_candidate(&fs, &root[i], candidate,
                             &discovery->ignored_attribute_bits);
   }

   if (!has_root_candidate)
      for (unsigned i = 0u; i < root_count; ++i)
      {
         if (!root[i].directory) continue;
         ArcDirectoryEntry child[77];
         unsigned child_count = 0u;
         size_t expected = fs.new_directory ? 2048u : 1280u;
         if (root[i].length != expected ||
             !arc_directory_read(&fs, root[i].address, false, child,
                                 &child_count,
                                 &discovery->ignored_attribute_bits))
         {
            ++discovery->unreadable_parents;
            discovery->scan_incomplete = true;
            continue;
         }
         ArcGuestName address = {0};
         bool addressable = arc_parent_address(root, root_count, i, &address);
         for (unsigned j = 0u; j < child_count; ++j)
         {
            if (!child[j].directory || !arc_candidate_name(&child[j].name))
               continue;
            if (discovery->candidate_count == ARC_LAUNCH_MAX_CANDIDATES)
            {
               ++discovery->omitted_candidates;
               discovery->scan_incomplete = true;
               continue;
            }
            ArcLaunchCandidate *candidate =
               &discovery->candidates[discovery->candidate_count++];
            candidate->parent = root[i].name;
            candidate->parent_address = address;
            candidate->addressable = addressable;
            arc_classify_candidate(&fs, &child[j], candidate,
                                   &discovery->ignored_attribute_bits);
            unsigned matches = 0u;
            for (unsigned k = 0u; k < child_count; ++k)
               if (arc_name_ascii_equal(&candidate->name, &child[k].name))
                  ++matches;
            if (matches > 1u)
            {
               candidate->state = ARC_LAUNCH_CANDIDATE_UNKNOWN;
               candidate->launchable = false;
            }
            if (!addressable) candidate->launchable = false;
         }
      }

   for (unsigned i = 0u; i < discovery->candidate_count && has_root_candidate; ++i)
   {
      unsigned matches = 0u;
      for (unsigned j = 0u; j < root_count; ++j)
         if (arc_name_ascii_equal(&discovery->candidates[i].name,
                                  &root[j].name)) ++matches;
      if (matches > 1u)
      {
         discovery->candidates[i].state = ARC_LAUNCH_CANDIDATE_UNKNOWN;
         discovery->candidates[i].launchable = false;
      }
   }

   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      if (discovery->candidates[i].resource) continue;
      if (discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION)
         ++discovery->application_count;
      else if (discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_UNKNOWN)
         ++discovery->unknown_count;
   }
   arc_fingerprint(discovery);
   if (fs.boot_option && discovery->root_boot)
   { discovery->reason = ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF; return true; }
   if (discovery->scan_incomplete)
   { discovery->reason = ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE; return true; }
   if (!discovery->candidate_count)
   { discovery->reason = ARC_LAUNCH_REASON_NO_CANDIDATES; return true; }
   if (!discovery->application_count && !discovery->unknown_count)
   { discovery->reason = ARC_LAUNCH_REASON_NO_APPLICATIONS; return true; }
   if (discovery->application_count == 1u && !discovery->unknown_count)
   {
      for (unsigned i = 0u; i < discovery->candidate_count; ++i)
         if (!discovery->candidates[i].resource &&
             discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION)
         {
            if (discovery->candidates[i].launchable)
            {
               discovery->target_index = (int)i;
               discovery->reason = ARC_LAUNCH_REASON_SINGLE_APPLICATION;
               return true;
            }
            break;
         }
   }

   char disc[11], stripped[11];
   bool ascii_disc = arc_normal_name(&fs.disc_name, false, disc);
   memcpy(stripped, disc, sizeof(stripped));
   size_t end = strlen(stripped);
   while (end && isdigit((unsigned char)stripped[end - 1u])) stripped[--end] = '\0';
   int selected = -1;
   unsigned matches = 0u;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      if (discovery->candidates[i].resource) continue;
      if (discovery->candidates[i].state != ARC_LAUNCH_CANDIDATE_APPLICATION &&
          discovery->candidates[i].state != ARC_LAUNCH_CANDIDATE_UNKNOWN) continue;
      char candidate[11];
      if (!ascii_disc ||
          !arc_normal_name(&discovery->candidates[i].name, true, candidate))
         continue;
      if ((disc[0] && !strcmp(candidate, disc)) ||
          (stripped[0] && strcmp(stripped, disc) && !strcmp(candidate, stripped)))
      { selected = (int)i; ++matches; }
   }
   if (matches == 1u &&
       discovery->candidates[selected].state == ARC_LAUNCH_CANDIDATE_APPLICATION &&
       discovery->candidates[selected].launchable)
   {
      discovery->target_index = selected;
      discovery->reason = ARC_LAUNCH_REASON_DISC_NAME_MATCH;
   }
   else
      discovery->reason = discovery->unknown_count ?
         ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE : ARC_LAUNCH_REASON_AMBIGUOUS;
   return true;
}

bool arc_launch_escape_name(const ArcGuestName *name, char *output,
                            size_t capacity)
{
   static const char hex[] = "0123456789ABCDEF";
   size_t at = 0u;
   if (!output || !capacity) return false;
   if (!name) { output[0] = '\0'; return true; }
   for (unsigned i = 0u; i < name->length; ++i)
   {
      uint8_t c = name->bytes[i];
      size_t needed = c >= 0x20u && c <= 0x7eu ?
         (c == '"' || c == '\\' ? 2u : 1u) : 4u;
      if (needed >= capacity - at)
      { output[at] = '\0'; return false; }
      if (needed == 4u)
      {
         output[at++] = '\\'; output[at++] = 'x';
         output[at++] = hex[c >> 4u]; output[at++] = hex[c & 15u];
      }
      else
      {
         if (needed == 2u) output[at++] = '\\';
         output[at++] = (char)c;
      }
   }
   output[at] = '\0';
   return true;
}

const char *arc_launch_reason_name(ArcLaunchReason reason)
{
   switch (reason)
   {
      case ARC_LAUNCH_REASON_UNREADABLE: return "unreadable";
      case ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE: return "inspection incomplete";
      case ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF: return "disc boots itself";
      case ARC_LAUNCH_REASON_NO_CANDIDATES: return "no candidates";
      case ARC_LAUNCH_REASON_NO_APPLICATIONS: return "no applications";
      case ARC_LAUNCH_REASON_AMBIGUOUS: return "ambiguous";
      case ARC_LAUNCH_REASON_SINGLE_APPLICATION: return "single application";
      case ARC_LAUNCH_REASON_DISC_NAME_MATCH: return "disc-name match";
      default: return "unknown";
   }
}
