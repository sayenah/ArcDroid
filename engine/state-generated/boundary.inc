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
   arc_state_boundary_armed=arc_state_publication_noticed=false;
   arc_state_speculation=arc_state_rollback_failed=false;
   arc_state_mode_noticed=false;
   arc_state_media_noticed=arc_state_pending_noticed=false;
   arc_state_media_slots=0;
   /* A closing session may still have Preemptive Frames' null poll installed.
    * Capture the next primary installation, never that outgoing callback. */
   arc_state_real_poll=NULL;
   memset(arc_state_committed_hd,0,sizeof(arc_state_committed_hd));
   memset(arc_state_committed_jfd,0,sizeof(arc_state_committed_jfd));
   arc_state_io_clear();arc_state_io_forget_stores();arc_io_state_clear();
   arc_state_clear_cache();
}
static bool arc_state_callbacks_real(void)
{ return arc_input_poll==arc_state_real_poll; }
static void arc_state_boundary_save(void)
{
   arc_state_note_committed();arc_state_boundary_armed=true;
}
/* Fake frames and Preemptive Frames use retro_input_poll_null. The callback
 * setter normalizes run-ahead's restoration to the original poll-type wrapper.
 * Comparing state-input callbacks would misclassify the real logging wrapper. */
static bool arc_state_boundary_finish(void)
{
   if(arc_kind!=ARC_SESSION_KIND_ARCULATOR)return true;
   if(arc_state_speculation||arc_state_rollback_failed||
      (arc_state_boundary_armed&&!arc_state_callbacks_real()))return false;
   arc_state_io_speculate(false);
   if(!arc_state_io_pending()||arc_state_io_publish()) {arc_state_note_committed();return true;}
   if(!arc_state_publication_noticed) {
      arc_state_publication_noticed=true;
      arc_message("Disc changes could not be published; further disc writes are blocked. Reload the content to recover.",RETRO_LOG_WARN);
   }
   return false;
}
static void arc_state_boundary_frame(void)
{
   if(arc_kind!=ARC_SESSION_KIND_ARCULATOR){arc_state_io_speculate(false);return;}
   if(!arc_state_real_poll)arc_state_real_poll=arc_input_poll;
   bool real=!arc_state_boundary_armed||arc_state_callbacks_real();
   if(!real)arc_state_speculation=true;
   if(real)(void)arc_state_boundary_finish();
   arc_state_io_speculate(!real||arc_state_speculation||arc_state_rollback_failed);
}
