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
static const ArcFirmwareFingerprint native_hd_guest_firmware = {
   ARC_OS_TEST, "ArcDroid native hard disc guest ROM", 524288u,
   UINT32_C(0xaf7b20fe), "705391add28dbbda52ae7196ebdac5f193a7beff",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint native_hd_stress_firmware = {
   ARC_OS_TEST, "ArcDroid native hard disc stress ROM", 524288u,
   UINT32_C(0xb274b6d4), "feeb34420c9b8c79013a50b81c21947a94d57b62",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint error_box_arbitration_firmware = {
   ARC_OS_RISCOS_311, "ArcDroid error box arbitration ROM", 524288u,
   UINT32_C(0x3b4eacb6), "d7c75c609c2321be519b36dfed6c0669f60e1ebb",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint error_box_arbitration_mouse_firmware = {
   ARC_OS_RISCOS_311, "ArcDroid error box arbitration mouse ROM", 524288u,
   UINT32_C(0xb9031505), "7521070dc8add9bb59732d18ecbc125356be078c",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint error_box_arbitration_after_firmware = {
   ARC_OS_RISCOS_311, "ArcDroid error box arbitration after ROM", 524288u,
   UINT32_C(0xa7ec3295), "6c4ba091dca0549727bf4238c1cd1400351f9863",
   ARC_RECIPE_COMBINED, 0u
};
static const ArcFirmwareFingerprint disc_request_firmware[] = {
   {ARC_OS_TEST, "ArcDroid invented disc request A ROM", 524288u,
    UINT32_C(0x04dec093), "0e45f047ac0e814b46953dd1a030a35db3bbb592",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B ROM", 524288u,
    UINT32_C(0xe4a4ec3c), "60c1dc94407cfc552b2afbbf5d128a76964e4057",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented successful disc lookup ROM", 524288u,
    UINT32_C(0x4880f0b5), "78ada5ac3c42246bb749eb88c141faecdc4da57a",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B back ROM", 524288u,
    UINT32_C(0x2c53c9b0), "1a7e19c0288eb93626b71f0113440c053c116d9f",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B alternating ROM", 524288u,
    UINT32_C(0x85d83520), "143dfc5c1b297196b3a60069db7e3c92fd8813e5",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request A1 ROM", 524288u,
    UINT32_C(0xacb49dcf), "5398f293e5c110434cb15a0fdf4a1eddc09cd520",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request A chain ROM", 524288u,
    UINT32_C(0x4995f6a8), "65f9c9179dcee7b6fa74db22943815eae95c982e",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request A back ROM", 524288u,
    UINT32_C(0x53376a98), "b3e5258b27fda4a5257c94575f30199d4eb31403",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B single ROM", 524288u,
    UINT32_C(0xfc012fc1), "d5121df9c4274fbf03c6349282badf8cc70b8731",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B Wimp ROM", 524288u,
    UINT32_C(0x40c91d52), "34b194cbdc491522ffd0d186007d60dfdd5082ac",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B other disc ROM", 524288u,
    UINT32_C(0xfcda148e), "e18d362326bd84600fca44ad315919f650ee305e",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B directory load ROM", 524288u,
    UINT32_C(0xa1822bc9), "6d9e26d1e4547de17b7ef8b146f8462fe6e07484",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B directory catalogue ROM", 524288u,
    UINT32_C(0xf64d364b), "46ce07a8010e317bfa6bec8deef3855125eb1e72",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B D6 ROM", 524288u,
    UINT32_C(0xe29ecad2), "d2de80a92cd54fa54330f52f997585a75f83ce98",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B drive-empty ROM", 524288u,
    UINT32_C(0x9bce85be), "bce05acb1c69877daa73558dada18bff28cf31c1",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B blocking-input ROM", 524288u,
    UINT32_C(0xdf15fc8a), "320d24918a47d9c9ac4f2adde8eb06feb770516e",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B distinct ROM", 524288u,
    UINT32_C(0xf0f3c26c), "0bec8d9618e916265117a076f965f1c3374557ec",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B two-context ROM", 524288u,
    UINT32_C(0xd0df2787), "755ab2556e7b05abd5e27f206b498d9e1fdc4a7b",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B error-branch ROM", 524288u,
    UINT32_C(0xdcfb6565), "e92ddc84a872448f7ba93f4124b1b8029faad3df",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B non-X error ROM", 524288u,
    UINT32_C(0xf02b7713), "a3d135d40a3864e468024e8414b24f33a2ecf5b9",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B conflict ROM", 524288u,
    UINT32_C(0x702a884c), "3adf4fd0d00cc034241c182632248064c59c055f",
    ARC_RECIPE_COMBINED, 0u},
   {ARC_OS_TEST, "ArcDroid invented disc request B directory-rejecting open ROM", 524288u,
    UINT32_C(0x574bd150), "724ce9f1fc8a7a94f221444cba61efb114713c10",
    ARC_RECIPE_COMBINED, 0u}
};
static const ArcFirmwareFingerprint launch_outcome_firmware[] = {
   {ARC_OS_TEST,"ArcDroid invented launch outcome exact ROM",524288u,
    UINT32_C(0xc7fcf1d0),"8baf63e08cb2d0af38a1e5623d060415aed14097",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome other ROM",524288u,
    UINT32_C(0x775267d4),"352c693fcc4c99dbe8335f054d14af132fa8e4e7",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome no-poll ROM",524288u,
    UINT32_C(0xbb993a44),"2f53d2d76684878eab659f6a4b27b7d6af4e2cc9",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome close-init ROM",524288u,
    UINT32_C(0xf07e206f),"39da3e4667bd3345995e298b5070dcf32c2ac932",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome refused ROM",524288u,
    UINT32_C(0xe4db2e24),"719809766875a39f4828a3e94c253147049a30ef",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome copy-clean ROM",524288u,
    UINT32_C(0x86b3c5da),"bf718315418f8bbc2713703e32c2f8f66ba3d32c",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome copy-abex ROM",524288u,
    UINT32_C(0xb7fb9aba),"d569f223b13ba12b2b13fa3b6d1e33a04392f275",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome copy-ram-abex ROM",524288u,
    UINT32_C(0xae248253),"ba6a5f50b549c202bc8cfd062fffe827b3f81d02",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome copy-delayed ROM",524288u,
    UINT32_C(0xecd65536),"cd382af6b64ee35df3765d5c3e371a4bef330578",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome delayed-input ROM",524288u,
    UINT32_C(0x55554b5a),"086534616e366aba89b4b0f4670ddad96d8a0ae8",
    ARC_RECIPE_COMBINED,0u},
   {ARC_OS_TEST,"ArcDroid invented launch outcome delayed-reset ROM",524288u,
    UINT32_C(0x4507848d),"16d45b2ae5639bbbf13424153e4c4fc31cdff4d2",
    ARC_RECIPE_COMBINED,0u}
};

const ArcFirmwareFingerprint *arc_firmware_table(size_t *count)
{
   enum { MAX_TEST_FIRMWARE = 128 };
   static ArcFirmwareFingerprint table[MAX_TEST_FIRMWARE];
   static size_t table_count;
   if (!table_count)
   {
      size_t base_count = 0u;
      const ArcFirmwareFingerprint *base = arc_base_firmware_table(&base_count);
      size_t extra_count = 7u + sizeof(launch_outcome_firmware) /
                                 sizeof(launch_outcome_firmware[0]) +
         sizeof(disc_request_firmware) / sizeof(disc_request_firmware[0]);
      if (base_count > MAX_TEST_FIRMWARE - extra_count)
         base_count = MAX_TEST_FIRMWARE - extra_count;
      memcpy(table, base, base_count * sizeof(*table));
      table[base_count] = chooser_firmware;
      table[base_count + 1u] = unsupported_launch_firmware;
      table[base_count + 2u] = native_hd_guest_firmware;
      table[base_count + 3u] = native_hd_stress_firmware;
      table[base_count + 4u] = error_box_arbitration_firmware;
      table[base_count + 5u] = error_box_arbitration_mouse_firmware;
      table[base_count + 6u] = error_box_arbitration_after_firmware;
      memcpy(table+base_count+7u,launch_outcome_firmware,
             sizeof(launch_outcome_firmware));
      memcpy(table+base_count+7u+sizeof(launch_outcome_firmware)/
             sizeof(launch_outcome_firmware[0]), disc_request_firmware,
             sizeof(disc_request_firmware));
      table_count = base_count + extra_count;
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
   if (digest && digest->size == native_hd_guest_firmware.size &&
       digest->crc32 == native_hd_guest_firmware.crc32 &&
       !strcmp(digest->sha1, native_hd_guest_firmware.sha1))
      return &native_hd_guest_firmware;
   if (digest && digest->size == native_hd_stress_firmware.size &&
       digest->crc32 == native_hd_stress_firmware.crc32 &&
       !strcmp(digest->sha1, native_hd_stress_firmware.sha1))
      return &native_hd_stress_firmware;
   if (digest && digest->size == error_box_arbitration_firmware.size &&
       digest->crc32 == error_box_arbitration_firmware.crc32 &&
       !strcmp(digest->sha1, error_box_arbitration_firmware.sha1))
      return &error_box_arbitration_firmware;
   if (digest && digest->size == error_box_arbitration_mouse_firmware.size &&
       digest->crc32 == error_box_arbitration_mouse_firmware.crc32 &&
       !strcmp(digest->sha1, error_box_arbitration_mouse_firmware.sha1))
      return &error_box_arbitration_mouse_firmware;
   if (digest && digest->size == error_box_arbitration_after_firmware.size &&
       digest->crc32 == error_box_arbitration_after_firmware.crc32 &&
       !strcmp(digest->sha1, error_box_arbitration_after_firmware.sha1))
      return &error_box_arbitration_after_firmware;
   for (unsigned i=0u;i<sizeof(launch_outcome_firmware)/
                         sizeof(launch_outcome_firmware[0]);++i)
      if (digest && digest->size == launch_outcome_firmware[i].size &&
          digest->crc32 == launch_outcome_firmware[i].crc32 &&
          !strcmp(digest->sha1,launch_outcome_firmware[i].sha1))
         return &launch_outcome_firmware[i];
   for (unsigned i = 0u; i < sizeof(disc_request_firmware)/
                              sizeof(disc_request_firmware[0]); ++i)
      if (digest && digest->size == disc_request_firmware[i].size &&
          digest->crc32 == disc_request_firmware[i].crc32 &&
          !strcmp(digest->sha1, disc_request_firmware[i].sha1))
         return &disc_request_firmware[i];
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
