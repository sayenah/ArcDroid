/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_platform_arcdroid_sound(ArcState *s)
{
   arc_state_region(s,"platform/arcdroid_sound.c:arc_audio_queue",&arc_audio_queue,sizeof(arc_audio_queue));
   arc_state_region(s,"platform/arcdroid_sound.c:arc_audio_read",&arc_audio_read,sizeof(arc_audio_read));
   arc_state_region(s,"platform/arcdroid_sound.c:arc_audio_count",&arc_audio_count,sizeof(arc_audio_count));
   arc_state_region(s,"platform/arcdroid_sound.c:arc_audio_underrun_frames",&arc_audio_underrun_frames,sizeof(arc_audio_underrun_frames));
   arc_state_region(s,"platform/arcdroid_sound.c:arc_audio_overrun_frames",&arc_audio_overrun_frames,sizeof(arc_audio_overrun_frames));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"platform/arcdroid_sound.c:arc_audio_queue");
   arc_state_value(s,&(arc_audio_queue),sizeof(arc_audio_queue));
   arc_state_object(s,"platform/arcdroid_sound.c:arc_audio_read");
   arc_state_value(s,&(arc_audio_read),sizeof(arc_audio_read));
   arc_state_object(s,"platform/arcdroid_sound.c:arc_audio_count");
   arc_state_value(s,&(arc_audio_count),sizeof(arc_audio_count));
   arc_state_object(s,"platform/arcdroid_sound.c:arc_audio_underrun_frames");
   arc_state_value(s,&(arc_audio_underrun_frames),sizeof(arc_audio_underrun_frames));
   arc_state_object(s,"platform/arcdroid_sound.c:arc_audio_overrun_frames");
   arc_state_value(s,&(arc_audio_overrun_frames),sizeof(arc_audio_overrun_frames));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,arc_audio_read,0,ARC_AUDIO_QUEUE_FRAMES-1u);ARC_STATE_BOUND(s,arc_audio_count,0,ARC_AUDIO_QUEUE_FRAMES);
   }
}
