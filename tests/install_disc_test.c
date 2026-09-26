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
static uint8_t independent_zone_sum(const uint8_t *p);
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
static unsigned get_bits(const uint8_t *p, unsigned at, unsigned n)
{
   unsigned value=0u;
   for (unsigned i=0u;i<n;++i)
      value |= ((p[(at+i)>>3u] >> ((at+i)&7u)) & 1u) << i;
   return value;
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
static void complex_directory(uint8_t *p, const char *name, uint32_t parent)
{
   memset(p,0,2048u); p[0]=p[2042u]=1u;
   memcpy(p+1u,"Nick",4u); memcpy(p+2043u,"Nick",4u);
   put24(p+2010u,parent);
   memset(p+2032u,13u,10u); memcpy(p+2032u,name,strlen(name));
}
static void complex_zone(uint8_t *p, unsigned zone,
                         const unsigned *ids, const unsigned *lengths,
                         unsigned count)
{
   assert(zone == 2u || zone == 3u);
   memset(p,0,512u);
   unsigned at=32u;
   for (unsigned i=0u;i<count;++i) {
      assert(ids[i]>=2u && lengths[i]>=16u && at+lengths[i]<=4096u);
      set_bits(p,at,15u,ids[i]);
      set_bits(p,at+lengths[i]-1u,1u,1u);
      at+=lengths[i];
   }
   if (at<4096u) {
      set_bits(p,8u,15u,at-8u);
      set_bits(p,4095u,1u,1u);
   }
   p[0]=independent_zone_sum(p);
}
static void complex_line(char lines[64][256], unsigned *count,
                         bool dir, const char *path, const uint8_t *data,
                         size_t size, unsigned type, unsigned attr)
{
   assert(*count<64u);
   int n;
   if (dir) n=snprintf(lines[*count],256u,"D %s a%02x\n",path,attr);
   else {
      char hash[41]; arc_sha1_hex(data,size,hash);
      n=snprintf(lines[*count],256u,"F %s %zu %s t%03x a%02x\n",
                 path,size,hash,type,attr);
   }
   assert(n>0 && n<256);
   ++*count;
}
static int complex_line_order(const void *a, const void *b)
{ return strcmp(*(const char *const *)a,*(const char *const *)b); }
static void test_complex_tree(uint8_t *image)
{
   assert(arc_install_hd_format(image,ARC_INSTALL_HD_BYTES));
   enum { MAP=0x778000u, ZONE=512u, BITS=4064u };
   uint8_t *map=image+MAP;
   const unsigned zone2_ids[]={2u,3u,4u,5u,7u,8u,9u,10u,11u,12u,13u,6u};
   const unsigned zone2_lengths[]={16u,16u,16u,16u,16u,16u,16u,
                                   3824u,16u,16u,16u,80u};
   complex_zone(map+2u*ZONE,2u,zone2_ids,zone2_lengths,12u);
   unsigned zone3_ids[43], zone3_lengths[43];
   zone3_ids[0]=6u; zone3_lengths[0]=64u;
   for (unsigned i=0u;i<41u;++i) {
      zone3_ids[i+1u]=20u+i; zone3_lengths[i+1u]=16u;
   }
   zone3_ids[42u]=70u; zone3_lengths[42u]=16u;
   complex_zone(map+3u*ZONE,3u,zone3_ids,zone3_lengths,43u);
   memcpy(map+5u*ZONE,map,5u*ZONE);
   uint8_t *zone2=image+MAP;
   uint8_t *zone3=image+MAP+BITS*1024u;
   ArcInstallProfile profile={0};
   ArcInstallVariant *variant=&profile.variants[0];
   profile.game_id=123u;
   variant->app="!Fixture";
   ArcInstallPlan plan={0};
   plan.profile=&profile; plan.variant=variant;
   strcpy(plan.storage_id,"fffbd5a69f9b43c4ea8cc69f1fe9234f393e3d45");
   uint8_t marker[96]; size_t marker_size=0u;
   assert(arc_install_marker(&plan,marker,sizeof(marker),&marker_size));
   uint8_t *root=image+MAP+10u*ZONE;
   entry(root,0u,"!Fixture",2048u,0x401u,0xfff00000u,0x38u);
   entry(root,1u,"ArcReady",(uint32_t)marker_size,0x301u,
         0xffffeb00u,0x33u);
   finish_directory(root,2u);
   memcpy(zone2+16u*1024u,marker,marker_size);
   char lines[64][256]; unsigned line_count=0u;
   char run[40]; int run_size=snprintf(run,sizeof(run),
                                      "ADFBootFloppy F0012301\r");
   assert(run_size>0 && run_size<(int)sizeof(run));
   uint8_t *app=zone2+32u*1024u;
   complex_directory(app,"!Fixture",0x20bu);
   entry(app,0u,"!Run",(uint32_t)run_size,0x501u,0xffffeb00u,0x33u);
   entry(app,1u,"MANUAL",2048u,0x701u,0xfff00000u,0x38u);
   static const char *const extra_names[]={"ARCHIVE","NOTES","DATA"};
   for (unsigned i=0u;i<3u;++i) {
      uint8_t *extra=zone2+(3936u+i*16u)*1024u;
      char path[80];
      int n=snprintf(path,sizeof(path),"$.!Fixture.%s",extra_names[i]);
      assert(n>0 && n<(int)sizeof(path));
      entry(app,i+2u,extra_names[i],2048u,(11u+i)*256u+1u,
            0xfff00000u,0x38u);
      complex_directory(extra,extra_names[i],0x401u);
      finish_directory(extra,0u);
      complex_line(lines,&line_count,true,path,NULL,0u,0u,0x38u);
   }
   finish_directory(app,5u);
   memcpy(zone2+48u*1024u,run,(size_t)run_size);
   complex_line(lines,&line_count,false,"$.!Fixture.!Run",
                (const uint8_t *)run,(size_t)run_size,0xfebu,0x33u);
   complex_line(lines,&line_count,true,"$.!Fixture.MANUAL",NULL,0u,0u,0x38u);
   uint8_t *manual=zone2+64u*1024u;
   complex_directory(manual,"MANUAL",0x401u);
   entry(manual,0u,"PICS",2048u,0x801u,0xfff00000u,0x38u);
   finish_directory(manual,1u);
   complex_line(lines,&line_count,true,"$.!Fixture.MANUAL.PICS",NULL,0u,0u,0x38u);
   uint8_t *pics=zone2+80u*1024u;
   complex_directory(pics,"PICS",0x701u);
   entry(pics,0u,"Big",134726u,0x601u,0xfffabc00u,0x13u);
   entry(pics,1u,"DEEP",2048u,0x901u,0xfff00000u,0x38u);
   complex_line(lines,&line_count,true,"$.!Fixture.MANUAL.PICS.DEEP",NULL,0u,0u,0x38u);
   uint8_t *deep=zone2+96u*1024u;
   complex_directory(deep,"DEEP",0x801u);
   entry(deep,0u,"Leaf",4u,0x4601u,0xfff12300u,0x03u);
   finish_directory(deep,1u);
   const uint8_t leaf[]={0x21u,0x42u,0x63u,0x84u};
   memcpy(zone3+(64u+41u*16u)*1024u,leaf,sizeof(leaf));
   complex_line(lines,&line_count,false,
      "$.!Fixture.MANUAL.PICS.DEEP.Leaf",leaf,sizeof(leaf),0x123u,0x03u);
   uint8_t *big=malloc(134726u); assert(big);
   for (unsigned i=0u;i<134726u;++i) big[i]=(uint8_t)(i*37u+11u);
   memcpy(zone2+3984u*1024u,big,80u*1024u);
   memcpy(zone3,big+80u*1024u,134726u-80u*1024u);
   complex_line(lines,&line_count,false,
      "$.!Fixture.MANUAL.PICS.Big",big,134726u,0xabcu,0x13u);
   for (unsigned i=0u;i<41u;++i) {
      char name[11],path[80];
      int n=snprintf(name,sizeof(name),"F%02u",i);
      assert(n>0 && n<(int)sizeof(name));
      if (i==0u) strcpy(name,"F00/GIF");
      if (i==1u) strcpy(name,"F01/GIF");
      n=snprintf(path,sizeof(path),"$.!Fixture.MANUAL.PICS.%s",name);
      assert(n>0 && n<(int)sizeof(path));
      uint8_t *content=zone3+(64u+i*16u)*1024u;
      if (i==0u) content+=512u; /* Shared-sector indirect address. */
      for (unsigned j=0u;j<4u;++j) content[j]=(uint8_t)(i*7u+j);
      unsigned attr=i==0u?0x03u:0x33u;
      entry(pics,i+2u,name,4u,(20u+i)*256u+(i==0u?2u:1u),
            0xfff12300u,(uint8_t)attr);
      complex_line(lines,&line_count,false,path,content,4u,0x123u,attr);
   }
   finish_directory(pics,43u);
   assert(line_count==50u);
   char *ordered[64]; size_t total=0u;
   for (unsigned i=0u;i<line_count;++i) ordered[i]=lines[i];
   qsort(ordered,line_count,sizeof(ordered[0]),complex_line_order);
   for (unsigned i=0u;i<line_count;++i) total+=strlen(ordered[i]);
   char *listing=malloc(total); assert(listing);
   size_t used=0u;
   for (unsigned i=0u;i<line_count;++i) {
      size_t n=strlen(ordered[i]); memcpy(listing+used,ordered[i],n); used+=n;
   }
   char digest[65]; arc_sha256_hex(listing,total,digest);
   variant->expected_tree_count=line_count;
   variant->expected_tree_sha256=digest;
   ArcInstallValidation result;
   assert(arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result));
   assert(result.tree_count==line_count && !strcmp(result.tree_sha256,digest));
   /* Keep structure valid while changing content: the listing digest must
    * still reject the candidate. */
   uint8_t *big_tail=zone3+134725u-80u*1024u;
   *big_tail^=1u;
   assert(!arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result) &&
          !strcmp(result.reason,"application-tree-mismatch"));
   *big_tail^=1u;
   /* A missing later fragment cannot be silently treated as a short file. */
   set_bits(map+3u*ZONE,32u,15u,71u);
   map[3u*ZONE]=independent_zone_sum(map+3u*ZONE);
   memcpy(map+5u*ZONE,map,5u*ZONE);
   assert(!arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result) &&
          !strcmp(result.reason,"application-tree-invalid"));
   set_bits(map+3u*ZONE,32u,15u,6u);
   map[3u*ZONE]=independent_zone_sum(map+3u*ZONE);
   memcpy(map+5u*ZONE,map,5u*ZONE);
   /* Directory metadata and generated run text are separate gates. */
   pics[2047u]^=1u;
   assert(!arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result) &&
          !strcmp(result.reason,"application-tree-invalid"));
   pics[2047u]^=1u;
   zone2[48u*1024u]^=1u;
   assert(!arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result) &&
          !strcmp(result.reason,"application-tree-invalid"));
   zone2[48u*1024u]^=1u;
   assert(arc_install_hd_validate(&plan,image,ARC_INSTALL_HD_BYTES,&result));
   free(listing); free(big);
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
   for (int i = 508; i >= 4; i -= 4) {
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
static void test_hd_launchers(void)
{
   static const struct {
      unsigned game_id, disc_count;
      const char *variant_key;
   } cases[] = {
      {10140u, 2u, "etype"},
      {10609u, 4u, "etype"},
      {10609u, 4u, "xtra"},
      {10587u, 1u, "premier"}
   };
   char generic[4096] = {0};
   size_t generic_size = 0u;
   assert(arc_adffs_launcher((uint8_t *)generic, sizeof(generic) - 1u,
                             &generic_size) && generic_size < sizeof(generic));
   const char *carrier_step = strstr(generic, "ADFMount ADFS::0.$.Game");
   assert(carrier_step);
   size_t prefix_size = (size_t)(carrier_step - generic);
   generic[prefix_size] = 0;
   for (size_t c = 0u; c < sizeof(cases) / sizeof(cases[0]); ++c) {
      const ArcInstallProfile *profile = arc_install_profile(
          cases[c].game_id, cases[c].disc_count);
      assert(profile);
      const ArcInstallVariant *variant = NULL;
      for (unsigned v = 0u; v < profile->variant_count; ++v)
         if (!strcmp(profile->variants[v].key, cases[c].variant_key))
            variant = &profile->variants[v];
      assert(variant);
      ArcInstallPlan plan = {0};
      plan.profile = profile;
      plan.variant = variant;
      char original[4096] = {0}, install[4096] = {0}, run[4096] = {0};
      size_t original_size = 0u, install_size = 0u, run_size = 0u;
      assert(arc_install_launcher(&plan, (uint8_t *)original,
                                  sizeof(original) - 1u, &original_size) &&
             arc_install_hd_launcher(&plan, false, (uint8_t *)install,
                                     sizeof(install) - 1u, &install_size) &&
             arc_install_hd_launcher(&plan, true, (uint8_t *)run,
                                     sizeof(run) - 1u, &run_size));
      char final_ram_step[128], final_hd_step[128], hd_tail[512];
      int ram_n = snprintf(final_ram_step, sizeof(final_ram_step),
          "Dir RAM::0.$.%s\rRun RAM::0.$.%s.!Run\r", variant->app,
          variant->app);
      int hd_n = snprintf(final_hd_step, sizeof(final_hd_step),
          "Dir ADFS::4.$.%s\rRun ADFS::4.$.%s.!Run\r", variant->app,
          variant->app);
      int tail_n = snprintf(hd_tail, sizeof(hd_tail),
          "IfThere RAM::0.$.%s Then Copy RAM::0.$.%s ADFS::4.$.%s ~C~VRFQ Else Error ArcDroid install: hard-disc copy failed\r"
          "IfThere ADFS::4.$.%s Then Set ArcDroid$Guard 1 Else Error ArcDroid install: hard-disc copy failed\r"
          "Copy ADFS::0.$.ArcReady ADFS::4.$.ArcReady ~C~VFQ\r"
          "IfThere ADFS::4.$.ArcReady Then Set ArcDroid$Guard 1 Else Error ArcDroid install: marker failed\r"
          "Dismount :4\rCopy ADFS::0.$.ArcDoor RAM::0.$.ArcDoor ~C~VFQ\r",
          variant->app, variant->app, variant->app, variant->app);
      assert(ram_n > 0 && (size_t)ram_n < sizeof(final_ram_step) &&
             hd_n > 0 && (size_t)hd_n < sizeof(final_hd_step) &&
             tail_n > 0 && (size_t)tail_n < sizeof(hd_tail) &&
             original_size >= (size_t)ram_n &&
             !memcmp(original + original_size - (size_t)ram_n,
                     final_ram_step, (size_t)ram_n));
      size_t install_prefix = original_size - (size_t)ram_n;
      size_t eject_size = variant->eject ? 0u : strlen("ADFEject\r");
      assert(install_size == install_prefix + eject_size + (size_t)tail_n &&
             !memcmp(install, original, install_prefix) &&
             (variant->eject ||
              !memcmp(install + install_prefix, "ADFEject\r", eject_size)) &&
             !memcmp(install + install_prefix + eject_size,
                     hd_tail, (size_t)tail_n));
      assert((variant->eject &&
              !memcmp(install + install_prefix - strlen("ADFEject\r"),
                      "ADFEject\r", strlen("ADFEject\r"))) ||
             (!variant->eject && eject_size == strlen("ADFEject\r")));
      assert(run_size == prefix_size + (size_t)hd_n &&
             !memcmp(run, generic, prefix_size) &&
             !memcmp(run + prefix_size, final_hd_step, (size_t)hd_n));
      const char *line = generic;
      unsigned found = 0u;
      while ((line = strstr(line, "RMEnsure ")) != NULL) {
         const char *end = strchr(line, '\r');
         char command[256];
         assert(end && (size_t)(end - line + 1u) < sizeof(command));
         size_t length = (size_t)(end - line + 1u);
         memcpy(command, line, length);
         command[length] = 0;
         assert(strstr(install, command) && strstr(run, command));
         ++found;
         line = end + 1u;
      }
      assert(found == 10u);
   }
}
int main(int argc, char **argv)
{
   test_hd_launchers();
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
   char blank_hash[65];
   arc_sha256_hex(first,ARC_INSTALL_HD_BYTES,blank_hash);
   assert(!strcmp(blank_hash,
      "9ec06c758da485f6b7c26b70e4c2b268ec959991cc43a9b6d3bffc495b4a57d2"));
   assert(le32(first + 0xc00u) == 0x20000000u);
   const uint8_t *record = first + 0xdc0u;
   assert(record[0] == 9u && record[1] == 63u && record[2] == 16u);
   assert(record[3] == 0u && record[4] == 15u && record[5] == 10u);
   assert(record[9] == 5u && record[10] == 32u && record[11] == 0u);
   assert(le32(record + 12u) == 0x20bu);
   assert(le32(record + 16u) == ARC_INSTALL_HD_BYTES);
   assert(record[20u] == 0x44u && record[21u] == 0x41u &&
          !memcmp(record+22u,"ArcDroidHD",10u));
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += first[0xc00u + i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   assert(first[0xdffu] == (uint8_t)sum);
   const size_t zone_size = (size_t)1u << record[0];
   const size_t allocation_bits = zone_size * 8u -
      ((size_t)record[10u] | ((size_t)record[11u] << 8u));
   const size_t map_at = ((record[9u] / 2u) * allocation_bits - 480u) *
      ((size_t)1u << record[5u]);
   const size_t map_bytes = (size_t)record[9u] * zone_size;
   const size_t root_at = map_at + 2u * map_bytes;
   assert(zone_size == 512u && allocation_bits == 4064u &&
          map_at == 0x778000u && map_bytes == 2560u &&
          root_at == 0x779400u);
   assert(root_at - map_at == ((le32(record+12u) & 255u)-1u)*zone_size);
   const uint8_t *map = first + map_at;
   assert(!memcmp(map, map + map_bytes, map_bytes));
   uint8_t cross = 0u;
   for (unsigned zone = 0u; zone < 5u; ++zone) {
      const uint8_t *block = map + zone * zone_size;
      assert(block[0] == independent_zone_sum(block));
      cross ^= block[3];
   }
   assert(cross == 0xffu);
   assert(!memcmp(map + 4u, record, 60u));
   assert(get_bits(map,512u,15u)==2u && get_bits(map,527u,1u)==1u);
   assert(get_bits(map+2u*zone_size,32u,15u)==2u &&
          get_bits(map+2u*zone_size,47u,1u)==1u);
   assert(get_bits(map+4u*zone_size,384u,15u)==1u &&
          get_bits(map+4u*zone_size,4095u,1u)==1u);
   const uint8_t *root = first + root_at;
   assert(root[0] == 1u && !memcmp(root + 1u, "Nick", 4u));
   assert(root[2007u] == 0u && root[2032u] == '$' &&
          root[2010u] == 0x0bu && root[2011u] == 2u &&
          !memcmp(root+2013u,"ArcDroid fixture",16u) &&
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
          strstr(launcher, "Dismount :4\rCopy ADFS::0.$.ArcDoor RAM::0.$.ArcDoor ~C~VFQ\r") &&
          !strstr(launcher, "Load ADFS::0.$.ArcDoor"));
   ArcInstallValidation result;
   assert(!arc_install_hd_validate(&plan, first, ARC_INSTALL_HD_BYTES,
                                   &result));
   assert(!strcmp(result.reason, "marker-missing-or-size"));
   if (argc > 1) {
      if (argc > 3) {
         FILE *reference=fopen(argv[3],"rb"); assert(reference);
         assert(fread(second,1u,ARC_INSTALL_HD_BYTES,reference)==
                ARC_INSTALL_HD_BYTES && fgetc(reference)==EOF &&
                !fclose(reference));
         assert(!memcmp(first,second,ARC_INSTALL_HD_BYTES));
         assert(!arc_install_hd_validate(&plan,second,ARC_INSTALL_HD_BYTES,
                                         &result) &&
                !strcmp(result.reason,"marker-missing-or-size"));
         memcpy(first,second,ARC_INSTALL_HD_BYTES);
      }
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
      uint8_t *zone=first+base+2u*zone_size;
      memset(zone+4u,0,zone_size-4u);
      /* Fragment 2 owns map/root; 3..7 each own a 16 KiB extent. */
      set_bits(zone,32u,15u,2u);
      set_bits(zone,47u,1u,1u);
      for (unsigned fragment=3u;fragment<=7u;++fragment) {
         unsigned at=32u+(fragment-2u)*16u;
         set_bits(zone,at,15u,fragment);
         set_bits(zone,at+15u,1u,1u);
      }
      set_bits(zone,8u,15u,120u);
      set_bits(zone,23u,1u,1u);
      set_bits(zone,4095u,1u,1u);
      zone[0]=independent_zone_sum(zone);
      memcpy(first+base+map_bytes,first+base,map_bytes);
      uint8_t *root=first+root_at;
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
      test_complex_tree(second);
      free(jfd);
   }
   free(first); free(second);
   puts("install-disc formatter: pass");
   return 0;
}
