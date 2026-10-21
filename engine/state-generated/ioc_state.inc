/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_ioc(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/ioc.c:ioc",&ioc,sizeof(ioc));
   arc_state_region(s,"engine/arculator/src/ioc.c:ref8m_period",&ref8m_period,sizeof(ref8m_period));
   arc_state_region(s,"engine/arculator/src/ioc.c:keyway",&keyway,sizeof(keyway));
   arc_state_region(s,"engine/arculator/src/ioc.c:tempkey",&tempkey,sizeof(tempkey));
   arc_state_region(s,"engine/arculator/src/ioc.c:iockey",&iockey,sizeof(iockey));
   arc_state_region(s,"engine/arculator/src/ioc.c:iockey2",&iockey2,sizeof(iockey2));
   arc_state_region(s,"engine/arculator/src/ioc.c:keydelay",&keydelay,sizeof(keydelay));
   arc_state_region(s,"engine/arculator/src/ioc.c:keydelay2",&keydelay2,sizeof(keydelay2));
   arc_state_target(s,&ioc.timers[0],ARC_STATE_REFERENCE_TIMER);
   arc_state_target(s,&ioc.timers[1],ARC_STATE_REFERENCE_TIMER);
   arc_state_target(s,&ioc.timers[2],ARC_STATE_REFERENCE_TIMER);
   arc_state_target(s,&ioc.timers[3],ARC_STATE_REFERENCE_TIMER);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/ioc.c:ioc");
   arc_state_value(s,&((ioc).irqa),sizeof((ioc).irqa));
   arc_state_value(s,&((ioc).irqb),sizeof((ioc).irqb));
   arc_state_value(s,&((ioc).fiq),sizeof((ioc).fiq));
   arc_state_value(s,&((ioc).mska),sizeof((ioc).mska));
   arc_state_value(s,&((ioc).mskb),sizeof((ioc).mskb));
   arc_state_value(s,&((ioc).mskf),sizeof((ioc).mskf));
   arc_state_value(s,&((ioc).ctrl),sizeof((ioc).ctrl));
   arc_state_value(s,&((ioc).timerc),sizeof((ioc).timerc));
   arc_state_value(s,&((ioc).timerl),sizeof((ioc).timerl));
   arc_state_value(s,&((ioc).timerr),sizeof((ioc).timerr));
   for (size_t ss_i0=0; ss_i0<sizeof((ioc).timers)/sizeof(((ioc).timers)[0]); ++ss_i0) {
      arc_state_value(s,&((((ioc).timers)[ss_i0]).ts_integer),sizeof((((ioc).timers)[ss_i0]).ts_integer));
      arc_state_value(s,&((((ioc).timers)[ss_i0]).ts_frac),sizeof((((ioc).timers)[ss_i0]).ts_frac));
      arc_state_value(s,&((((ioc).timers)[ss_i0]).enabled),sizeof((((ioc).timers)[ss_i0]).enabled));
      arc_state_typed_reference(s,(void*)&((((ioc).timers)[ss_i0]).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
      arc_state_typed_reference(s,(void*)&((((ioc).timers)[ss_i0]).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
   }
   arc_state_object(s,"engine/arculator/src/ioc.c:ref8m_period");
   arc_state_value(s,&(ref8m_period),sizeof(ref8m_period));
   arc_state_object(s,"engine/arculator/src/ioc.c:keyway");
   arc_state_value(s,&(keyway),sizeof(keyway));
   arc_state_object(s,"engine/arculator/src/ioc.c:tempkey");
   arc_state_value(s,&(tempkey),sizeof(tempkey));
   arc_state_object(s,"engine/arculator/src/ioc.c:iockey");
   arc_state_value(s,&(iockey),sizeof(iockey));
   arc_state_object(s,"engine/arculator/src/ioc.c:iockey2");
   arc_state_value(s,&(iockey2),sizeof(iockey2));
   arc_state_object(s,"engine/arculator/src/ioc.c:keydelay");
   arc_state_value(s,&(keydelay),sizeof(keydelay));
   arc_state_object(s,"engine/arculator/src/ioc.c:keydelay2");
   arc_state_value(s,&(keydelay2),sizeof(keydelay2));
}
