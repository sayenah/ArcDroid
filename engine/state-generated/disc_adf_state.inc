/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_disc_adf(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_disc_funcs",&adf_disc_funcs,sizeof(adf_disc_funcs));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf",&adf,sizeof(adf));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_sector",&adf_sector,sizeof(adf_sector));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_track",&adf_track,sizeof(adf_track));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_side",&adf_side,sizeof(adf_side));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_drive",&adf_drive,sizeof(adf_drive));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_inread",&adf_inread,sizeof(adf_inread));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_readpos",&adf_readpos,sizeof(adf_readpos));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_inwrite",&adf_inwrite,sizeof(adf_inwrite));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_inreadaddr",&adf_inreadaddr,sizeof(adf_inreadaddr));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_notfound",&adf_notfound,sizeof(adf_notfound));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_search_indexes",&adf_search_indexes,sizeof(adf_search_indexes));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_extent",&adf_extent,sizeof(adf_extent));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_informat",&adf_informat,sizeof(adf_informat));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_pause",&adf_pause,sizeof(adf_pause));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_phase_us",&adf_phase_us,sizeof(adf_phase_us));
   arc_state_region(s,"engine/arculator/src/disc_adf.c:adf_writable",&adf_writable,sizeof(adf_writable));
   arc_state_target(s,&adf_disc_funcs,ARC_STATE_REFERENCE_DISC_FUNCTIONS);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_disc_funcs");
   arc_state_value(s,&((adf_disc_funcs).high_res_poll),sizeof((adf_disc_funcs).high_res_poll));
   arc_state_value(s,&((adf_disc_funcs).multiwrite),sizeof((adf_disc_funcs).multiwrite));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf");
   for (size_t ss_i0=0; ss_i0<sizeof(adf)/sizeof((adf)[0]); ++ss_i0) {
      arc_state_typed_reference(s,(void*)&(((adf)[ss_i0]).f),0,1,1,ARC_STATE_REFERENCE_FILE);
      /* Cached tracks are reconstructed from the logical medium plus dirty deltas. */
      arc_state_value(s,&(((adf)[ss_i0]).dblside),sizeof(((adf)[ss_i0]).dblside));
      arc_state_value(s,&(((adf)[ss_i0]).sectors),sizeof(((adf)[ss_i0]).sectors));
      arc_state_value(s,&(((adf)[ss_i0]).size),sizeof(((adf)[ss_i0]).size));
      arc_state_value(s,&(((adf)[ss_i0]).track),sizeof(((adf)[ss_i0]).track));
      arc_state_value(s,&(((adf)[ss_i0]).dblstep),sizeof(((adf)[ss_i0]).dblstep));
      arc_state_value(s,&(((adf)[ss_i0]).density),sizeof(((adf)[ss_i0]).density));
      arc_state_value(s,&(((adf)[ss_i0]).maxsector),sizeof(((adf)[ss_i0]).maxsector));
      arc_state_value(s,&(((adf)[ss_i0]).sector_offset),sizeof(((adf)[ss_i0]).sector_offset));
      arc_state_value(s,&(((adf)[ss_i0]).image_size),sizeof(((adf)[ss_i0]).image_size));
      arc_state_value(s,&(((adf)[ss_i0]).effective_size),sizeof(((adf)[ss_i0]).effective_size));
      arc_state_value(s,&(((adf)[ss_i0]).read_id_sector),sizeof(((adf)[ss_i0]).read_id_sector));
      arc_state_raw_cache(s,(unsigned)ss_i0);
   }
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_sector");
   arc_state_value(s,&(adf_sector),sizeof(adf_sector));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_track");
   arc_state_value(s,&(adf_track),sizeof(adf_track));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_side");
   arc_state_value(s,&(adf_side),sizeof(adf_side));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_drive");
   arc_state_value(s,&(adf_drive),sizeof(adf_drive));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_inread");
   arc_state_value(s,&(adf_inread),sizeof(adf_inread));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_readpos");
   arc_state_value(s,&(adf_readpos),sizeof(adf_readpos));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_inwrite");
   arc_state_value(s,&(adf_inwrite),sizeof(adf_inwrite));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_inreadaddr");
   arc_state_value(s,&(adf_inreadaddr),sizeof(adf_inreadaddr));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_notfound");
   arc_state_value(s,&(adf_notfound),sizeof(adf_notfound));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_search_indexes");
   arc_state_value(s,&(adf_search_indexes),sizeof(adf_search_indexes));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_extent");
   arc_state_value(s,&(adf_extent),sizeof(adf_extent));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_informat");
   arc_state_value(s,&(adf_informat),sizeof(adf_informat));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_pause");
   arc_state_value(s,&(adf_pause),sizeof(adf_pause));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_phase_us");
   arc_state_value(s,&(adf_phase_us),sizeof(adf_phase_us));
   arc_state_object(s,"engine/arculator/src/disc_adf.c:adf_writable");
   arc_state_value(s,&(adf_writable),sizeof(adf_writable));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,adf_drive,0,3);ARC_STATE_BOUND(s,adf_side,0,1);ARC_STATE_BOUND(s,adf_readpos,0,20*1024);
      int drive,side,sector,position,active_read,active_write,active_format;
      arc_state_project(s,&drive,&adf_drive,sizeof(drive));arc_state_project(s,&side,&adf_side,sizeof(side));
      arc_state_project(s,&sector,&adf_sector,sizeof(sector));arc_state_project(s,&position,&adf_readpos,sizeof(position));
      arc_state_project(s,&active_read,&adf_inread,sizeof(active_read));arc_state_project(s,&active_write,&adf_inwrite,sizeof(active_write));arc_state_project(s,&active_format,&adf_informat,sizeof(active_format));
      if(!arc_state_error(s)&&(active_read||active_write||active_format)) {
         int size;arc_state_project(s,&size,&adf[drive].size,sizeof(size));
         if(size<=0||size>20*1024||sector<0||(uint64_t)sector*(unsigned)size+(unsigned)position>=20u*1024u)arc_state_fail(s,"raw transfer extent");
      }
   }
}
