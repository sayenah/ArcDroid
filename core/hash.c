/* ArcDroid hashing. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <string.h>

static uint32_t arc_rotl32(uint32_t value, unsigned shift)
{
   return (value << shift) | (value >> (32u - shift));
}

uint32_t arc_crc32(const void *data, size_t size)
{
   const uint8_t *bytes = (const uint8_t *)data;
   uint32_t crc = UINT32_C(0xffffffff);
   for (size_t i = 0; i < size; ++i)
   {
      crc ^= bytes[i];
      for (unsigned bit = 0; bit < 8u; ++bit)
         crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
   }
   return ~crc;
}

typedef struct {
   uint32_t state[5];
   uint64_t length;
   uint8_t block[64];
   size_t used;
} ArcSha1;

static void arc_sha1_transform(ArcSha1 *ctx, const uint8_t block[64])
{
   uint32_t words[80];
   for (unsigned i = 0; i < 16u; ++i)
      words[i] = ((uint32_t)block[i * 4u] << 24) |
                 ((uint32_t)block[i * 4u + 1u] << 16) |
                 ((uint32_t)block[i * 4u + 2u] << 8) |
                 block[i * 4u + 3u];
   for (unsigned i = 16u; i < 80u; ++i)
      words[i] = arc_rotl32(words[i - 3u] ^ words[i - 8u] ^ words[i - 14u] ^ words[i - 16u], 1u);

   uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
   uint32_t d = ctx->state[3], e = ctx->state[4];
   for (unsigned i = 0; i < 80u; ++i)
   {
      uint32_t f, k;
      if (i < 20u) { f = (b & c) | ((~b) & d); k = UINT32_C(0x5a827999); }
      else if (i < 40u) { f = b ^ c ^ d; k = UINT32_C(0x6ed9eba1); }
      else if (i < 60u) { f = (b & c) | (b & d) | (c & d); k = UINT32_C(0x8f1bbcdc); }
      else { f = b ^ c ^ d; k = UINT32_C(0xca62c1d6); }
      uint32_t temp = arc_rotl32(a, 5u) + f + e + k + words[i];
      e = d; d = c; c = arc_rotl32(b, 30u); b = a; a = temp;
   }
   ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
   ctx->state[3] += d; ctx->state[4] += e;
}

static void arc_sha1_update(ArcSha1 *ctx, const uint8_t *data, size_t size)
{
   ctx->length += (uint64_t)size * 8u;
   while (size)
   {
      size_t take = 64u - ctx->used;
      if (take > size) take = size;
      memcpy(ctx->block + ctx->used, data, take);
      ctx->used += take; data += take; size -= take;
      if (ctx->used == 64u)
      {
         arc_sha1_transform(ctx, ctx->block);
         ctx->used = 0u;
      }
   }
}

void arc_sha1_hex(const void *data, size_t size, char output[41])
{
   static const char hex[] = "0123456789abcdef";
   ArcSha1 ctx = {{UINT32_C(0x67452301), UINT32_C(0xefcdab89),
                   UINT32_C(0x98badcfe), UINT32_C(0x10325476),
                   UINT32_C(0xc3d2e1f0)}, 0u, {0}, 0u};
   arc_sha1_update(&ctx, (const uint8_t *)data, size);
   ctx.block[ctx.used++] = 0x80u;
   if (ctx.used > 56u)
   {
      memset(ctx.block + ctx.used, 0, 64u - ctx.used);
      arc_sha1_transform(&ctx, ctx.block);
      ctx.used = 0u;
   }
   memset(ctx.block + ctx.used, 0, 56u - ctx.used);
   for (unsigned i = 0; i < 8u; ++i)
      ctx.block[63u - i] = (uint8_t)(ctx.length >> (i * 8u));
   arc_sha1_transform(&ctx, ctx.block);
   for (unsigned i = 0; i < 20u; ++i)
   {
      uint8_t byte = (uint8_t)(ctx.state[i / 4u] >> (24u - 8u * (i % 4u)));
      output[i * 2u] = hex[byte >> 4];
      output[i * 2u + 1u] = hex[byte & 15u];
   }
   output[40] = '\0';
}

ArcDigest arc_digest(const void *data, size_t size)
{
   ArcDigest digest;
   digest.size = size;
   digest.crc32 = arc_crc32(data, size);
   arc_sha1_hex(data, size, digest.sha1);
   return digest;
}
