/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_platform_arcdroid_video(ArcState *s)
{
   arc_state_region(s,"platform/arcdroid_video.c:arc_video_frame",&arc_video_frame,sizeof(arc_video_frame));
   arc_state_region(s,"platform/arcdroid_video.c:arc_video_width",&arc_video_width,sizeof(arc_video_width));
   arc_state_region(s,"platform/arcdroid_video.c:arc_video_height",&arc_video_height,sizeof(arc_video_height));
   arc_state_region(s,"platform/arcdroid_video.c:arc_video_sequence",&arc_video_sequence,sizeof(arc_video_sequence));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"platform/arcdroid_video.c:arc_video_width");
   arc_state_value(s,&(arc_video_width),sizeof(arc_video_width));
   arc_state_object(s,"platform/arcdroid_video.c:arc_video_height");
   arc_state_value(s,&(arc_video_height),sizeof(arc_video_height));
   arc_state_object(s,"platform/arcdroid_video.c:arc_video_sequence");
   arc_state_value(s,&(arc_video_sequence),sizeof(arc_video_sequence));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,arc_video_width,1,4096);ARC_STATE_BOUND(s,arc_video_height,1,2048);
   }
}
