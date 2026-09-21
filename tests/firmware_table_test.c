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
   UINT32_C(0x254e55c8), "69bc5ba8665154e0688900d7d9035a4fe2244701",
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
      if (base_count >= MAX_TEST_FIRMWARE) base_count = MAX_TEST_FIRMWARE - 1u;
      memcpy(table, base, base_count * sizeof(*table));
      table[base_count] = chooser_firmware;
      table_count = base_count + 1u;
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
