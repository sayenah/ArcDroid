/* Authored SHA-256/IEEE CRC-32 identity and throughput probes. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "hash_accel.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__x86_64__)
#include <cpuid.h>
#endif
uint64_t arc_test_clock_ns(void);
static uint64_t random_state=UINT64_C(0xc231e76a209d4b85);
static uint32_t random_word(void)
{ random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return (uint32_t)random_state; }
static const char *name(unsigned mode)
{ switch(mode){case 0:return "auto";case 1:return "portable";case 2:return "slice8";
  case 4:return "arm-sha2";case 8:return "arm-crc32";case 16:return "x86-sha";default:return "invalid";} }
static void known(void)
{
    const char *input[]={"","abc","abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[]={"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for(unsigned i=0;i<3;++i){char hex[65];arc_sha256_hex(input[i],strlen(input[i]),hex);assert(!strcmp(hex,expected[i]));}
    char *million=malloc(1000000);assert(million);memset(million,'a',1000000);char hex[65];
    arc_sha256_hex(million,1000000,hex);assert(!strcmp(hex,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));free(million);
    assert(arc_crc32(NULL,0)==0&&arc_crc32("123456789",9)==UINT32_C(0xcbf43926));
}
static void compare(unsigned mode,const uint8_t *p,size_t n)
{
    uint8_t expected[32],actual[32];ArcSha256 a,b;
    assert(arc_hash_test_select(ARC_HASH_PORTABLE));uint32_t crc=arc_crc32(p,n);
    arc_sha256_init(&a);arc_sha256_update(&a,p,n);arc_sha256_final(&a,expected);
    assert(arc_hash_test_select(mode));assert(arc_crc32(p,n)==crc);
    arc_sha256_init(&b);arc_sha256_update(&b,p,n);arc_sha256_final(&b,actual);assert(!memcmp(expected,actual,32));
    /* Unaligned fragments, empty updates, copied context, final twice and
     * continued append after final cover the journal's snapshot contract. */
    arc_sha256_init(&b);size_t at=0;
    while(at<n){size_t take=1+random_word()%173;if(take>n-at)take=n-at;
        arc_sha256_update(&b,p+at,take);at+=take;arc_sha256_update(&b,NULL,0);}
    ArcSha256 snapshot=b;arc_sha256_final(&b,actual);assert(!memcmp(&b,&snapshot,sizeof(b)));
    assert(!memcmp(expected,actual,32));arc_sha256_final(&snapshot,actual);assert(!memcmp(expected,actual,32));
    arc_sha256_update(&b,"tail",4);arc_sha256_final(&b,actual);
    assert(arc_hash_test_select(ARC_HASH_PORTABLE));arc_sha256_update(&a,"tail",4);arc_sha256_final(&a,expected);assert(!memcmp(expected,actual,32));
}
int main(int argc,char **argv)
{
    unsigned available=arc_hash_test_available();bool advertised=false;
#if defined(__x86_64__)
    unsigned a,b,c,d;advertised=__get_cpuid_count(7,0,&a,&b,&c,&d)&&(b&(1u<<29));
#endif
    assert(arc_hash_test_select(0));unsigned selected=arc_hash_test_sha_selected();
    printf("hash capabilities=0x%x x86_SHA_advertised=%u auto_SHA=%s\n",available,advertised,name(selected));fflush(stdout);
    if(argc>1&&!strcmp(argv[1],"--require-x86")&&advertised&&
       (!(available&ARC_HASH_X86_SHA)||selected!=ARC_HASH_X86_SHA)){
        fputs("Advertised x86 SHA-NI was not selected\n",stderr);return 2;}
    if(argc>1&&!strcmp(argv[1],"--require-arm") &&
       (available&(ARC_HASH_ARM_SHA2|ARC_HASH_ARM_CRC32))!=(ARC_HASH_ARM_SHA2|ARC_HASH_ARM_CRC32)){
        fputs("Required arm64 SHA2/CRC32 paths unavailable\n",stderr);return 2;}
    uint8_t *p=malloc(1048576+64);assert(p);for(unsigned i=0;i<1048576+64;++i)p[i]=(uint8_t)random_word();
    for(unsigned mode=0;mode<=16;mode=mode?mode*2:1){
        if(mode&&!(mode&available)){printf("%s: unavailable (not executed)\n",name(mode));continue;}
        assert(arc_hash_test_select(mode));known();
        if(argc>1&&!strcmp(argv[1],"--bench")){
            volatile uint32_t sink=0;char hex[65];unsigned loops=128;uint64_t start=arc_test_clock_ns();
            for(unsigned i=0;i<loops;++i)sink^=arc_crc32(p,262144);
            uint64_t crc_ns=arc_test_clock_ns()-start;start=arc_test_clock_ns();
            for(unsigned i=0;i<loops;++i)arc_sha256_hex(p,262144,hex);
            uint64_t sha_ns=arc_test_clock_ns()-start;
            printf("%s bytes=%u crc_ns=%" PRIu64 " sha_ns=%" PRIu64 " crc_MiB_s=%.3f sha_MiB_s=%.3f sink=%x/%s\n",
                name(mode),loops*262144,crc_ns,sha_ns,32e9/crc_ns,32e9/sha_ns,(unsigned)sink,hex);
            continue;
        }
        unsigned cases=0;
        for(size_t length=0;length<=1024;++length)for(unsigned alignment=0;alignment<64;++alignment){compare(mode,p+alignment,length);++cases;}
        const size_t edges[]={2047,2048,2049,4095,4096,4097,65535,65536,65537,131071,131072,131168,262143,262144,262145,262276,1048575,1048576};
        for(unsigned i=0;i<sizeof(edges)/sizeof(*edges);++i)for(unsigned alignment=0;alignment<64;++alignment){compare(mode,p+alignment,edges[i]);++cases;}
        for(unsigned i=0;i<512;++i){compare(mode,p+random_word()%64,random_word()%262277);++cases;}
        printf("%s: known answers and %u differential length/alignment/stream cases pass\n",name(mode),cases);
    }
    assert(!arc_hash_test_select(3)&&!arc_hash_test_select(32));assert(arc_hash_test_select(0));free(p);return 0;
}
