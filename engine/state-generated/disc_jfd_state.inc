/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_disc_jfd(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_disc_funcs",&jfd_disc_funcs,sizeof(jfd_disc_funcs));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_image",&jfd_image,sizeof(jfd_image));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_state",&jfd_state,sizeof(jfd_state));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_track",&jfd_track,sizeof(jfd_track));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_side",&jfd_side,sizeof(jfd_side));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_density",&jfd_density,sizeof(jfd_density));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_sector_id",&jfd_sector_id,sizeof(jfd_sector_id));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_transfer_pos",&jfd_transfer_pos,sizeof(jfd_transfer_pos));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_selected_view",&jfd_selected_view,sizeof(jfd_selected_view));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_head_track",&jfd_head_track,sizeof(jfd_head_track));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_requested_c",&jfd_requested_c,sizeof(jfd_requested_c));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_selected_side",&jfd_selected_side,sizeof(jfd_selected_side));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_phase_us",&jfd_phase_us,sizeof(jfd_phase_us));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_search_indexes",&jfd_search_indexes,sizeof(jfd_search_indexes));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_writing",&jfd_writing,sizeof(jfd_writing));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_address",&jfd_address,sizeof(jfd_address));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_write_buffer",&jfd_write_buffer,sizeof(jfd_write_buffer));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_write_wait",&jfd_write_wait,sizeof(jfd_write_wait));
   arc_state_region(s,"engine/arculator/src/disc_jfd.c:jfd_requested_size",&jfd_requested_size,sizeof(jfd_requested_size));
   arc_state_target(s,&jfd_disc_funcs,ARC_STATE_REFERENCE_DISC_FUNCTIONS);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_disc_funcs");
   arc_state_value(s,&((jfd_disc_funcs).high_res_poll),sizeof((jfd_disc_funcs).high_res_poll));
   arc_state_value(s,&((jfd_disc_funcs).multiwrite),sizeof((jfd_disc_funcs).multiwrite));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_image");
   arc_state_typed_reference(s,(void*)&(jfd_image),0,1,1,ARC_STATE_REFERENCE_JFD_IMAGE);
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_state");
   arc_state_value(s,&(jfd_state),sizeof(jfd_state));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_track");
   arc_state_value(s,&(jfd_track),sizeof(jfd_track));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_side");
   arc_state_value(s,&(jfd_side),sizeof(jfd_side));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_density");
   arc_state_value(s,&(jfd_density),sizeof(jfd_density));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_sector_id");
   arc_state_value(s,&(jfd_sector_id),sizeof(jfd_sector_id));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_transfer_pos");
   arc_state_value(s,&(jfd_transfer_pos),sizeof(jfd_transfer_pos));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_selected_view");
   arc_state_value(s,&(((jfd_selected_view).sector).time_us),sizeof(((jfd_selected_view).sector).time_us));
   arc_state_value(s,&(((jfd_selected_view).sector).data_offset),sizeof(((jfd_selected_view).sector).data_offset));
   arc_state_value(s,&(((jfd_selected_view).sector).size),sizeof(((jfd_selected_view).sector).size));
   arc_state_value(s,&(((jfd_selected_view).sector).id),sizeof(((jfd_selected_view).sector).id));
   arc_state_value(s,&(((jfd_selected_view).sector).density),sizeof(((jfd_selected_view).sector).density));
   arc_state_value(s,&(((jfd_selected_view).sector).flags),sizeof(((jfd_selected_view).sector).flags));
   ArcJfdSector selected_sector;arc_state_project(s,&selected_sector,&jfd_selected_view.sector,sizeof(selected_sector));
   if(selected_sector.size>65536u)arc_state_fail(s,"JFD selected extent");
   arc_state_typed_reference(s,(void*)&((jfd_selected_view).data),0,selected_sector.size,1,ARC_STATE_REFERENCE_JFD_DATA);
   arc_state_value(s,&((jfd_selected_view).key),sizeof((jfd_selected_view).key));
   arc_state_value(s,&((jfd_selected_view).created),sizeof((jfd_selected_view).created));
   arc_state_value(s,&((jfd_selected_view).id_visible),sizeof((jfd_selected_view).id_visible));
   arc_state_value(s,&((jfd_selected_view).write_eligible),sizeof((jfd_selected_view).write_eligible));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_head_track");
   arc_state_value(s,&(jfd_head_track),sizeof(jfd_head_track));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_requested_c");
   arc_state_value(s,&(jfd_requested_c),sizeof(jfd_requested_c));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_selected_side");
   arc_state_value(s,&(jfd_selected_side),sizeof(jfd_selected_side));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_phase_us");
   arc_state_value(s,&(jfd_phase_us),sizeof(jfd_phase_us));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_search_indexes");
   arc_state_value(s,&(jfd_search_indexes),sizeof(jfd_search_indexes));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_writing");
   arc_state_value(s,&(jfd_writing),sizeof(jfd_writing));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_address");
   arc_state_value(s,&(jfd_address),sizeof(jfd_address));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_write_buffer");
   arc_state_value(s,&(jfd_write_buffer),sizeof(jfd_write_buffer));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_write_wait");
   arc_state_value(s,&(jfd_write_wait),sizeof(jfd_write_wait));
   arc_state_object(s,"engine/arculator/src/disc_jfd.c:jfd_requested_size");
   arc_state_value(s,&(jfd_requested_size),sizeof(jfd_requested_size));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,jfd_state,JFD_IDLE,JFD_WRITE_DATA);
      JfdState state;unsigned pos;ArcJfdView view;
      arc_state_project(s,&state,&jfd_state,sizeof(state));
      arc_state_project(s,&pos,&jfd_transfer_pos,sizeof(pos));
      arc_state_project(s,&view,&jfd_selected_view,sizeof(view));
      if((state==JFD_READ_ADDRESS&&pos>=sizeof(jfd_address))||
         ((state==JFD_READ_DATA||state==JFD_WRITE_DATA)&&(!view.sector.size||pos>=view.sector.size))||
         (state==JFD_WRITE_DATA&&view.sector.size>sizeof(jfd_write_buffer)))arc_state_fail(s,"JFD transfer position");
   }

}
