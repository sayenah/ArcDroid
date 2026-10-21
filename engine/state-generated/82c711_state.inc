/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_82c711(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/82c711.c:configmode",&configmode,sizeof(configmode));
   arc_state_region(s,"engine/arculator/src/82c711.c:configindex",&configindex,sizeof(configindex));
   arc_state_region(s,"engine/arculator/src/82c711.c:configregs",&configregs,sizeof(configregs));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/82c711.c:configmode");
   arc_state_value(s,&(configmode),sizeof(configmode));
   arc_state_object(s,"engine/arculator/src/82c711.c:configindex");
   arc_state_value(s,&(configindex),sizeof(configindex));
   arc_state_object(s,"engine/arculator/src/82c711.c:configregs");
   arc_state_value(s,&(configregs),sizeof(configregs));
}
