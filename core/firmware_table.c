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
   C(ARC_OS_TEST, "ArcDroid test ROM", 524288u, 0xb4a464bb, "80a4211d1d8fd28e3e57d766d85cfcf260338771"),
   C(ARC_OS_TEST, "ArcDroid media exerciser ROM", 524288u, 0x24a0d92b, "47a2f9b31057674aad50ecdb04517a1373137ccc"),
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
