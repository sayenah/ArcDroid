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
} ArcDiscReader;

typedef struct {
   ArcDiscReader reader;
   bool new_map;
   bool new_directory;
   size_t disc_size;
   unsigned boot_option;
   ArcGuestName disc_name;
   uint8_t map[1024];
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
      size_t interleaved_track = logical_sector / 5u;
      track_side = interleaved_track;
      sector_id = (unsigned)(logical_sector % 5u);
   }
   if (track_side >= reader->jfd->track_count) return false;
   const ArcJfdTrack *track = &reader->jfd->tracks[track_side];
   const ArcJfdSector *match = NULL;
   for (unsigned i = 0u; i < track->sector_count; ++i)
   {
      const ArcJfdSector *sector =
         &reader->jfd->sectors[(size_t)track->first_sector + i];
      if (sector->id != sector_id || sector->density != reader->density ||
          sector->size != reader->sector_size) continue;
      if (match) return false;
      match = sector;
   }
   if (!match || match->data_offset == UINT32_MAX ||
       (match->flags & (ARC_JFD_ID_CRC_BAD | ARC_JFD_DATA_CRC_BAD)) ||
       match->data_offset > reader->jfd->decoded_size ||
       match->size > reader->jfd->decoded_size - match->data_offset)
      return false;
   memcpy(output, reader->jfd->decoded + match->data_offset,
          reader->sector_size);
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
   if (sectors < minimum || sectors > expected) return false;
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

static bool arc_filecore_new(ArcFileCore *fs)
{
   if (!arc_reader_configure(&fs->reader, ARC_GEOMETRY_800K_E) ||
       !arc_reader_read(&fs->reader, 0u, fs->map, sizeof(fs->map))) return false;
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
      return arc_filecore_new(fs);
   }
   if (!data || !media) return false;
   if (media->geometry == ARC_GEOMETRY_640K_L ||
       media->geometry == ARC_GEOMETRY_800K_D)
      return arc_filecore_old(fs, media->geometry);
   if (media->geometry == ARC_GEOMETRY_800K_E)
      return arc_filecore_new(fs);
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

static bool arc_new_map_resolve(const ArcFileCore *fs, uint32_t address,
                                size_t object_sector, size_t *disc_offset)
{
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
   if (!arc_filecore_init(&fs, data, size, media, jfd)) return true;
   discovery->readable = true;
   discovery->boot_option = fs.boot_option;
   discovery->geometry = fs.reader.geometry;
   discovery->disc_name = fs.disc_name;
   if (!arc_directory_read(&fs, 0u, true, root, &root_count,
                           &discovery->ignored_attribute_bits))
   { discovery->readable = false; return true; }

   for (unsigned i = 0u; i < root_count; ++i)
   {
      if (!root[i].directory || !arc_candidate_name(&root[i].name)) continue;
      ArcLaunchCandidate *candidate =
         &discovery->candidates[discovery->candidate_count++];
      candidate->name = root[i].name;
      candidate->resource =
         arc_name_ascii_literal(&candidate->name, "!System") ||
         arc_name_ascii_literal(&candidate->name, "!Fonts") ||
         arc_name_ascii_literal(&candidate->name, "!Scrap");
      candidate->launchable = arc_candidate_launchable(&candidate->name);
      candidate->state = ARC_LAUNCH_CANDIDATE_UNKNOWN;
      size_t expected = fs.new_directory ? 2048u : 1280u;
      ArcDirectoryEntry child[77];
      unsigned child_count = 0u;
      if (root[i].length != expected ||
          !arc_directory_read(&fs, root[i].address, false, child, &child_count,
                              &discovery->ignored_attribute_bits))
         continue;
      unsigned run_count = 0u, boot_count = 0u;
      for (unsigned item = 0u; item < child_count; ++item)
      {
         if (child[item].directory) continue;
         if (arc_name_ascii_literal(&child[item].name, "!Run")) ++run_count;
         if (arc_name_ascii_literal(&child[item].name, "!Boot")) ++boot_count;
      }
      if (run_count > 1u || boot_count > 1u)
      {
         candidate->launchable = false;
         continue;
      }
      candidate->has_boot = boot_count == 1u;
      candidate->state = run_count == 1u ? ARC_LAUNCH_CANDIDATE_APPLICATION :
                                           ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
   }

   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
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
   if (fs.boot_option)
   { discovery->reason = ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF; return true; }
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
