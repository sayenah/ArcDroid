/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_ds2401(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/ds2401.c:ds2401",&ds2401,sizeof(ds2401));
   arc_state_target(s,&ds2401.timer,ARC_STATE_REFERENCE_TIMER);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/ds2401.c:ds2401");
   arc_state_value(s,&((ds2401).old_val),sizeof((ds2401).old_val));
   arc_state_value(s,&((ds2401).old_tsc),sizeof((ds2401).old_tsc));
   arc_state_value(s,&((ds2401).rx_val),sizeof((ds2401).rx_val));
   arc_state_value(s,&((ds2401).state),sizeof((ds2401).state));
   arc_state_value(s,&((ds2401).state_pos),sizeof((ds2401).state_pos));
   arc_state_value(s,&((ds2401).command),sizeof((ds2401).command));
   arc_state_value(s,&((ds2401).id),sizeof((ds2401).id));
   arc_state_value(s,&(((ds2401).timer).ts_integer),sizeof(((ds2401).timer).ts_integer));
   arc_state_value(s,&(((ds2401).timer).ts_frac),sizeof(((ds2401).timer).ts_frac));
   arc_state_value(s,&(((ds2401).timer).enabled),sizeof(((ds2401).timer).enabled));
   arc_state_typed_reference(s,(void*)&(((ds2401).timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((ds2401).timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
}
