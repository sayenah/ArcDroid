/* Alternate boot option, reserved fields and a valid fragmented free chain.
 * Public authored metadata only. GPL-2.0-only. */
#undef NDEBUG
#include <assert.h>
static void audit_map_seal(uint8_t boot[512],uint8_t maps[5120])
{
 unsigned sum=0;for(unsigned i=0;i<511;++i)sum+=boot[i];
 while(sum>255)sum=(sum&255)+(sum>>8);boot[511]=(uint8_t)sum;
 for(unsigned z=0;z<5;++z)maps[z*512]=zone_check(maps+z*512);
 memcpy(maps+2560,maps,2560);
}
void arc_predicate_filecore_audit(const uint8_t source_boot[512],const uint8_t source_maps[5120])
{
 uint8_t boot[512],maps[5120];ArcBootLayout view;
 for(unsigned option=0;option<=2;option+=2){
  memcpy(boot,source_boot,512);memcpy(maps,source_maps,5120);boot[455]=maps[11]=(uint8_t)option;
  audit_map_seal(boot,maps);assert(arc_boot_layout_accept(&view,boot,maps));
 }
 for(unsigned byte=40;byte<60;++byte){
  memcpy(boot,source_boot,512);memcpy(maps,source_maps,5120);boot[448+byte]=maps[4+byte]=1;
  audit_map_seal(boot,maps);assert(!arc_boot_layout_accept(&view,boot,maps));
 }
 memcpy(boot,source_boot,512);memcpy(maps,source_maps,5120);uint8_t *zone=maps+512;
 memset(zone,0,512);bits(zone,23,1,1);bits(zone,8,15,24);
 bits(zone,32,15,32);bits(zone,47,1,1); /* First free fragment links to bit 64. */
 bits(zone,48,15,257);bits(zone,63,1,1); /* An independent allocated fragment. */
 bits(zone,4095,1,1); /* Last free fragment extends from bit 64 to the zone end. */
 audit_map_seal(boot,maps);assert(arc_boot_layout_accept(&view,boot,maps));
}
