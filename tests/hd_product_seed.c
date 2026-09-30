/* Invented completed hard-disc fixture, written through the normal store.
 * Linked only into the test core. GPL-2.0-only. */
#include "hard_disc_scope.h"
#include "install_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAP_AT=(2*4064-480)*1024, MAP_SIZE=5*512,
       OWNED_AT=MAP_AT+16*1024, RECORD_AT=MAP_AT+32*1024,
       COPY_AT=MAP_AT+48*1024, PRODUCT_AT=MAP_AT+64*1024,
       RUN_AT=MAP_AT+80*1024, COPY_RUN_AT=MAP_AT+96*1024 };
static void put24(uint8_t *p, unsigned v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u); p[2]=(uint8_t)(v>>16u); }
static void put32(uint8_t *p, unsigned v)
{ put24(p,v); p[3]=(uint8_t)(v>>24u); }
static void set_bits(uint8_t *p, unsigned at, unsigned count, unsigned value)
{
   for (unsigned i=0u;i<count;++i) {
      uint8_t mask=(uint8_t)(1u<<((at+i)&7u));
      if ((value>>i)&1u) p[(at+i)>>3u]|=mask;
      else p[(at+i)>>3u]&=(uint8_t)~mask;
   }
}
static uint8_t zone_check(const uint8_t *block)
{
   unsigned s[4]={0};
   for (int at=508;at>0;at-=4) {
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
   memset(d,0,2048u); d[0]=1u; memcpy(d+1,"Nick",4u);
   put24(d+2010,parent); d[2032]='$'; memset(d+2033,'\r',9u);
   d[2042]=1u; memcpy(d+2043,"Nick",4u); finish(d,0u);
}
static void entry(uint8_t *d, unsigned index, const char *name,
                  unsigned length, unsigned address, bool directory)
{
   uint8_t *e=d+5u+index*26u;
   memset(e,'\r',10u); memcpy(e,name,strlen(name));
   put32(e+18u,length); put24(e+22u,address);
   e[25]=directory?0x3bu:0x33u;
}
static bool seed_scope(ArcInstallStore *store,
                       const ArcHdExpected *expected, unsigned variant)
{
   if (!store || !expected || !expected->copy || variant > 3u ||
       !arc_install_store_writable(store)) return false;
   uint8_t *image=malloc(ARC_INSTALL_HD_BYTES);
   if (!image) return false;
   memcpy(image,arc_install_store_image(store),ARC_INSTALL_HD_BYTES);
   uint8_t *map=image+MAP_AT, *zone=map+2u*512u;
   for (unsigned i=0u;i<6u;++i) {
      unsigned start=48u+i*16u;
      set_bits(zone,start,15u,3u+i);
      set_bits(zone,start+15u,1u,1u);
   }
   set_bits(zone,8u,15u,48u+6u*16u-8u);
   zone[0]=zone_check(zone);
   memcpy(map+MAP_SIZE,map,MAP_SIZE);
   uint8_t *root=image+MAP_AT+2u*MAP_SIZE;
   entry(root,0u,"ArcDroid",2048u,0x300u,true); finish(root,1u);
   make_dir(image+OWNED_AT,0x20bu);
   static const char run[]="*Echo Invented fixture product\r";
   memcpy(image+RUN_AT,run,sizeof(run)-1u);
   memcpy(image+COPY_RUN_AT,run,sizeof(run)-1u);
   make_dir(image+COPY_AT,0x300u);
   entry(image+COPY_AT,0u,"!Run",sizeof(run)-1u,0x800u,false);
   finish(image+COPY_AT,1u);
   make_dir(image+PRODUCT_AT,0x300u);
   entry(image+PRODUCT_AT,0u,"!Run",sizeof(run)-1u,0x700u,false);
   finish(image+PRODUCT_AT,1u);
   int record_size=snprintf((char *)image+RECORD_AT,1024u,
      "ARCINST1\rrevision=1\rstate=%s\rattempt=1\rsource=%s\rmedia=%s\r"
      "fingerprint=%s\rcontent=%s\ros=%s\r%s",
      variant == 0u ? "completed" : "started",
      expected->source,expected->media,expected->fingerprint,
      expected->content,expected->os,
      variant == 0u ? "product=!Result\r" : "");
   if (record_size <= 0 || record_size >= 1024) { free(image); return false; }
   entry(image+OWNED_AT,0u,"Installed",(unsigned)record_size,0x400u,false);
   unsigned count=1u;
   if (variant != 0u) {
      char name[11];
      if (expected->copy->length >= sizeof(name)) { free(image); return false; }
      memcpy(name,expected->copy->bytes,expected->copy->length);
      name[expected->copy->length]=0;
      entry(image+OWNED_AT,count++,name,2048u,0x500u,true);
   }
   if (variant == 0u || variant == 1u || variant == 3u)
      entry(image+OWNED_AT,count++,"!Result",2048u,0x600u,true);
   if (variant == 3u)
      entry(image+OWNED_AT,count++,"Notes",1u,0x700u,false);
   finish(image+OWNED_AT,count);
   bool okay=true;
   for (uint32_t lba=0u;lba<ARC_INSTALL_HD_SECTORS && okay;++lba)
      if (memcmp(image+(size_t)lba*512u,
                 arc_install_store_image(store)+(size_t)lba*512u,512u))
         okay=arc_install_store_write(store,lba,1u,image+(size_t)lba*512u);
   if (okay) {
      ArcHdScope scope=arc_hd_scope_read(arc_install_store_read,store,expected);
      okay=variant == 0u ?
         scope.state==ARC_HD_COMPLETED && scope.product_present :
         scope.state==ARC_HD_STARTED && scope.copy_present &&
         scope.application_count == (variant == 2u ? 1u : 2u) &&
         (variant != 3u || scope.other_count == 1u);
   }
   free(image);
   return okay;
}

bool arc_hd_test_seed_product(ArcInstallStore *store,
                              const ArcHdExpected *expected)
{ return seed_scope(store,expected,0u); }

bool arc_hd_test_seed_started(ArcInstallStore *store,
                              const ArcHdExpected *expected,
                              unsigned variant)
{ return variant >= 1u && variant <= 3u &&
         seed_scope(store,expected,variant); }

bool arc_hd_test_preflight_collision(ArcInstallStore *store)
{
   if (!store || !arc_install_store_writable(store)) return false;
   enum { ROOT_AT=MAP_AT+2*MAP_SIZE };
   uint8_t root[2048];
   memcpy(root,arc_install_store_image(store)+ROOT_AT,sizeof(root));
   entry(root,0u,"ArcDroid",0u,0x300u,false);
   finish(root,1u);
   return arc_install_store_write(store,ROOT_AT/512u,4u,root);
}
