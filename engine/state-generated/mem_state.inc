/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
/* The only producers fill three uniform banks (initmem, mem_updatetimings,
 * mem_setromspeed). Store all six actual timings, including historical I/O
 * timings, rather than recomputing them from a possibly newer clock. */
static void arc_state_mem_timings_rebuild(void *opaque)
{
   const uint64_t *timings=opaque;
   for(unsigned i=0;i<16384u;++i) {
      unsigned bank=i<0x3000u?0u:i<0x3800u?1u:2u;
      memcpy(mem_speed[i],timings+bank*2u,sizeof(mem_speed[i]));
   }
}
void arc_state_visit_engine_arculator_src_mem(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/mem.c:mem_speed",&mem_speed,sizeof(mem_speed));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_dorefresh",&mem_dorefresh,sizeof(mem_dorefresh));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_spd_multi",&mem_spd_multi,sizeof(mem_spd_multi));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_spd_multi_2",&mem_spd_multi_2,sizeof(mem_spd_multi_2));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_spd_multi_5",&mem_spd_multi_5,sizeof(mem_spd_multi_5));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_spd_multi_32",&mem_spd_multi_32,sizeof(mem_spd_multi_32));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_romspeed_n",&mem_romspeed_n,sizeof(mem_romspeed_n));
   arc_state_region(s,"engine/arculator/src/mem.c:mem_romspeed_s",&mem_romspeed_s,sizeof(mem_romspeed_s));
   arc_state_region(s,"engine/arculator/src/mem.c:backplane_mask",&backplane_mask,sizeof(backplane_mask));
   arc_state_region(s,"engine/arculator/src/mem.c:bank",&bank,sizeof(bank));
   arc_state_region(s,"engine/arculator/src/mem.c:ddensity",&ddensity,sizeof(ddensity));
   arc_state_region(s,"engine/arculator/src/mem.c:fdcside",&fdcside,sizeof(fdcside));
   arc_state_region(s,"engine/arculator/src/mem.c:realmemsize",&realmemsize,sizeof(realmemsize));
   arc_state_region(s,"engine/arculator/src/mem.c:ram",&ram,sizeof(ram));
   arc_state_region(s,"engine/arculator/src/mem.c:rom",&rom,sizeof(rom));
   arc_state_region(s,"engine/arculator/src/mem.c:rom_5th_column",&rom_5th_column,sizeof(rom_5th_column));
   arc_state_region(s,"engine/arculator/src/mem.c:rom_arcrom",&rom_arcrom,sizeof(rom_arcrom));
   arc_state_region(s,"engine/arculator/src/mem.c:mempoint",&mempoint,sizeof(mempoint));
   arc_state_region(s,"engine/arculator/src/mem.c:memstat",&memstat,sizeof(memstat));
   arc_state_region(s,"engine/arculator/src/mem.c:memmode",&memmode,sizeof(memmode));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/mem.c:mem_speed");
   const unsigned starts[3]={0,0x3000u,0x3800u};
   for(unsigned bank=0;bank<3u;++bank)
      arc_state_value(s,&mem_speed[starts[bank]],sizeof(mem_speed[0]));
   if(arc_state_loading(s)) {
      uint64_t *timings=arc_state_scratch(s,6u*sizeof(*timings));
      if(timings) {
         for(unsigned bank=0;bank<3u;++bank)
            arc_state_project(s,timings+bank*2u,&mem_speed[starts[bank]],sizeof(mem_speed[0]));
         arc_state_action(s,arc_state_mem_timings_rebuild,NULL,timings);
      }
   } else if(!arc_state_measuring(s)) {
      for(unsigned i=0;i<16384u;++i) {
         unsigned bank=i<0x3000u?0u:i<0x3800u?1u:2u;
         if(memcmp(mem_speed[i],mem_speed[starts[bank]],sizeof(mem_speed[0]))) {
            arc_state_fail(s,"nonuniform memory timings");break;
         }
      }
   }
   arc_state_object(s,"engine/arculator/src/mem.c:mem_dorefresh");
   arc_state_value(s,&(mem_dorefresh),sizeof(mem_dorefresh));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_spd_multi");
   arc_state_value(s,&(mem_spd_multi),sizeof(mem_spd_multi));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_spd_multi_2");
   arc_state_value(s,&(mem_spd_multi_2),sizeof(mem_spd_multi_2));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_spd_multi_5");
   arc_state_value(s,&(mem_spd_multi_5),sizeof(mem_spd_multi_5));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_spd_multi_32");
   arc_state_value(s,&(mem_spd_multi_32),sizeof(mem_spd_multi_32));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_romspeed_n");
   arc_state_value(s,&(mem_romspeed_n),sizeof(mem_romspeed_n));
   arc_state_object(s,"engine/arculator/src/mem.c:mem_romspeed_s");
   arc_state_value(s,&(mem_romspeed_s),sizeof(mem_romspeed_s));
   arc_state_object(s,"engine/arculator/src/mem.c:backplane_mask");
   arc_state_value(s,&(backplane_mask),sizeof(backplane_mask));
   arc_state_object(s,"engine/arculator/src/mem.c:bank");
   arc_state_value(s,&(bank),sizeof(bank));
   arc_state_object(s,"engine/arculator/src/mem.c:ddensity");
   arc_state_value(s,&(ddensity),sizeof(ddensity));
   arc_state_object(s,"engine/arculator/src/mem.c:fdcside");
   arc_state_value(s,&(fdcside),sizeof(fdcside));
   arc_state_object(s,"engine/arculator/src/mem.c:realmemsize");
   arc_state_value(s,&(realmemsize),sizeof(realmemsize));
   arc_state_object(s,"engine/arculator/src/mem.c:memstat");
   arc_state_value(s,&(memstat),sizeof(memstat));
   arc_state_object(s,"engine/arculator/src/mem.c:memmode");
   arc_state_value(s,&(memmode),sizeof(memmode));
   if(arc_state_loading(s)) {
      ARC_STATE_MATCH(s,realmemsize);ARC_STATE_BOUND(s,memmode,MEMMODE_USER,MEMMODE_SUPER);
   }
}
