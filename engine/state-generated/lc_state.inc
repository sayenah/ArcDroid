/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_lc(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/lc.c:lc",&lc,sizeof(lc));
   arc_state_target(s,&lc.blank_timer,ARC_STATE_REFERENCE_TIMER);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/lc.c:lc");
   arc_state_value(s,&((lc).ram),sizeof((lc).ram));
   arc_state_value(s,&((lc).wp),sizeof((lc).wp));
   arc_state_value(s,&((lc).vdsr),sizeof((lc).vdsr));
   arc_state_value(s,&((lc).vdlr),sizeof((lc).vdlr));
   arc_state_value(s,&((lc).hdsr),sizeof((lc).hdsr));
   arc_state_value(s,&((lc).hdlr),sizeof((lc).hdlr));
   arc_state_value(s,&((lc).licr),sizeof((lc).licr));
   arc_state_value(s,&((lc).v_delay),sizeof((lc).v_delay));
   arc_state_value(s,&((lc).v_display),sizeof((lc).v_display));
   arc_state_value(s,&((lc).vc),sizeof((lc).vc));
   arc_state_value(s,&((lc).pal),sizeof((lc).pal));
   arc_state_typed_reference(s,(void*)&((lc).lc_buffer),0,1,1,ARC_STATE_REFERENCE_NULL);
   arc_state_value(s,&((lc).has_updated),sizeof((lc).has_updated));
   arc_state_value(s,&(((lc).blank_timer).ts_integer),sizeof(((lc).blank_timer).ts_integer));
   arc_state_value(s,&(((lc).blank_timer).ts_frac),sizeof(((lc).blank_timer).ts_frac));
   arc_state_value(s,&(((lc).blank_timer).enabled),sizeof(((lc).blank_timer).enabled));
   arc_state_typed_reference(s,(void*)&(((lc).blank_timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((lc).blank_timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
}
