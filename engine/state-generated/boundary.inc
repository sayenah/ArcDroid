/* ArcDroid RetroArch 1.22.2 single-instance publication boundary. GPL-2.0-only. */
static void arc_state_note_committed(void)
{
   if(arc_state_io_speculative()||arc_state_io_pending())return;
   arc_state_committed_hd[0]=arc_install_store_generation(arc_hd_store);
   arc_state_committed_hd[1]=arc_install_store_sequence(arc_hd_store);
   for(unsigned i=0;i<arc_media_count;++i)
      if(arc_media[i].jfd.overlay&&arc_media[i].jfd.overlay->generation>arc_state_committed_jfd[i])
         arc_state_committed_jfd[i]=arc_media[i].jfd.overlay->generation;
}
static void arc_state_boundary_reset(void)
{
   arc_state_firmware_valid=false;
   arc_state_boundary_armed=arc_state_restore_seen=arc_state_uncertain_frame=arc_state_speculative_frame_seen=false;
   arc_state_real_poll=NULL;arc_state_real_input=NULL;
   memset(arc_state_committed_hd,0,sizeof(arc_state_committed_hd));
   memset(arc_state_committed_jfd,0,sizeof(arc_state_committed_jfd));
   arc_state_io_clear();arc_state_io_forget_stores();arc_io_state_clear();
   arc_state_clear_cache();
}
static enum retro_savestate_context arc_state_context(void)
{
   enum retro_savestate_context context=RETRO_SAVESTATE_CONTEXT_NORMAL;
   if(arc_environment)arc_environment(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT,&context);
   return context;
}
static void arc_state_boundary_save(void)
{
   arc_state_note_committed();
   if(arc_state_context()==RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE) {
      if(!arc_state_boundary_armed) {
         arc_state_real_poll=arc_input_poll;arc_state_real_input=arc_input_state;
         arc_state_boundary_armed=true;
      }
      arc_state_restore_seen=false;
   }
}
static void arc_state_boundary_callback(void)
{
   /* A transition from the baseline callbacks into the speculative callbacks
    * proves that a preceding uncertain baseline frame was the real frame.
    * The normal supported sequence has already published that frame eagerly. */
   if(arc_state_boundary_armed&&arc_state_uncertain_frame&&!arc_state_speculative_frame_seen&&
      (arc_input_poll!=arc_state_real_poll||arc_input_state!=arc_state_real_input)) {
      if(arc_state_io_publish()) {arc_state_io_speculate(false);arc_state_note_committed();}
      arc_state_uncertain_frame=false;
   }
}
static void arc_state_boundary_frame(void)
{
   if(arc_kind!=ARC_SESSION_KIND_ARCULATOR){arc_state_io_speculate(false);return;}
   bool baseline=arc_input_poll==arc_state_real_poll&&arc_input_state==arc_state_real_input;
   bool real=!arc_state_boundary_armed||(baseline&&arc_state_restore_seen);
   arc_state_uncertain_frame=arc_state_boundary_armed&&baseline&&!real;
   if(arc_state_boundary_armed&&!baseline)arc_state_speculative_frame_seen=true;
   arc_state_io_speculate(!real);
   if(real&&arc_state_io_pending()) {
      if(arc_state_io_publish())arc_state_note_committed();
      else arc_message("Disc changes could not be published; save states are unavailable.",RETRO_LOG_WARN);
   }
}
