/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_cp15(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/cp15.c:arm3cp",&arm3cp,sizeof(arm3cp));
   arc_state_region(s,"engine/arculator/src/cp15.c:cp15_cacheon",&cp15_cacheon,sizeof(cp15_cacheon));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/cp15.c:arm3cp");
   arc_state_value(s,&((arm3cp).ctrl),sizeof((arm3cp).ctrl));
   arc_state_value(s,&((arm3cp).cache),sizeof((arm3cp).cache));
   arc_state_value(s,&((arm3cp).update),sizeof((arm3cp).update));
   arc_state_value(s,&((arm3cp).disrupt),sizeof((arm3cp).disrupt));
   arc_state_object(s,"engine/arculator/src/cp15.c:cp15_cacheon");
   arc_state_value(s,&(cp15_cacheon),sizeof(cp15_cacheon));
}
