/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_timer(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/timer.c:tsc",&tsc,sizeof(tsc));
   arc_state_region(s,"engine/arculator/src/timer.c:TIMER_USEC",&TIMER_USEC,sizeof(TIMER_USEC));
   arc_state_region(s,"engine/arculator/src/timer.c:timer_target",&timer_target,sizeof(timer_target));
   arc_state_region(s,"engine/arculator/src/timer.c:timer_head",&timer_head,sizeof(timer_head));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/timer.c:tsc");
   arc_state_value(s,&(tsc),sizeof(tsc));
   arc_state_object(s,"engine/arculator/src/timer.c:TIMER_USEC");
   arc_state_value(s,&(TIMER_USEC),sizeof(TIMER_USEC));
   arc_state_object(s,"engine/arculator/src/timer.c:timer_target");
   arc_state_value(s,&(timer_target),sizeof(timer_target));
   arc_state_object(s,"engine/arculator/src/timer.c:timer_head");
   arc_state_typed_reference(s,(void*)&(timer_head),0,1,1,ARC_STATE_REFERENCE_TIMER);
}

void arc_state_validate_timers(ArcState *s)
{
   if(!arc_state_loading(s)||!arc_state_pass(s)||arc_state_error(s))return;
   emu_timer_t *current,*previous=NULL;
   arc_state_project(s,&current,&timer_head,sizeof(current));
   for(unsigned visited=0;current;++visited) {
      if(visited>=32u){arc_state_fail(s,"timer cycle");return;}
      emu_timer_t *prev,*next;int enabled;
      arc_state_project(s,&prev,&current->prev,sizeof(prev));
      arc_state_project(s,&next,&current->next,sizeof(next));
      arc_state_project(s,&enabled,&current->enabled,sizeof(enabled));
      if(prev!=previous||enabled!=1||!current->callback){arc_state_fail(s,"timer linkage");return;}
      previous=current;current=next;
   }
}
