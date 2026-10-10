/* ArcDroid authored SHA2/CRC acceleration and runtime dispatch. GPL-2.0-only.
 * Instruction semantics: Arm ACLE and Intel SHA256RNDS2.
 * Every optional instruction is confined to a target-attributed function. */
#include "hash_crc_slices.h"
#if defined(__aarch64__) || defined(__arm64__)
#define ARC_HASH_ARM 1
#include <arm_neon.h>
#include <arm_acle.h>
#if defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#if defined(__clang__)
#define ARC_SHA_TARGET __attribute__((target("sha2")))
#define ARC_CRC_TARGET __attribute__((target("crc")))
#else
#define ARC_SHA_TARGET __attribute__((target("+crypto")))
#define ARC_CRC_TARGET __attribute__((target("+crc")))
#endif
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define ARC_HASH_X86 1
#include <immintrin.h>
#include <cpuid.h>
#endif

static unsigned arc_hash_detect(void)
{
    unsigned f=ARC_HASH_PORTABLE|ARC_HASH_SLICE8;
#if defined(ARC_HASH_ARM)
#if defined(__linux__)
    unsigned long caps=getauxval(AT_HWCAP);
    if(caps&HWCAP_SHA2)f|=ARC_HASH_ARM_SHA2;
    if(caps&HWCAP_CRC32)f|=ARC_HASH_ARM_CRC32;
#elif defined(__APPLE__)
    int value=0;size_t n=sizeof(value);
    if(!sysctlbyname("hw.optional.arm.FEAT_SHA256",&value,&n,NULL,0)&&value)f|=ARC_HASH_ARM_SHA2;
    value=0;n=sizeof(value);
    if(!sysctlbyname("hw.optional.arm.FEAT_CRC32",&value,&n,NULL,0)&&value)f|=ARC_HASH_ARM_CRC32;
#endif
#elif defined(ARC_HASH_X86)
    unsigned a,b,c,d;
    if(__get_cpuid_count(7,0,&a,&b,&c,&d)&&(b&(1u<<29)))f|=ARC_HASH_X86_SHA;
#endif
    return f;
}
static atomic_uint arc_hash_capabilities;
static unsigned arc_hash_available(void)
{
    unsigned f=atomic_load_explicit(&arc_hash_capabilities,memory_order_relaxed);
    if(!f){f=arc_hash_detect();atomic_store_explicit(&arc_hash_capabilities,f,memory_order_relaxed);}
    return f;
}
#ifdef ARCDROID_HASH_TEST
/* The standalone identity executable is single-threaded. No TLS runtime or
 * test selection exists in the shipping core. */
static unsigned arc_hash_selection;
unsigned arc_hash_test_available(void) { return arc_hash_available(); }
bool arc_hash_test_select(unsigned implementation)
{
    if(implementation && (implementation&(implementation-1)))return false;
    if(implementation && !(implementation&arc_hash_available()))return false;
    arc_hash_selection=implementation;return true;
}
static unsigned arc_hash_selected(void)
{ return arc_hash_selection?arc_hash_selection:arc_hash_available(); }
#else
#define arc_hash_selected arc_hash_available
#endif

static uint32_t arc_crc32_slice8(const void *data,size_t n)
{
    const uint8_t *p=data;uint32_t c=UINT32_MAX;
    while(n>=8){
        /* Byte assembly is endian independent and permits every alignment. */
        uint32_t a=c^((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24);
        c=crc_slices[7][a&255]^crc_slices[6][a>>8&255]^crc_slices[5][a>>16&255]^crc_slices[4][a>>24]^
          crc_slices[3][p[4]]^crc_slices[2][p[5]]^crc_slices[1][p[6]]^crc_slices[0][p[7]];
        p+=8;n-=8;
    }
    while(n--){c=table[(c^*p++)&255]^(c>>8);}
    return ~c;
}
#if defined(ARC_HASH_ARM)
ARC_CRC_TARGET static uint32_t arc_crc32_arm(const void *data,size_t n)
{
    const uint8_t *p=data;uint32_t c=UINT32_MAX;
    while(n>=8){uint64_t v;memcpy(&v,p,8);
#if defined(__AARCH64EB__)
        v=__builtin_bswap64(v);
#endif
        c=__crc32d(c,v);p+=8;n-=8;}
    while(n--)c=__crc32b(c,*p++);
    return ~c; /* IEEE CRC32, not the different CRC32C polynomial. */
}
ARC_SHA_TARGET static void arc_sha256_arm(ArcSha256 *ctx,const uint8_t *p)
{
    uint32x4_t a=vld1q_u32(ctx->state),e=vld1q_u32(ctx->state+4),saved_a=a,saved_e=e,m[4];
    for(unsigned i=0;i<4;++i){
        uint8x16_t bytes=vld1q_u8(p+16*i);
#if !defined(__AARCH64EB__)
        bytes=vrev32q_u8(bytes);
#endif
        m[i]=vreinterpretq_u32_u8(bytes);
    }
    for(unsigned i=0;i<16;++i){unsigned j=i&3;uint32x4_t old_a=a;
        uint32x4_t wk=vaddq_u32(m[j],vld1q_u32(sha256_k+4*i));
        a=vsha256hq_u32(a,e,wk);e=vsha256h2q_u32(e,old_a,wk);
        if(i<12)m[j]=vsha256su1q_u32(vsha256su0q_u32(m[j],m[(j+1)&3]),m[(j+2)&3],m[(j+3)&3]);
    }
    vst1q_u32(ctx->state,vaddq_u32(a,saved_a));vst1q_u32(ctx->state+4,vaddq_u32(e,saved_e));
}
#endif
#if defined(ARC_HASH_X86)
__attribute__((target("sha"))) static void arc_sha256_x86(ArcSha256 *ctx,const uint8_t *p)
{
    uint32_t w[64];
    for(unsigned i=0;i<16;++i)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(unsigned i=16;i<64;++i){uint32_t x=w[i-15],y=w[i-2];
        w[i]=w[i-16]+(arc_rotr32(x,7)^arc_rotr32(x,18)^(x>>3))+w[i-7]+(arc_rotr32(y,17)^arc_rotr32(y,19)^(y>>10));}
    /* SHA256RNDS2 registers are ABEF / CDGH, high to low. */
    __m128i a=_mm_set_epi32(ctx->state[0],ctx->state[1],ctx->state[4],ctx->state[5]);
    __m128i b=_mm_set_epi32(ctx->state[2],ctx->state[3],ctx->state[6],ctx->state[7]);
    __m128i saved_a=a,saved_b=b;
    for(unsigned i=0;i<64;i+=4){__m128i wk=_mm_add_epi32(_mm_loadu_si128((const __m128i *)(const void *)(w+i)),
            _mm_loadu_si128((const __m128i *)(const void *)(sha256_k+i)));
        b=_mm_sha256rnds2_epu32(b,a,wk);wk=_mm_shuffle_epi32(wk,0x0e);a=_mm_sha256rnds2_epu32(a,b,wk);}
    uint32_t x[4],y[4];_mm_storeu_si128((__m128i *)(void *)x,_mm_add_epi32(a,saved_a));
    _mm_storeu_si128((__m128i *)(void *)y,_mm_add_epi32(b,saved_b));
    ctx->state[0]=x[3];ctx->state[1]=x[2];ctx->state[2]=y[3];ctx->state[3]=y[2];
    ctx->state[4]=x[1];ctx->state[5]=x[0];ctx->state[6]=y[1];ctx->state[7]=y[0];
}
#endif
uint32_t arc_crc32(const void *data,size_t size)
{
    unsigned f=arc_hash_selected();
#if defined(ARC_HASH_ARM)
    if(f&ARC_HASH_ARM_CRC32)return arc_crc32_arm(data,size);
#endif
    if(f&ARC_HASH_SLICE8)return arc_crc32_slice8(data,size);
    return arc_crc32_portable(data,size);
}
typedef void (*ArcSha256Transform)(ArcSha256 *,const uint8_t *);
static ArcSha256Transform arc_sha256_dispatch(void)
{
    unsigned f=arc_hash_selected();
#if defined(ARC_HASH_ARM)
    if(f&ARC_HASH_ARM_SHA2)return arc_sha256_arm;
#elif defined(ARC_HASH_X86)
    if(f&ARC_HASH_X86_SHA)return arc_sha256_x86;
#endif
    (void)f;return arc_sha256_portable;
}
