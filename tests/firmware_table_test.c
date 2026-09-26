/* Test-only firmware catalogue extension. GPL-2.0-only. */
#define arc_firmware_table arc_base_firmware_table
#define arc_firmware_match arc_base_firmware_match
#define arc_firmware_auto_priority arc_base_firmware_auto_priority
#define arc_firmware_nearest_size arc_base_firmware_nearest_size
#define arc_os_name arc_base_os_name
#define arc_firmware_assemble arc_base_firmware_assemble
#include "../core/firmware_table.c"
#undef arc_firmware_table
#undef arc_firmware_match
#undef arc_firmware_auto_priority
#undef arc_firmware_nearest_size
#undef arc_os_name
#undef arc_firmware_assemble

static const ArcFirmwareFingerprint chooser_firmware = {
   ARC_OS_TEST, "ArcDroid chooser application ROM", 524288u,
   UINT32_C(0xf3f7a049), "434ed89211a1d6bb9b7ccb67fffcd6dc6c1101a6",
   ARC_RECIPE_COMBINED, 0u
};

static const ArcFirmwareFingerprint unsupported_launch_firmware = {
   ARC_OS_RISCOS_200, "ArcDroid unsupported-launch test ROM", 524288u,
   UINT32_C(0x46276f78), "76eade94a489bac39296cc5eb1adbe683af1acb9",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint install_guest_firmware = {
   ARC_OS_TEST, "ArcDroid install guest ROM", 524288u,
   UINT32_C(0x54d5aaa0), "a1284ef5c91773f79a7a2c5234ef0425f3abf374",
   ARC_RECIPE_COMBINED, 0u
};

const ArcFirmwareFingerprint *arc_firmware_table(size_t *count)
{
   enum { MAX_TEST_FIRMWARE = 64 };
   static ArcFirmwareFingerprint table[MAX_TEST_FIRMWARE];
   static size_t table_count;
   if (!table_count)
   {
      size_t base_count = 0u;
      const ArcFirmwareFingerprint *base = arc_base_firmware_table(&base_count);
      if (base_count >= MAX_TEST_FIRMWARE - 2u)
         base_count = MAX_TEST_FIRMWARE - 3u;
      memcpy(table, base, base_count * sizeof(*table));
      table[base_count] = chooser_firmware;
      table[base_count + 1u] = unsupported_launch_firmware;
      table[base_count + 2u] = install_guest_firmware;
      table_count = base_count + 3u;
   }
   if (count) *count = table_count;
   return table;
}

const ArcFirmwareFingerprint *arc_firmware_match(const ArcDigest *digest)
{
   if (digest && digest->size == chooser_firmware.size &&
       digest->crc32 == chooser_firmware.crc32 &&
       !strcmp(digest->sha1, chooser_firmware.sha1))
      return &chooser_firmware;
   if (digest && digest->size == unsupported_launch_firmware.size &&
       digest->crc32 == unsupported_launch_firmware.crc32 &&
       !strcmp(digest->sha1, unsupported_launch_firmware.sha1))
      return &unsupported_launch_firmware;
   if (digest && digest->size == install_guest_firmware.size &&
       digest->crc32 == install_guest_firmware.crc32 &&
       !strcmp(digest->sha1, install_guest_firmware.sha1))
      return &install_guest_firmware;
   return arc_base_firmware_match(digest);
}

unsigned arc_firmware_auto_priority(ArcOsClass os)
{
   return arc_base_firmware_auto_priority(os);
}

bool arc_firmware_nearest_size(size_t actual, size_t margin, size_t *expected)
{
   return arc_base_firmware_nearest_size(actual, margin, expected);
}

const char *arc_os_name(ArcOsClass os)
{
   return arc_base_os_name(os);
}

bool arc_firmware_assemble(const uint8_t *const *parts, const size_t *sizes,
                           unsigned count, ArcFirmwareRecipe recipe,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size)
{
   return arc_base_firmware_assemble(parts, sizes, count, recipe, output,
                                     output_size, error, error_size);
}
