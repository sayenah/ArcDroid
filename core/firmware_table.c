/* ArcDroid firmware catalogue. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <string.h>

/* Combined values: CLK ROMCatalogue.cpp @ 9ecb6bf39c12 and verified SHA-1.
 * Component values: MAME aa310.cpp @ ebe671d777af. */
#define C(os_, name_, size_, crc_, sha_) {os_, name_, size_, UINT32_C(crc_), sha_, ARC_RECIPE_COMBINED, 0u}
#define B(os_, name_, size_, crc_, sha_, lane_) {os_, name_, size_, UINT32_C(crc_), sha_, ARC_RECIPE_BYTE_LANES_4, lane_}
#define H(os_, name_, size_, crc_, sha_, lane_) {os_, name_, size_, UINT32_C(crc_), sha_, ARC_RECIPE_HALFWORD_LANES_2, lane_}
static const ArcFirmwareFingerprint fingerprints[] = {
   C(ARC_OS_TEST, "ArcDroid test ROM", 524288u, 0x4330ed83, "f4c9ec049431a3282efbe4bf151ae066320ed592"),
   C(ARC_OS_TEST, "ArcDroid chooser application ROM", 524288u, 0x278555b5, "32f7d800e65fbd26325241b874f9279021c465a9"),
   C(ARC_OS_TEST, "ArcDroid D-format helper visual ROM", 524288u, 0xeeea625e, "13fc935664e628a9c751f3b280346fab60285ccf"),
#if defined(ARCDROID_TEST_FAULTS)
   C(ARC_OS_TEST, "ArcDroid boot prompt test ROM", 524288u, 0xdcf93506, "9b337404a35146da6bafdddb75d27dbfcc6636f8"),
   C(ARC_OS_TEST, "ArcDroid boot write prompt test ROM", 524288u, 0xab9038e9, "e8864fac1301fc3bc47af51179be4ba293e85d21"),
   C(ARC_OS_TEST, "ArcDroid boot CLI test ROM", 524288u, 0x706ad88f, "b31ed6ad314daecb44e6f284172c6c54fc26936b"),
   C(ARC_OS_TEST, "ArcDroid boot confirmation cancel test ROM", 524288u, 0xcdd6d596, "bc0997bb593ca9cf5a6df96e7e39965221644bdd"),
   C(ARC_OS_TEST, "ArcDroid boot horizon test ROM", 524288u, 0xa44198fd, "53edffdd48d51ae9395a346d399a41a68d896463"),
   C(ARC_OS_TEST, "ArcDroid boot CLI then prompt test ROM", 524288u, 0x4839c460, "8b9653d8768eb63a82c521a7dadfdbb985395c64"),
   C(ARC_OS_TEST, "ArcDroid boot desktop test ROM", 524288u, 0xd253068b, "fd67b07a8c60b2c493732b66f268534e7701ca5c"),
   C(ARC_OS_TEST, "ArcDroid boot structural several test ROM", 524288u, 0xebb090d7, "55fbf4a29138068372706ee704b44c7ec950c66c"),
   C(ARC_OS_TEST, "ArcDroid boot structural none test ROM", 524288u, 0x23cda86e, "2fe1d8bee8a5c1ecf53b8ea8c9d80c1653b50cc7"),
   C(ARC_OS_TEST, "ArcDroid boot structural one test ROM", 524288u, 0x48fb686e, "164c7e0dcff9c35c776e52e5fdace47c67b6a5e4"),
   C(ARC_OS_TEST, "ArcDroid boot structural rename test ROM", 524288u, 0xbb4ab91d, "7e9921495ad69730e1002ad0e2a50a9680e24925"),
   C(ARC_OS_TEST, "ArcDroid boot structural sibling test ROM", 524288u, 0xf88049e3, "538cb91da158bee3f340df91f372d27dc9a48a23"),
   /* Synthetic unsupported-OS chooser probe, never accepted by shipping. */
   C(ARC_OS_RISCOS_200, "ArcDroid unsupported launch test ROM", 524288u, 0x92559a84, "6b42001508cbc297b0f792a23f010938c0670326"),
#endif
   C(ARC_OS_TEST, "ArcDroid media exerciser ROM", 524288u, 0x98eb2fb5, "2cd97482b0b64fd2abdc657a1a0143e6556f50f6"),
   C(ARC_OS_TEST, "ArcDroid JFD save test ROM", 524288u, 0x21e5f59b, "6af9d30474d067f9e5c274cc8ffd3449fdab8a62"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO save test ROM", 524288u, 0x8a0fb781, "a5ed887b89711c63484931aeb724a69875342f3a"),
   C(ARC_OS_TEST, "ArcDroid JFD creation test ROM", 524288u, 0xf070775e, "7b625f0db40d5d00200446ead4c059e0d6bf239f"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO creation test ROM", 524288u, 0x39fe6f79, "9f3e796f12ba4bee3c5c4b91a1e2aba1303d343e"),
   C(ARC_OS_TEST, "ArcDroid JFD allocated merge test ROM", 524288u, 0xc865a866, "99d1e5bc6fd9022a2312da7b497f04c944f80ee8"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO allocated merge test ROM", 524288u, 0xa45068ab, "1779db1d1fc2b932f22330bd50156295d9ff39ba"),
   C(ARC_OS_TEST, "ArcDroid JFD missing-slot test ROM", 524288u, 0x8813b8e1, "055879902853ff5e613ce5f67ee1a71f6561c792"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO missing-slot test ROM", 524288u, 0x4215268d, "cc4ebeec0cc1021ed69aa442fd1d47bd7d098656"),
   C(ARC_OS_TEST, "ArcDroid JFD map veto test ROM", 524288u, 0x72400ba4, "c8adc91e35a4795493d655a3dd1c3e6dec28874d"),
   C(ARC_OS_TEST, "ArcDroid JFD map claim test ROM", 524288u, 0xe9264a76, "8f80db72aee166f6160f6f06ec7a3325a4802a6f"),
   C(ARC_OS_TEST, "ArcDroid JFD allocation read test ROM", 524288u, 0x7013e39a, "460d35e0ec64419c58de2b7f770812cbbfcf1b97"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO map veto test ROM", 524288u, 0xcb64b343, "8d56db8967b4083abb86583c164df9478faf5421"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO map claim test ROM", 524288u, 0x62f16f7c, "daa359b1126fe9008af89fa9e3095775eeee5281"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO allocation read test ROM", 524288u, 0x168263c2, "0b521b979b4976225d3a59b7ba3cd9474c8b4f1b"),
   C(ARC_OS_TEST, "ArcDroid JFD probe-write test ROM", 524288u, 0xf119dafe, "66e54bc43feefa96ed88ce7ba9a7bf366a6d3aed"),
   C(ARC_OS_TEST, "ArcDroid JFD New-IO probe-write test ROM", 524288u, 0x206bedb7, "fc0bd1a410da01b40f71130d7d190a6ba2a80f19"),
   C(ARC_OS_TEST, "ArcDroid JFD benign directory write ROM", 524288u, 0xb876f99d, "0f957890cb139a5a0b05958ac5200d25280e7c21"),
   C(ARC_OS_TEST, "ArcDroid JFD directory rename ROM", 524288u, 0x6f93b871, "778eea0fd1a26c826723bb0140cac182875e01cf"),
   C(ARC_OS_RISCOS_311, "RISC OS 3.11", 2097152u, 0x54c0c963, "3487729e87bebc9cb51665838c27beff22f7b3bd"),
   C(ARC_OS_RISCOS_310, "RISC OS 3.10", 2097152u, 0xecac4ea6, "68533e0a93657a879c1697c04f32d517345b1a61"),
   C(ARC_OS_RISCOS_319, "RISC OS 3.19", 2097152u, 0x00c7a3d3, "be7a8cba5d6c6c0e1c4838712524056cf4b8c8cb"),
   C(ARC_OS_RISCOS_300, "RISC OS 3.00", 2097152u, 0xbfc99817, "b4fa494d51dd704c9da9653c2acc6acd08cc8cf4"),
   C(ARC_OS_RISCOS_201, "RISC OS 2.01", 524288u, 0x7cb5ea3f, "9ed7dee8553d96a6d58dd23d31682234f57beb62"),
   C(ARC_OS_RISCOS_200, "RISC OS 2.00", 524288u, 0x89c4ad36, "b82a78830dac386f9b649b6d32a34f9c6910546d"),
   C(ARC_OS_ARTHUR_120, "Arthur 1.20", 524288u, 0xeb3fda57, "1181ff9c2c2f3d6d414054ec33b2260404bafc81"),
   C(ARC_OS_ARTHUR_030, "Arthur 0.30", 524288u, 0x5df8ed42, "6aebd686d97dfdf6726fa5f3246ef35b840b286d"),

   B(ARC_OS_RISCOS_311, "RISC OS 3.11 lane 0", 524288u, 0x84185879, "2740312b32e9cb8ca6cba9f7b33b68dc0dfab810", 0u),
   B(ARC_OS_RISCOS_311, "RISC OS 3.11 lane 1", 524288u, 0xc7584553, "144f8f55f06d6d0752f2f989f4f5c7cec38a43ea", 1u),
   B(ARC_OS_RISCOS_311, "RISC OS 3.11 lane 2", 524288u, 0xff5acf17, "f9c9d4eb2f465b44353257594e631d0e3706f651", 2u),
   B(ARC_OS_RISCOS_311, "RISC OS 3.11 lane 3", 524288u, 0xe2a3480e, "5b48e8b66ba86568e2225d60f34e201dd5f5d52a", 3u),
   H(ARC_OS_RISCOS_311, "RISC OS 3.11 halfword 0", 1048576u, 0x552fc3aa, "b2f1911e53d7377f2e69e1a870139745d3df494b", 0u),
   H(ARC_OS_RISCOS_311, "RISC OS 3.11 halfword 1", 1048576u, 0x308d5a4a, "b309e1dd85670a06d77ec504dbbec6c42336329f", 1u),
   B(ARC_OS_RISCOS_310, "RISC OS 3.10 lane 0", 524288u, 0xb7499ef8, "4ab53a53c531bfbecdd441c82d9f4c0517682dde", 0u),
   B(ARC_OS_RISCOS_310, "RISC OS 3.10 lane 1", 524288u, 0xd55a854c, "c9308cee92cca2a626d8577ec99485ad58b8da2a", 1u),
   B(ARC_OS_RISCOS_310, "RISC OS 3.10 lane 2", 524288u, 0x19bc549a, "88b02bd3df94b56284ffad1c24fa140249b7cb63", 2u),
   B(ARC_OS_RISCOS_310, "RISC OS 3.10 lane 3", 524288u, 0xbf86f497, "a200dca6dbee7c0be25a7e5363a6a3e6455a3bf3", 3u),
   H(ARC_OS_RISCOS_319, "RISC OS 3.19 halfword 0", 1048576u, 0x8aaf7ff3, "bc00d90842f40259a48d8f0627d4129e2fa766fe", 0u),
   H(ARC_OS_RISCOS_319, "RISC OS 3.19 halfword 1", 1048576u, 0x0ddc807e, "b0fdb33869cc593123a04fe959c1528f76aac0b9", 1u),
   B(ARC_OS_RISCOS_300, "RISC OS 3.00 lane 0", 524288u, 0x023115a9, "d3233f76d5750e04ef2bc39d5b2dfd96e6a03c45", 0u),
   B(ARC_OS_RISCOS_300, "RISC OS 3.00 lane 1", 524288u, 0x6db01129, "4b801dcce4d268d5e4c2680efa23acb29e4f907f", 1u),
   B(ARC_OS_RISCOS_300, "RISC OS 3.00 lane 2", 524288u, 0xd749a9f2, "c53c35b847d300989163f9e779590d1853e8adaf", 2u),
   B(ARC_OS_RISCOS_300, "RISC OS 3.00 lane 3", 524288u, 0x5b13c523, "b815bdf31dd99e5b4f2d99790cc28ee8cd907b43", 3u),
   B(ARC_OS_RISCOS_201, "RISC OS 2.01 lane 0", 131072u, 0x29e2890b, "2ccdbda7494824180426d66cd38659f6ee55a045", 0u),
   B(ARC_OS_RISCOS_201, "RISC OS 2.01 lane 1", 131072u, 0xdd1e4893, "2d39a5027fd164fd9409e38074c68731622406bb", 1u),
   B(ARC_OS_RISCOS_201, "RISC OS 2.01 lane 2", 131072u, 0x985a8703, "87376fba36757b311f6c4178c2ac04d8a4ad063a", 2u),
   B(ARC_OS_RISCOS_201, "RISC OS 2.01 lane 3", 131072u, 0xf23e4c8d, "27595da90d76d3b25b55e176e0688740c5ce39de", 3u),
   B(ARC_OS_RISCOS_200, "RISC OS 2.00 lane 0", 131072u, 0x24291ebf, "758adaf6f73b4041a680cdf9a0b2107da12ca5a0", 0u),
   B(ARC_OS_RISCOS_200, "RISC OS 2.00 lane 1", 131072u, 0x44a134f1, "2db7f06e692c3191b2e131d55a1cf997e226c7c6", 1u),
   B(ARC_OS_RISCOS_200, "RISC OS 2.00 lane 2", 131072u, 0x997f42b6, "779fcb13ce4107c27a003cdc848e8a5b7e039268", 2u),
   B(ARC_OS_RISCOS_200, "RISC OS 2.00 lane 3", 131072u, 0x6335dba2, "0e0631ee43acf3d005f24a4d5c11c5c60e6f29a2", 3u),
   B(ARC_OS_ARTHUR_120, "Arthur 1.20 lane 0", 131072u, 0x03bfe550, "e4f3f1e37b84e716d75a32aa291a9189371daa1c", 0u),
   B(ARC_OS_ARTHUR_120, "Arthur 1.20 lane 1", 131072u, 0x89ece77c, "e1979a8d3586c006e3837ff721cfa5439e6394bc", 1u),
   B(ARC_OS_ARTHUR_120, "Arthur 1.20 lane 2", 131072u, 0x2302db86, "9fb3761571141bd51936daf03fa4f94fca177356", 2u),
   B(ARC_OS_ARTHUR_120, "Arthur 1.20 lane 3", 131072u, 0x1546a0da, "078ef64aa8a7196d0ee4bca9926a74ade0a36173", 3u),
   B(ARC_OS_ARTHUR_030, "Arthur 0.30 lane 0", 131072u, 0xe6862d4c, "13d8470f1cb2c1d15530bc7fa8a95ecc4a371cf3", 0u),
   B(ARC_OS_ARTHUR_030, "Arthur 0.30 lane 1", 131072u, 0xa9aeb4cf, "f37e744ba0e48861815683b24612b0dd69d6ea8b", 1u),
   B(ARC_OS_ARTHUR_030, "Arthur 0.30 lane 2", 131072u, 0x7a175186, "efdee41d9811b05a726af9ba135e0178bb1749e5", 2u),
   B(ARC_OS_ARTHUR_030, "Arthur 0.30 lane 3", 131072u, 0x7a8811b8, "7a6b5d7bb94ff5e2a31469ab024c8b2d4d295709", 3u)
};
#undef C
#undef B
#undef H

const ArcFirmwareFingerprint *arc_firmware_table(size_t *count)
{
   if (count) *count = sizeof(fingerprints) / sizeof(fingerprints[0]);
   return fingerprints;
}

const ArcFirmwareFingerprint *arc_firmware_match(const ArcDigest *digest)
{
   if (!digest) return NULL;
   for (size_t i = 0; i < sizeof(fingerprints) / sizeof(fingerprints[0]); ++i)
      if (digest->size == fingerprints[i].size && digest->crc32 == fingerprints[i].crc32 &&
          strcmp(digest->sha1, fingerprints[i].sha1) == 0)
         return &fingerprints[i];
   return NULL;
}

unsigned arc_firmware_auto_priority(ArcOsClass os)
{
   switch (os)
   {
      case ARC_OS_RISCOS_311: return 1u;
      case ARC_OS_RISCOS_310: return 2u;
      case ARC_OS_RISCOS_319: return 3u;
      case ARC_OS_RISCOS_300: return 4u;
      case ARC_OS_RISCOS_200: return 5u;
      case ARC_OS_ARTHUR_120: return 6u;
      case ARC_OS_RISCOS_201: return 7u;
      case ARC_OS_ARTHUR_030: return 8u;
      case ARC_OS_TEST: return 9u;
      default: return 100u;
   }
}

bool arc_firmware_nearest_size(size_t actual, size_t margin, size_t *expected)
{
   size_t nearest = 0u;
   size_t nearest_delta = SIZE_MAX;
   for (size_t i = 0; i < sizeof(fingerprints) / sizeof(fingerprints[0]); ++i)
   {
      size_t candidate = fingerprints[i].size;
      size_t delta = actual > candidate ? actual - candidate : candidate - actual;
      if (delta < nearest_delta)
      {
         nearest = candidate;
         nearest_delta = delta;
      }
   }
   if (nearest_delta > margin) return false;
   if (expected) *expected = nearest;
   return true;
}

const char *arc_os_name(ArcOsClass os)
{
   switch (os)
   {
      case ARC_OS_TEST: return "ArcDroid test ROM";
      case ARC_OS_RISCOS_311: return "RISC OS 3.11";
      case ARC_OS_RISCOS_310: return "RISC OS 3.10";
      case ARC_OS_RISCOS_319: return "RISC OS 3.19";
      case ARC_OS_RISCOS_300: return "RISC OS 3.00";
      case ARC_OS_RISCOS_201: return "RISC OS 2.01";
      case ARC_OS_RISCOS_200: return "RISC OS 2.00";
      case ARC_OS_ARTHUR_120: return "Arthur 1.20";
      case ARC_OS_ARTHUR_030: return "Arthur 0.30";
      default: return "unknown firmware";
   }
}

bool arc_firmware_assemble(const uint8_t *const *parts, const size_t *sizes,
                           unsigned count, ArcFirmwareRecipe recipe,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size)
{
   unsigned stride = recipe == ARC_RECIPE_BYTE_LANES_4 ? 4u :
                     recipe == ARC_RECIPE_HALFWORD_LANES_2 ? 2u : 1u;
   if (!parts || !sizes || !output || count != stride || stride == 1u)
   {
      if (error && error_size) snprintf(error, error_size, "invalid firmware assembly recipe");
      return false;
   }
   size_t part_size = sizes[0];
   if (part_size > SIZE_MAX / stride || output_size != part_size * stride)
   {
      if (error && error_size) snprintf(error, error_size, "firmware output size mismatch");
      return false;
   }
   for (unsigned lane = 0; lane < count; ++lane)
      if (!parts[lane] || sizes[lane] != part_size)
      {
         if (error && error_size) snprintf(error, error_size, "firmware lane size mismatch");
         return false;
      }
   if (recipe == ARC_RECIPE_BYTE_LANES_4)
      for (size_t i = 0; i < part_size; ++i)
         for (unsigned lane = 0; lane < 4u; ++lane) output[i * 4u + lane] = parts[lane][i];
   else
      for (size_t i = 0; i < part_size; i += 2u)
         for (unsigned lane = 0; lane < 2u; ++lane)
         {
            output[(i / 2u) * 4u + lane * 2u] = parts[lane][i];
            output[(i / 2u) * 4u + lane * 2u + 1u] = parts[lane][i + 1u];
         }
   return true;
}
