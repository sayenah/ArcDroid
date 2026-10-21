/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_platform_arcdroid_input(ArcState *s)
{
   arc_state_region(s,"platform/arcdroid_input.c:key",&key,sizeof(key));
   arc_state_region(s,"platform/arcdroid_input.c:arc_mouse_x",&arc_mouse_x,sizeof(arc_mouse_x));
   arc_state_region(s,"platform/arcdroid_input.c:arc_mouse_y",&arc_mouse_y,sizeof(arc_mouse_y));
   arc_state_region(s,"platform/arcdroid_input.c:arc_mouse_buttons",&arc_mouse_buttons,sizeof(arc_mouse_buttons));
   arc_state_region(s,"platform/arcdroid_input.c:arc_input_service",&arc_input_service,sizeof(arc_input_service));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"platform/arcdroid_input.c:key");
   arc_state_value(s,&(key),sizeof(key));
   arc_state_object(s,"platform/arcdroid_input.c:arc_mouse_x");
   arc_state_value(s,&(arc_mouse_x),sizeof(arc_mouse_x));
   arc_state_object(s,"platform/arcdroid_input.c:arc_mouse_y");
   arc_state_value(s,&(arc_mouse_y),sizeof(arc_mouse_y));
   arc_state_object(s,"platform/arcdroid_input.c:arc_mouse_buttons");
   arc_state_value(s,&(arc_mouse_buttons),sizeof(arc_mouse_buttons));
   arc_state_object(s,"platform/arcdroid_input.c:arc_input_service");
   arc_state_typed_reference(s,(void*)&(arc_input_service),0,1,1,ARC_STATE_REFERENCE_INPUT);
}
