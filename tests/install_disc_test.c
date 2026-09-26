/* Independent checks for ArcDroid's synthetic hard-disc formatter.
 * Project-authored GPL-2.0-only. */
#include "install_disc.h"
#include "install_validate.h"
#include "adffs.h"
#include "arcdroid_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le32(const uint8_t *p);
static void put24(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8u); p[2]=(uint8_t)(n>>16u); }
static void put32(uint8_t *p, uint32_t n)
{ put24(p,n); p[3]=(uint8_t)(n>>24u); }
static void set_bits(uint8_t *p, unsigned at, unsigned n, unsigned value)
{
   for (unsigned i=0u;i<n;++i) {
      unsigned mask=1u<<((at+i)&7u);
      p[(at+i)>>3u]=(uint8_t)((p[(at+i)>>3u]&~mask)|
          (((value>>i)&1u)?mask:0u));
   }
}
static uint32_t ror13(uint32_t n) { return (n>>13u)|(n<<19u); }
static void finish_directory(uint8_t *p, unsigned count)
{
   uint32_t check=0u; unsigned used=5u+count*26u;
   for (unsigned at=0u;at<(used&~3u);at+=4u)
      check=le32(p+at)^ror13(check);
   for (unsigned at=used&~3u;at<used;++at)
      check=p[at]^ror13(check);
   for (unsigned at=2008u;at<2044u;at+=4u)
      check=le32(p+at)^ror13(check);
   p[2047u]=(uint8_t)(check^(check>>8u)^(check>>16u)^(check>>24u));
}
static void entry(uint8_t *dir, unsigned index, const char *name,
                  uint32_t length, uint32_t indirect, uint32_t load,
                  uint8_t attr)
{
   uint8_t *p=dir+5u+index*26u;
   memset(p,13u,10u); memcpy(p,name,strlen(name));
   put32(p+10u,load); put32(p+14u,0u);
   put32(p+18u,length); put24(p+22u,indirect); p[25u]=attr;
}
static void child_directory(uint8_t *p)
{
   memset(p,0,2048u); p[0]=p[2042u]=1u;
   memcpy(p+1u,"Nick",4u); memcpy(p+2043u,"Nick",4u);
   p[2010u]=2u; memcpy(p+2032u,"!Fixture",8u);
   memset(p+2040u,13u,2u);
}
static uint8_t *read_input(const char *path, size_t *size)
{
   FILE *f=fopen(path,"rb"); assert(f);
   assert(!fseek(f,0,SEEK_END)); long n=ftell(f);
   assert(n>0 && n<16384 && !fseek(f,0,SEEK_SET));
   uint8_t *p=malloc((size_t)n); assert(p);
   assert(fread(p,1u,(size_t)n,f)==(size_t)n && !fclose(f));
   *size=(size_t)n; return p;
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
   ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u); }
static uint8_t independent_zone_sum(const uint8_t *p)
{
   unsigned x = 0u, y = 0u, z = 0u, w = 0u;
   for (int i = 1020; i >= 4; i -= 4) {
      x += p[i] + (w >> 8u); w &= 255u;
      y += p[i+1] + (x >> 8u); x &= 255u;
      z += p[i+2] + (y >> 8u); y &= 255u;
      w += p[i+3] + (z >> 8u); z &= 255u;
   }
   x += w >> 8u;
   y += p[1] + (x >> 8u);
   z += p[2] + (y >> 8u);
   w += p[3] + (z >> 8u);
   return (uint8_t)(x ^ y ^ z ^ w);
}
int main(int argc, char **argv)
{
   assert(ARC_INSTALL_HD_SECTORS == 32256u);
   assert(ARC_INSTALL_HD_BYTES == 16515072u);
   uint8_t *first = malloc(ARC_INSTALL_HD_BYTES + 1u);
   uint8_t *second = malloc(ARC_INSTALL_HD_BYTES);
   assert(first && second);
   first[ARC_INSTALL_HD_BYTES] = 0xa5u;
   assert(!arc_install_hd_format(NULL, ARC_INSTALL_HD_BYTES));
   assert(!arc_install_hd_format(first, ARC_INSTALL_HD_BYTES - 1u));
   assert(arc_install_hd_format(first, ARC_INSTALL_HD_BYTES));
   assert(arc_install_hd_format(second, ARC_INSTALL_HD_BYTES));
   assert(first[ARC_INSTALL_HD_BYTES] == 0xa5u);
   assert(!memcmp(first, second, ARC_INSTALL_HD_BYTES));
   assert(le32(first + 0xc00u) == 0x20000000u);
   const uint8_t *record = first + 0xdc0u;
   assert(record[0] == 9u && record[1] == 63u && record[2] == 16u);
   assert(record[3] == 0u && record[4] == 15u && record[5] == 10u);
   assert(record[9] == 5u && record[10] == 32u && record[11] == 0u);
   assert(le32(record + 12u) == 0x20bu);
   assert(le32(record + 16u) == ARC_INSTALL_HD_BYTES);
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += first[0xc00u + i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   assert(first[0xdffu] == (uint8_t)sum);
   size_t map_at = (2u * 8160u - 480u) * 1024u;
   const uint8_t *map = first + map_at;
   assert(!memcmp(map, map + 5120u, 5120u));
   uint8_t cross = 0u;
   for (unsigned zone = 0u; zone < 5u; ++zone) {
      const uint8_t *block = map + zone * 1024u;
      assert(block[0] == independent_zone_sum(block));
      cross ^= block[3];
   }
   assert(cross == 0xffu);
   assert(!memcmp(map + 4u, record, 60u));
   const uint8_t *root = first + map_at + 10240u;
   assert(root[0] == 1u && !memcmp(root + 1u, "Nick", 4u));
   assert(root[2007u] == 0u && root[2032u] == '$' &&
          root[2042u] == 1u && !memcmp(root + 2043u, "Nick", 4u));
   const ArcInstallProfile *profile = arc_install_profile(10140u, 2u);
   assert(profile);
   ArcInstallPlan plan = {0};
   plan.profile = profile; plan.variant = &profile->variants[0];
   strcpy(plan.storage_id, "0000000000000000000000000000000000000000");
   char launcher[4096] = {0}; size_t launcher_size = 0u;
   assert(arc_install_hd_launcher(&plan, false, (uint8_t *)launcher,
                                  sizeof(launcher) - 1u, &launcher_size));
   assert(launcher_size < sizeof(launcher) &&
          strstr(launcher, "Dismount :4\rCopy ADFS::1.$.ArcDoor RAM::0.$.ArcDoor ~C~VFQ\r") &&
          !strstr(launcher, "Load ADFS::1.$.ArcDoor"));
   ArcInstallValidation result;
   assert(!arc_install_hd_validate(&plan, first, ARC_INSTALL_HD_BYTES,
                                   &result));
   assert(!strcmp(result.reason, "marker-missing-or-size"));
   if (argc > 1) {
      size_t jfd_size=0u;
      uint8_t *jfd=read_input(argv[1],&jfd_size);
      ArcInstallProfile synthetic={0};
      synthetic.game_id=123u;
      ArcInstallVariant *variant=&synthetic.variants[0];
      variant->app="!Fixture"; variant->image_in_app=true;
      variant->discs[0].size=6756u;
      variant->discs[0].sha1="015e1ef84221f0242ea7eb65ec489b2ea04b0bdf";
      plan.profile=&synthetic; plan.variant=variant;
      strcpy(plan.storage_id,"fffbd5a69f9b43c4ea8cc69f1fe9234f393e3d45");
      uint8_t marker[96]; size_t marker_size=0u;
      assert(arc_install_marker(&plan,marker,sizeof(marker),&marker_size));
      const size_t base=map_at;
      uint8_t *zone=first+base+2u*1024u;
      memset(zone+4u,0,1020u);
      /* Fragment 2 owns map/root; 3..7 each own a 16 KiB extent. */
      for (unsigned fragment=2u;fragment<=7u;++fragment) {
         unsigned at=32u+(fragment-2u)*16u;
         set_bits(zone,at,15u,fragment);
         set_bits(zone,at+15u,1u,1u);
      }
      set_bits(zone,8u,15u,120u);
      set_bits(zone,23u,1u,1u);
      set_bits(zone,319u,1u,1u);
      zone[0]=independent_zone_sum(zone);
      memcpy(first+base+5120u,first+base,5120u);
      uint8_t *root=first+base+10240u;
      entry(root,0u,"!Fixture",2048u,0x401u,0xfff00000u,0x3bu);
      entry(root,1u,"ArcReady",(uint32_t)marker_size,0x301u,
            0xffffeb00u,0x33u);
      finish_directory(root,2u);
      const size_t zone_base=base;
      memcpy(first+zone_base+16u*1024u,marker,marker_size);
      uint8_t *app=first+zone_base+32u*1024u;
      child_directory(app);
      char run[40]; int run_size=snprintf(run,sizeof(run),
                                      "ADFBootFloppy F0012301\r");
      assert(run_size>0);
      entry(app,0u,"!Run",(uint32_t)run_size,0x501u,
            0xffffeb00u,0x33u);
      entry(app,1u,"F0012301",(uint32_t)jfd_size,0x601u,
            0xfff00000u,0x33u);
      entry(app,2u,"Save",4u,0x701u,0xfff00000u,0x33u);
      finish_directory(app,3u);
      memcpy(first+zone_base+48u*1024u,run,(size_t)run_size);
      memcpy(first+zone_base+64u*1024u,jfd,jfd_size);
      memcpy(first+zone_base+80u*1024u,"save",4u);
      char run_sha[41], save_sha[41], listing[512];
      arc_sha1_hex(run,(size_t)run_size,run_sha);
      arc_sha1_hex("save",4u,save_sha);
      int listing_size=snprintf(listing,sizeof(listing),
            "F $.!Fixture.!Run %d %s tfeb a33\n"
            "F $.!Fixture.Save 4 %s t000 a33\n",
            run_size,run_sha,save_sha);
      assert(listing_size>0 && (size_t)listing_size<sizeof(listing));
      char tree_hash[65];
      arc_sha256_hex(listing,(size_t)listing_size,tree_hash);
      variant->expected_tree_count=2u;
      variant->expected_tree_sha256=tree_hash;
      assert(arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      assert(result.tree_count==2u && !strcmp(result.tree_sha256,tree_hash));
      if (argc > 2) {
         FILE *dump=fopen(argv[2],"wb"); assert(dump);
         assert(fwrite(first,1u,ARC_INSTALL_HD_BYTES,dump)==ARC_INSTALL_HD_BYTES);
         assert(!fclose(dump));
      }
      root[5u+26u+18u]=0u; finish_directory(root,2u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      root[5u+26u+18u]=(uint8_t)marker_size; finish_directory(root,2u);
      app[5u+10u+2u]=0u; finish_directory(app,3u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      app[5u+10u+2u]=0xffu; finish_directory(app,3u);
      app[5u+2u*26u+25u]&=(uint8_t)~2u; finish_directory(app,3u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      app[5u+2u*26u+25u]|=2u; finish_directory(app,3u);
      app[5u+2u*26u+18u]=3u; finish_directory(app,3u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      app[5u+2u*26u+18u]=4u; finish_directory(app,3u);
      uint8_t save_entry[26];
      memcpy(save_entry,app+5u+2u*26u,sizeof(save_entry));
      memset(app+5u+2u*26u,0u,sizeof(save_entry));
      finish_directory(app,2u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      memcpy(app+5u+2u*26u,save_entry,sizeof(save_entry));
      finish_directory(app,3u);
      entry(app,3u,"Extra",4u,0x701u,0xfff00000u,0x33u);
      finish_directory(app,4u);
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      memset(app+5u+3u*26u,0u,26u); finish_directory(app,3u);
      first[zone_base+64u*1024u]^=1u;
      assert(!arc_install_hd_validate(&plan,first,ARC_INSTALL_HD_BYTES,&result));
      free(jfd);
   }
   free(first); free(second);
   puts("install-disc formatter: pass");
   return 0;
}
