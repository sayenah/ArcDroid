/* Invented FileCore owned-scope reader tests. GPL-2.0-only. */
#include "hard_disc_scope.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { MAP_AT=(2*4064-480)*1024, MAP_SIZE=5*512,
       OWNED_AT=MAP_AT+16*1024, RECORD_AT=MAP_AT+32*1024,
       COPY_AT=MAP_AT+48*1024, PRODUCT_AT=MAP_AT+64*1024 };
static void put24(uint8_t *p, unsigned v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u); p[2]=(uint8_t)(v>>16u); }
static void put32(uint8_t *p, unsigned v)
{ put24(p,v); p[3]=(uint8_t)(v>>24u); }
static void set_bits(uint8_t *p, unsigned at, unsigned count, unsigned value)
{
   for (unsigned i=0u; i<count; ++i) {
      uint8_t mask=(uint8_t)(1u<<((at+i)&7u));
      if ((value>>i)&1u) p[(at+i)>>3u]|=mask;
      else p[(at+i)>>3u]&=(uint8_t)~mask;
   }
}
static uint8_t zone_check(const uint8_t *block)
{
   unsigned s[4]={0};
   for (int at=508; at>0; at-=4) {
      s[0]+=block[at]+(s[3]>>8u); s[3]&=255u;
      s[1]+=block[at+1]+(s[0]>>8u); s[0]&=255u;
      s[2]+=block[at+2]+(s[1]>>8u); s[1]&=255u;
      s[3]+=block[at+3]+(s[2]>>8u); s[2]&=255u;
   }
   s[0]+=s[3]>>8u;
   s[1]+=block[1]+(s[0]>>8u);
   s[2]+=block[2]+(s[1]>>8u);
   s[3]+=block[3]+(s[2]>>8u);
   return (uint8_t)(s[0]^s[1]^s[2]^s[3]);
}
static void allocations(uint8_t *image, unsigned count)
{
   uint8_t *map=image+MAP_AT;
   uint8_t *zone=map+2*512u;
   for (unsigned i=0u; i<count; ++i) {
      unsigned start=48u+i*16u;
      set_bits(zone,start,15u,3u+i);
      set_bits(zone,start+15u,1u,1u);
   }
   set_bits(zone,8u,15u,48u+count*16u-8u);
   zone[0]=zone_check(zone);
   memcpy(map+MAP_SIZE,map,MAP_SIZE);
}
static unsigned ror13(unsigned v)
{ return (v>>13u)|(v<<19u); }
static void finish(uint8_t *d, unsigned count)
{
   size_t used=5u+count*26u, words=used&~(size_t)3u;
   unsigned check=0u;
   for (size_t i=0u;i<words;i+=4u)
      check=((unsigned)d[i]|(unsigned)d[i+1]<<8u|
             (unsigned)d[i+2]<<16u|(unsigned)d[i+3]<<24u)^ror13(check);
   for (size_t i=words;i<used;++i) check=d[i]^ror13(check);
   for (size_t i=2008u;i<2044u;i+=4u)
      check=((unsigned)d[i]|(unsigned)d[i+1]<<8u|
             (unsigned)d[i+2]<<16u|(unsigned)d[i+3]<<24u)^ror13(check);
   d[2047]=(uint8_t)(check^(check>>8u)^(check>>16u)^(check>>24u));
}
static void make_dir(uint8_t *d, unsigned parent)
{
   memset(d,0,2048u);
   d[0]=1u; memcpy(d+1,"Nick",4u);
   put24(d+2010,parent);
   d[2032]='$'; memset(d+2033,'\r',9u);
   d[2042]=1u; memcpy(d+2043,"Nick",4u);
   finish(d,0u);
}
static void entry(uint8_t *d, unsigned index, const char *name,
                  unsigned length, unsigned address, bool directory)
{
   uint8_t *e=d+5u+index*26u;
   memset(e,'\r',10u);
   memcpy(e,name,strlen(name));
   put32(e+18u,length);
   put24(e+22u,address);
   e[25]=directory?0x3bu:0x33u;
}
static void record(uint8_t *image, const ArcHdExpected *expected,
                   const char *state, const char *product)
{
   int n=snprintf((char *)image+RECORD_AT,1024u,
      "ARCINST1\rrevision=1\rstate=%s\rattempt=1\rsource=%s\rmedia=%s\r"
      "fingerprint=%s\rcontent=%s\ros=%s\r%s%s%s%s",
      state,expected->source,expected->media,expected->fingerprint,
      expected->content,expected->os,product?"product=":"",
      product?product:"",product?"\r":"",
      !strcmp(state,"failed")?"error=Invented failure\r":"");
   assert(n>0 && n<1024);
   entry(image+OWNED_AT,0u,"Installed",(unsigned)n,0x400u,false);
   memset(image+OWNED_AT+31u,0u,52u);
   finish(image+OWNED_AT,1u);
}

static bool memory_read(void *opaque, uint32_t lba, uint32_t count, uint8_t *out)
{
   if (lba > ARC_INSTALL_HD_SECTORS || count > ARC_INSTALL_HD_SECTORS-lba)
      return false;
   memcpy(out,(uint8_t *)opaque+(size_t)lba*512u,(size_t)count*512u);
   return true;
}
int main(void)
{
   uint8_t *image=malloc(ARC_INSTALL_HD_BYTES);
   assert(image && arc_install_hd_format(image,ARC_INSTALL_HD_BYTES));
   ArcGuestName copy={{'!','M','o','c','k'},5u};
   ArcHdExpected expected={"ADFS::0.$.!Mock",
      "0123456789012345678901234567890123456789",
      "0123456789012345678901234567890123456789",
      "0123456789012345678901234567890123456789","riscos311",&copy};
   ArcHdScope scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_ABSENT);
   allocations(image,4u);
   uint8_t *root=image+MAP_AT+2u*MAP_SIZE;
   entry(root,0u,"ArcDroid",0u,0x300u,false);
   finish(root,1u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_UNOWNED);
   entry(root,0u,"ArcDroid",2048u,0x300u,true);
   finish(root,1u);
   make_dir(image+OWNED_AT,0x20bu);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_UNOWNED);
   entry(image+OWNED_AT,0u,"Installed~",9u,0x400u,false);
   finish(image+OWNED_AT,1u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_UNOWNED);
   record(image,&expected,"pending",NULL);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_PENDING);
   unsigned pending_size=(unsigned)image[OWNED_AT+5u+18u]|
      (unsigned)image[OWNED_AT+5u+19u]<<8u;
   memcpy(image+RECORD_AT+pending_size,"state=completed\r",16u);
   put32(image+OWNED_AT+5u+18u,pending_size+16u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   record(image,&expected,"started",NULL);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_STARTED && !scope.copy_present);
   make_dir(image+COPY_AT,0x300u);
   entry(image+COPY_AT,0u,"!Run",1u,0x500u,false);
   finish(image+COPY_AT,1u);
   entry(image+OWNED_AT,1u,"!Mock",2048u,0x500u,true);
   finish(image+OWNED_AT,2u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_STARTED && scope.copy_present &&
          scope.application_count == 1u);
   make_dir(image+PRODUCT_AT,0x300u);
   entry(image+PRODUCT_AT,0u,"!Run",1u,0x600u,false);
   finish(image+PRODUCT_AT,1u);
   entry(image+OWNED_AT,2u,"!Result",2048u,0x600u,true);
   finish(image+OWNED_AT,3u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_STARTED && scope.copy_present &&
          scope.product_present && scope.application_count == 2u);
   record(image,&expected,"failed",NULL);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_FAILED);
   record(image,&expected,"completed","!Result");
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && !scope.product_present);
   entry(image+OWNED_AT,1u,"!Mock",2048u,0x500u,true);
   entry(image+OWNED_AT,2u,"!Result",2048u,0x600u,true);
   finish(image+OWNED_AT,3u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && scope.product_present);
   entry(image+PRODUCT_AT,1u,"!Run",1u,0x600u,false);
   finish(image+PRODUCT_AT,2u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && !scope.product_present &&
          scope.application_count == 1u);
   entry(image+PRODUCT_AT,1u,"!Boot",1u,0x600u,false);
   entry(image+PRODUCT_AT,2u,"!Boot",1u,0x600u,false);
   finish(image+PRODUCT_AT,3u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && !scope.product_present &&
          scope.application_count == 1u);
   make_dir(image+PRODUCT_AT,0x300u);
   entry(image+PRODUCT_AT,0u,"!Run",1u,0x600u,false);
   entry(image+PRODUCT_AT,1u,"!Boot",1u,0x600u,false);
   finish(image+PRODUCT_AT,2u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && scope.product_present);
   make_dir(image+PRODUCT_AT,0x300u);
   entry(image+PRODUCT_AT,0u,"!Run",1u,0x600u,false);
   finish(image+PRODUCT_AT,1u);
   record(image,&expected,"completed","!Missing");
   entry(image+OWNED_AT,1u,"!Mock",2048u,0x500u,true);
   entry(image+OWNED_AT,2u,"!Result",2048u,0x600u,true);
   finish(image+OWNED_AT,3u);
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_COMPLETED && !scope.product_present &&
          scope.application_count == 2u);
   ((char *)image+RECORD_AT)[0]='X';
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_UNOWNED);
   uint8_t *installed=image+OWNED_AT+5u;
   put32(installed+18u,1025u);
   finish(image+OWNED_AT,3u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_UNOWNED);
   record(image,&expected,"started",NULL);
   put32(installed+18u,1025u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   record(image,&expected,"started",NULL);
   put32(installed+18u,8u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   put32(installed+18u,0u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_UNOWNED);
   record(image,&expected,"started",NULL);
   ((char *)image+RECORD_AT)[40]='X';
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_FOREIGN);
   record(image,&expected,"started",NULL);
   ArcHdExpected wrong=expected;
   wrong.source="ADFS::0.$.!Other";
   assert(arc_hd_scope_read(memory_read,image,&wrong).state==ARC_HD_FOREIGN);
   wrong=expected; wrong.media="ffffffffffffffffffffffffffffffffffffffff";
   assert(arc_hd_scope_read(memory_read,image,&wrong).state==ARC_HD_FOREIGN);
   wrong=expected; wrong.fingerprint="ffffffffffffffffffffffffffffffffffffffff";
   assert(arc_hd_scope_read(memory_read,image,&wrong).state==ARC_HD_FOREIGN);
   wrong=expected; wrong.content="ffffffffffffffffffffffffffffffffffffffff";
   assert(arc_hd_scope_read(memory_read,image,&wrong).state==ARC_HD_FOREIGN);
   wrong=expected; wrong.os="riscos310";
   assert(arc_hd_scope_read(memory_read,image,&wrong).state==ARC_HD_FOREIGN);
   char *revision=strstr((char *)image+RECORD_AT,"revision=1");
   assert(revision); revision[9]='2';
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   record(image,&expected,"started",NULL);
   unsigned record_length=(unsigned)installed[18]|
      (unsigned)installed[19]<<8u;
   put32(installed+18u,record_length-4u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   put32(installed+18u,1025u);
   finish(image+OWNED_AT,1u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_FOREIGN);
   record(image,&expected,"started",NULL);
   image[MAP_AT+MAP_SIZE+2u*512u+90u]^=1u;
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_INDETERMINATE);
   memcpy(image+MAP_AT+MAP_SIZE,image+MAP_AT,MAP_SIZE);
   image[0xc00u] ^= 1u;
   scope=arc_hd_scope_read(memory_read,image,&expected);
   assert(scope.state == ARC_HD_INDETERMINATE);
   image[0xc00u] ^= 1u;
   image[OWNED_AT+2047u]^=1u;
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_INDETERMINATE);
   image[OWNED_AT+2047u]^=1u;
   image[OWNED_AT+2042u]^=1u;
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_INDETERMINATE);
   image[OWNED_AT+2042u]^=1u;
   for (unsigned i=1u;i<=31u;++i) {
      char name[10];
      assert(snprintf(name,sizeof(name),"!X%02u",i)>0);
      entry(image+OWNED_AT,i,name,2048u,0x500u,true);
   }
   finish(image+OWNED_AT,32u);
   assert(arc_hd_scope_read(memory_read,image,&expected).state==ARC_HD_INDETERMINATE);
   free(image);
   return 0;
}
