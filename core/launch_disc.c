/* ArcDroid deterministic drive-1 launch disc. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
   ARC_L_SECTOR_SIZE = 256u,
   ARC_L_TOTAL_SECTORS = ARC_LAUNCH_DISC_SIZE / ARC_L_SECTOR_SIZE,
   ARC_L_ROOT_SECTOR = 2u,
   ARC_L_BOOT_SECTOR = 1279u,
   ARC_L_LAUNCH_SECTOR = 1280u,
   ARC_L_RAM_SECTOR = 7u
};

/* A numbered text program loads without BASIC's "Program renumbered" prompt. */
static const char arc_ram_program[] =
   "10 SYS \"OS_ReadDynamicArea\",5 TO ,size%\r"
   "20 IF size%=0 THEN SYS \"XOS_ChangeDynamicArea\",5,32768\r";

static void arc_store_u24(uint8_t *p, unsigned value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8u);
   p[2] = (uint8_t)(value >> 16u);
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

static uint8_t arc_ascii_fold(uint8_t c)
{
   return c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + 'a') : c;
}

static bool arc_name_literal(const ArcGuestName *name, const char *literal)
{
   size_t length = strlen(literal);
   if (!name || name->length != length) return false;
   for (size_t i = 0u; i < length; ++i)
      if (arc_ascii_fold(name->bytes[i]) !=
          arc_ascii_fold((uint8_t)literal[i])) return false;
   return true;
}

static bool arc_leaf_representable(const ArcGuestName *name)
{
   if (!name || name->length < 2u || name->length > ARC_GUEST_NAME_MAX ||
       name->bytes[0] != '!') return false;
   for (unsigned i = 1u; i < name->length; ++i)
   {
      uint8_t c = name->bytes[i];
      if (c <= 0x20u || c == 0x7fu || c == '.' || c == ':' || c == '*' ||
          c == '#' || c == '$' || c == '&' || c == '@' || c == '^' ||
          c == '%' || c == '\\' || c == '|' || c == '"' || c == '<')
         return false;
   }
   return true;
}

bool arc_launch_sibling_bootable(const ArcLaunchCandidate *candidate)
{
   return candidate && candidate->has_boot && candidate->launchable &&
          arc_leaf_representable(&candidate->name);
}

/* Shared literal admission for RPC plans. Does not build the legacy Desktop
 * script or execute sibling boots. Discovery owns parent uniqueness proof. */
bool arc_launch_candidate_path(const ArcLaunchCandidate *candidate,
                               unsigned drive, char *out, size_t capacity)
{
   if (!out || (drive != 0u && drive != 4u) ||
       !arc_launch_candidate_is_target(candidate) ||
       !arc_leaf_representable(&candidate->name) ||
       candidate->parent.length > ARC_GUEST_NAME_MAX) return false;
   if (candidate->parent.length)
   {
      if (!candidate->addressable ||
          candidate->parent_address.length != candidate->parent.length ||
          memcmp(candidate->parent_address.bytes, candidate->parent.bytes,
                 candidate->parent.length)) return false;
      /* Apply the same literal rules to the parent without the bang rule. */
      ArcGuestName probe = {{'!'}, 1u};
      for (unsigned i = 0; i < candidate->parent.length; ++i)
      {
         probe.bytes[1] = candidate->parent.bytes[i]; probe.length = 2u;
         if (!arc_leaf_representable(&probe)) return false;
      }
   }
   int n = snprintf(out, capacity, "ADFS::%u.$.%.*s%s%.*s", drive,
       (int)candidate->parent.length, (const char *)candidate->parent.bytes,
       candidate->parent.length ? "." : "", (int)candidate->name.length,
       (const char *)candidate->name.bytes);
   return n > 0 && (size_t)n < capacity;
}

typedef struct {
   uint8_t *output;
   size_t capacity;
   size_t size;
   bool ok;
} ArcScriptWriter;

static void arc_script_bytes(ArcScriptWriter *writer, const void *data,
                             size_t size)
{
   if (!writer->ok || size > SIZE_MAX - writer->size)
   { writer->ok = false; return; }
   if (writer->output)
   {
      if (writer->size > writer->capacity ||
          size > writer->capacity - writer->size)
      { writer->ok = false; return; }
      memcpy(writer->output + writer->size, data, size);
   }
   writer->size += size;
}

static void arc_script_text(ArcScriptWriter *writer, const char *text)
{
   arc_script_bytes(writer, text, strlen(text));
}

static void arc_script_leaf(ArcScriptWriter *writer, const ArcGuestName *name)
{
   arc_script_bytes(writer, name->bytes, name->length);
}

static void arc_script_path(ArcScriptWriter *writer, const ArcGuestName *name)
{
   arc_script_text(writer, "ADFS::0.$.");
   arc_script_leaf(writer, name);
}

static void arc_script_candidate_path(ArcScriptWriter *writer,
                                       const ArcLaunchCandidate *candidate)
{
   arc_script_text(writer, "ADFS::0.$.");
   if (candidate->parent.length)
   {
      arc_script_leaf(writer, &candidate->parent_address);
      arc_script_text(writer, ".");
   }
   arc_script_leaf(writer, &candidate->name);
}

static void arc_script_boot(ArcScriptWriter *writer,
                            const ArcLaunchCandidate *candidate)
{
   arc_script_text(writer, "%Filer_Boot ");
   if (candidate->parent.length)
      arc_script_candidate_path(writer, candidate);
   else
      arc_script_path(writer, &candidate->name);
   arc_script_text(writer, "\r");
}

static void arc_launch_error(char *error, size_t error_size,
                             const char *message)
{
   if (error && error_size) snprintf(error, error_size, "%s", message);
}

bool arc_launch_script_build_ramdisc(const ArcLaunchDiscovery *discovery,
                             unsigned target_index, bool ramdisc, uint8_t *output,
                             size_t capacity, size_t *output_size,
                             char *error, size_t error_size)
{
   if (output_size) *output_size = 0u;
   if (!discovery || target_index >= discovery->candidate_count)
   { arc_launch_error(error, error_size, "launch target does not exist"); return false; }
   const ArcLaunchCandidate *target = &discovery->candidates[target_index];
   if (target->state != ARC_LAUNCH_CANDIDATE_APPLICATION || !target->launchable ||
       !arc_leaf_representable(&target->name))
   { arc_launch_error(error, error_size, "launch target leaf is not representable"); return false; }
   if (target->parent.length && !target->addressable)
   { arc_launch_error(error, error_size, "launch target parent is not representable"); return false; }

   ArcScriptWriter writer = {output, capacity, 0u, true};
   arc_script_text(&writer, "| ArcDroid desktop boot file\r");
   if (ramdisc)
      arc_script_text(&writer, "%BASIC -quit ADFS::1.$.ArcRAM\r");
   static const char *const preferred[] = {"!System", "!Fonts", "!Scrap"};
   for (unsigned p = 0u; p < sizeof(preferred) / sizeof(preferred[0]); ++p)
      for (unsigned i = 0u; i < discovery->candidate_count; ++i)
         if (i != target_index &&
             arc_launch_sibling_bootable(&discovery->candidates[i]) &&
             discovery->candidates[i].parent.length == target->parent.length &&
             !memcmp(discovery->candidates[i].parent.bytes, target->parent.bytes,
                     target->parent.length) &&
             arc_name_literal(&discovery->candidates[i].name, preferred[p]))
            arc_script_boot(&writer, &discovery->candidates[i]);
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      if (i == target_index || !arc_launch_sibling_bootable(candidate) ||
          candidate->parent.length != target->parent.length ||
          memcmp(candidate->parent.bytes, target->parent.bytes,
                 target->parent.length) ||
          arc_name_literal(&candidate->name, "!System") ||
          arc_name_literal(&candidate->name, "!Fonts") ||
          arc_name_literal(&candidate->name, "!Scrap")) continue;
      arc_script_boot(&writer, candidate);
   }
   arc_script_boot(&writer, target);
   arc_script_text(&writer, "%Dir ADFS::0.$\r");
   arc_script_text(&writer, "%URD ADFS::0.$\r");
   arc_script_text(&writer, "%Filer_Run ");
   if (target->parent.length)
      arc_script_candidate_path(&writer, target);
   else
      arc_script_path(&writer, &target->name);
   arc_script_text(&writer, "\r");
   if (!writer.ok)
   { arc_launch_error(error, error_size, "desktop boot file buffer is too small"); return false; }
   if (output_size) *output_size = writer.size;
   return true;
}

bool arc_launch_script_build(const ArcLaunchDiscovery *discovery,
                             unsigned target_index, uint8_t *output,
                             size_t capacity, size_t *output_size,
                             char *error, size_t error_size)
{
   return arc_launch_script_build_ramdisc(discovery, target_index, false,
      output, capacity, output_size, error, error_size);
}

static bool arc_launch_special_script(const ArcLaunchDiscovery *discovery,
                                      unsigned target_index,
                                      ArcHdHelperMode mode,
                                      const ArcHdProgramSpec *spec,
                                      uint8_t *output, size_t capacity,
                                      size_t *output_size,
                                      char *error, size_t error_size)
{
   if (!spec || !spec->target ||
       (mode != ARC_HD_HELPER_INSTALL && !spec->product) ||
       !discovery || target_index >= discovery->candidate_count)
   { arc_launch_error(error,error_size,"hard-disc helper target is missing"); return false; }
   ArcScriptWriter writer={output,capacity,0u,true};
   arc_script_text(&writer,"| ArcDroid desktop boot file\r");
   if (mode == ARC_HD_HELPER_INSTALL)
   {
      const ArcLaunchCandidate *target=&discovery->candidates[target_index];
      static const char *const preferred[]={"!System","!Fonts","!Scrap"};
      for (unsigned p=0u; p<3u; ++p)
         for (unsigned i=0u; i<discovery->candidate_count; ++i)
            if (i != target_index &&
                arc_launch_sibling_bootable(&discovery->candidates[i]) &&
                discovery->candidates[i].parent.length == target->parent.length &&
                !memcmp(discovery->candidates[i].parent.bytes,target->parent.bytes,
                        target->parent.length) &&
                arc_name_literal(&discovery->candidates[i].name,preferred[p]))
               arc_script_boot(&writer,&discovery->candidates[i]);
      for (unsigned i=0u; i<discovery->candidate_count; ++i)
      {
         const ArcLaunchCandidate *candidate=&discovery->candidates[i];
         if (i == target_index || !arc_launch_sibling_bootable(candidate) ||
             candidate->parent.length != target->parent.length ||
             memcmp(candidate->parent.bytes,target->parent.bytes,
                    target->parent.length) ||
             arc_name_literal(&candidate->name,"!System") ||
             arc_name_literal(&candidate->name,"!Fonts") ||
             arc_name_literal(&candidate->name,"!Scrap")) continue;
         arc_script_boot(&writer,candidate);
      }
      arc_script_text(&writer,"%BASIC -quit ADFS::1.$.ArcInstall\r");
   }
   else if (mode == ARC_HD_HELPER_MARK)
      arc_script_text(&writer,"%BASIC -quit ADFS::1.$.ArcMark\r");
   else if (mode == ARC_HD_HELPER_PRODUCT)
   {
      arc_script_text(&writer,"%Filer_Boot ADFS::4.$.ArcDroid.");
      arc_script_text(&writer,spec->product);
      arc_script_text(&writer,"\r%Dir ADFS::4.$.ArcDroid\r");
      arc_script_text(&writer,"%URD ADFS::4.$.ArcDroid\r");
      arc_script_text(&writer,"%Filer_Run ADFS::4.$.ArcDroid.");
      arc_script_text(&writer,spec->product);
      arc_script_text(&writer,"\r");
   }
   else { arc_launch_error(error,error_size,"unsupported hard-disc helper"); return false; }
   if (!writer.ok)
   { arc_launch_error(error,error_size,"desktop boot file buffer is too small"); return false; }
   if (output_size) *output_size=writer.size;
   return true;
}

static size_t arc_l_physical_offset(unsigned logical_sector)
{
   unsigned track = logical_sector / 16u;
   unsigned side = track / 80u;
   unsigned cylinder = track % 80u;
   unsigned physical_sector = (cylinder * 2u + side) * 16u +
                              logical_sector % 16u;
   return (size_t)physical_sector * ARC_L_SECTOR_SIZE;
}

static bool arc_l_write(uint8_t output[ARC_LAUNCH_DISC_SIZE],
                        unsigned logical_sector, const uint8_t *data,
                        size_t size)
{
   size_t at = 0u;
   while (at < size)
   {
      unsigned sector = logical_sector + (unsigned)(at / ARC_L_SECTOR_SIZE);
      size_t within = at % ARC_L_SECTOR_SIZE;
      size_t amount = ARC_L_SECTOR_SIZE - within;
      if (sector >= ARC_L_TOTAL_SECTORS) return false;
      if (amount > size - at) amount = size - at;
      memcpy(output + arc_l_physical_offset(sector) + within, data + at, amount);
      at += amount;
   }
   return true;
}

static void arc_old_entry(uint8_t *entry, const char *name, uint32_t load,
                          size_t length, unsigned sector)
{
   size_t name_length = strlen(name);
   memset(entry, '\r', 10u);
   memcpy(entry, name, name_length);
   entry[0] |= 0x80u; /* owner read */
   entry[1] |= 0x80u; /* owner write */
   entry[10] = (uint8_t)load;
   entry[11] = (uint8_t)(load >> 8u);
   entry[12] = (uint8_t)(load >> 16u);
   entry[13] = (uint8_t)(load >> 24u);
   /* RISC OS 3.11 will not run a typed file whose exec field equals load. */
   memset(entry + 14u, 0, 4u);
   entry[18] = (uint8_t)length;
   entry[19] = (uint8_t)(length >> 8u);
   entry[20] = (uint8_t)(length >> 16u);
   entry[21] = (uint8_t)(length >> 24u);
   arc_store_u24(entry + 22u, sector);
   entry[25] = 0u;
}

static bool arc_launch_disc_build_internal(const ArcLaunchDiscovery *discovery,
                           unsigned target_index, bool ramdisc,
                           ArcHdHelperMode mode, const ArcHdProgramSpec *spec,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE],
                           char *error, size_t error_size)
{
   static const char boot[] =
      "| ArcDroid launch disc\r"
      "%Desktop -File \"ADFS::1.$.ArcLaunch\"\r";
   size_t launch_size = 0u;
   bool special = mode != ARC_HD_HELPER_FLOPPY;
   if (!output || !(special ?
       arc_launch_special_script(discovery,target_index,mode,spec,NULL,0u,
                                 &launch_size,error,error_size) :
       arc_launch_script_build_ramdisc(discovery,target_index,ramdisc,NULL,0u,
                                      &launch_size,error,error_size)))
      return false;
   size_t program_size = 0u;
   if ((mode == ARC_HD_HELPER_INSTALL || mode == ARC_HD_HELPER_MARK) &&
       !arc_hd_program_build(mode,spec,NULL,0u,&program_size))
   { arc_launch_error(error,error_size,"hard-disc BASIC program is invalid"); return false; }
   unsigned program_sectors = (unsigned)((program_size+ARC_L_SECTOR_SIZE-1u)/ARC_L_SECTOR_SIZE);
   if (program_sectors > ARC_L_BOOT_SECTOR - ARC_L_RAM_SECTOR)
   { arc_launch_error(error,error_size,"hard-disc BASIC program is too large"); return false; }
   size_t launch_sectors = (launch_size + ARC_L_SECTOR_SIZE - 1u) /
                           ARC_L_SECTOR_SIZE;
   if (!launch_sectors || launch_sectors > ARC_L_TOTAL_SECTORS - ARC_L_LAUNCH_SECTOR)
   { arc_launch_error(error, error_size, "desktop boot file does not fit helper disc"); return false; }
   uint8_t *launch = malloc(launch_size);
   if (!launch)
   { arc_launch_error(error, error_size, "desktop boot file allocation failed"); return false; }
   size_t written = 0u;
   if (!(special ?
         arc_launch_special_script(discovery,target_index,mode,spec,launch,
                                   launch_size,&written,error,error_size) :
         arc_launch_script_build_ramdisc(discovery,target_index,ramdisc,launch,
                                         launch_size,&written,error,error_size)) ||
       written != launch_size)
   { free(launch); return false; }
   uint8_t *program=NULL;
   if (program_size)
   {
      program=malloc(program_size);
      size_t generated=0u;
      if (!program ||
          !arc_hd_program_build(mode,spec,program,program_size,&generated) ||
          generated != program_size)
      { free(program); free(launch); return false; }
   }

   memset(output, 0, ARC_LAUNCH_DISC_SIZE);
   /* Free extents surround the boot sector and the exact launch-file extent. */
   arc_store_u24(output, ARC_L_RAM_SECTOR +
                 (ramdisc ? 1u : program_sectors));
   arc_store_u24(output + 3u, ARC_L_LAUNCH_SECTOR + (unsigned)launch_sectors);
   memcpy(output + 247u, "Acri\r", 5u); /* ArcDroid, interleaved halves. */
   arc_store_u24(output + 252u, ARC_L_TOTAL_SECTORS);
   arc_store_u24(output + 256u, ARC_L_BOOT_SECTOR -
                 (ramdisc ? ARC_L_RAM_SECTOR + 1u :
                            ARC_L_RAM_SECTOR + program_sectors));
   arc_store_u24(output + 259u,
                 ARC_L_TOTAL_SECTORS - ARC_L_LAUNCH_SECTOR -
                 (unsigned)launch_sectors);
   memcpy(output + 502u, "rDod\r", 5u);
   output[507] = 0x44u;
   output[508] = 0x41u;
   output[509] = 2u;
   output[510] = 6u;
   output[255] = arc_old_checksum(output);
   output[511] = arc_old_checksum(output + 256u);

   uint8_t *root = output + arc_l_physical_offset(ARC_L_ROOT_SECTOR);
   root[0] = 1u;
   memcpy(root + 1u, "Hugo", 4u);
   arc_old_entry(root + 5u, "!Boot", UINT32_C(0xffffeb00),
                 sizeof(boot) - 1u, ARC_L_BOOT_SECTOR);
   arc_old_entry(root + 31u, "ArcLaunch", UINT32_C(0xffffea00),
                 launch_size, ARC_L_LAUNCH_SECTOR);
   if (ramdisc)
      arc_old_entry(root + 57u, "ArcRAM", UINT32_C(0xffffff00),
                    sizeof(arc_ram_program) - 1u, ARC_L_RAM_SECTOR);
   else if (program_size)
      arc_old_entry(root + 57u,
                    mode == ARC_HD_HELPER_INSTALL ? "ArcInstall" : "ArcMark",
                    UINT32_C(0xffffff00), program_size, ARC_L_RAM_SECTOR);
   uint8_t *tail = root + 1227u;
   tail[0] = 0u;
   memset(tail + 1u, '\r', 10u);
   tail[1] = '$';
   arc_store_u24(tail + 11u, ARC_L_ROOT_SECTOR);
   memcpy(tail + 14u, "ArcDroid launch", 15u);
   tail[47] = 1u;
   memcpy(tail + 48u, "Hugo", 4u);
   tail[52] = 0u; /* FileCore's accepted old-directory sentinel. */

   bool ok = arc_l_write(output, ARC_L_BOOT_SECTOR,
                         (const uint8_t *)boot, sizeof(boot) - 1u) &&
             arc_l_write(output, ARC_L_LAUNCH_SECTOR, launch, launch_size) &&
             (!ramdisc || arc_l_write(output, ARC_L_RAM_SECTOR,
                            (const uint8_t *)arc_ram_program,
                            sizeof(arc_ram_program) - 1u)) &&
             (!program || arc_l_write(output,ARC_L_RAM_SECTOR,program,
                                      program_size));
   free(program);
   free(launch);
   if (!ok) arc_launch_error(error, error_size, "helper disc write failed");
   return ok;
}

bool arc_launch_disc_build_ramdisc(const ArcLaunchDiscovery *discovery,
                           unsigned target_index, bool ramdisc,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE],
                           char *error, size_t error_size)
{
   return arc_launch_disc_build_internal(discovery,target_index,ramdisc,
      ARC_HD_HELPER_FLOPPY,NULL,output,error,error_size);
}

bool arc_launch_disc_build_mode(const ArcLaunchDiscovery *discovery,
                           unsigned target_index, ArcHdHelperMode mode,
                           const ArcHdProgramSpec *spec,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE],
                           char *error, size_t error_size)
{
   return arc_launch_disc_build_internal(discovery,target_index,false,mode,
                                         spec,output,error,error_size);
}

bool arc_launch_disc_build(const ArcLaunchDiscovery *discovery,
                           unsigned target_index,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE],
                           char *error, size_t error_size)
{
   return arc_launch_disc_build_ramdisc(discovery, target_index, false,
      output, error, error_size);
}
