/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_wd1770(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/wd1770.c:wd1770_fdc_funcs",&wd1770_fdc_funcs,sizeof(wd1770_fdc_funcs));
   arc_state_region(s,"engine/arculator/src/wd1770.c:wd1770",&wd1770,sizeof(wd1770));
   arc_state_target(s,&wd1770.timer,ARC_STATE_REFERENCE_TIMER);
   arc_state_target(s,&wd1770,ARC_STATE_REFERENCE_CONTROLLER);
   arc_state_target(s,&wd1770_fdc_funcs,ARC_STATE_REFERENCE_FDC_FUNCTIONS);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/wd1770.c:wd1770_fdc_funcs");
   arc_state_object(s,"engine/arculator/src/wd1770.c:wd1770");
   arc_state_value(s,&((wd1770).command),sizeof((wd1770).command));
   arc_state_value(s,&((wd1770).sector),sizeof((wd1770).sector));
   arc_state_value(s,&((wd1770).track),sizeof((wd1770).track));
   arc_state_value(s,&((wd1770).status),sizeof((wd1770).status));
   arc_state_value(s,&((wd1770).data),sizeof((wd1770).data));
   arc_state_value(s,&((wd1770).ctrl),sizeof((wd1770).ctrl));
   arc_state_value(s,&((wd1770).curside),sizeof((wd1770).curside));
   arc_state_value(s,&((wd1770).curtrack),sizeof((wd1770).curtrack));
   arc_state_value(s,&((wd1770).density),sizeof((wd1770).density));
   arc_state_value(s,&((wd1770).written),sizeof((wd1770).written));
   arc_state_value(s,&((wd1770).stepdir),sizeof((wd1770).stepdir));
   arc_state_value(s,&((wd1770).index_count),sizeof((wd1770).index_count));
   arc_state_value(s,&((wd1770).rnf_detection),sizeof((wd1770).rnf_detection));
   arc_state_value(s,&(((wd1770).timer).ts_integer),sizeof(((wd1770).timer).ts_integer));
   arc_state_value(s,&(((wd1770).timer).ts_frac),sizeof(((wd1770).timer).ts_frac));
   arc_state_value(s,&(((wd1770).timer).enabled),sizeof(((wd1770).timer).enabled));
   arc_state_typed_reference(s,(void*)&(((wd1770).timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((wd1770).timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
}
