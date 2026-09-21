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

typedef struct {
   uint32_t state[8];
   uint64_t length;
   uint8_t block[64];
   size_t used;
} ArcSha256;

static uint32_t arc_rotr32(uint32_t value, unsigned shift)
{ return (value >> shift) | (value << (32u - shift)); }

static void arc_sha256_transform(ArcSha256 *ctx, const uint8_t block[64])
{
   static const uint32_t k[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
   };
   uint32_t w[64];
   for (unsigned i = 0; i < 16u; ++i)
      w[i] = ((uint32_t)block[i*4u]<<24)|((uint32_t)block[i*4u+1u]<<16)|
             ((uint32_t)block[i*4u+2u]<<8)|block[i*4u+3u];
   for (unsigned i = 16u; i < 64u; ++i)
   {
      uint32_t s0=arc_rotr32(w[i-15u],7)^arc_rotr32(w[i-15u],18)^(w[i-15u]>>3);
      uint32_t s1=arc_rotr32(w[i-2u],17)^arc_rotr32(w[i-2u],19)^(w[i-2u]>>10);
      w[i]=w[i-16u]+s0+w[i-7u]+s1;
   }
   uint32_t a=ctx->state[0],b=ctx->state[1],c=ctx->state[2],d=ctx->state[3];
   uint32_t e=ctx->state[4],f=ctx->state[5],g=ctx->state[6],h=ctx->state[7];
   for (unsigned i=0;i<64u;++i)
   {
      uint32_t s1=arc_rotr32(e,6)^arc_rotr32(e,11)^arc_rotr32(e,25);
      uint32_t ch=(e&f)^((~e)&g), t1=h+s1+ch+k[i]+w[i];
      uint32_t s0=arc_rotr32(a,2)^arc_rotr32(a,13)^arc_rotr32(a,22);
      uint32_t maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
      h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
   }
   ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
   ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void arc_sha256_hex(const void *data, size_t size, char output[65])
{
   static const char hex[]="0123456789abcdef";
   ArcSha256 ctx={{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                   0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},0u,{0},0u};
   const uint8_t *p=data; ctx.length=(uint64_t)size*8u;
   while(size){size_t n=64u-ctx.used;if(n>size)n=size;memcpy(ctx.block+ctx.used,p,n);ctx.used+=n;p+=n;size-=n;if(ctx.used==64u){arc_sha256_transform(&ctx,ctx.block);ctx.used=0u;}}
   ctx.block[ctx.used++]=0x80u;
   if(ctx.used>56u){memset(ctx.block+ctx.used,0,64u-ctx.used);arc_sha256_transform(&ctx,ctx.block);ctx.used=0u;}
   memset(ctx.block+ctx.used,0,56u-ctx.used);
   for(unsigned i=0;i<8u;++i)ctx.block[63u-i]=(uint8_t)(ctx.length>>(i*8u));
   arc_sha256_transform(&ctx,ctx.block);
   for(unsigned i=0;i<32u;++i){uint8_t v=(uint8_t)(ctx.state[i/4u]>>(24u-8u*(i%4u)));output[i*2u]=hex[v>>4];output[i*2u+1u]=hex[v&15u];}
   output[64]='\0';
}
