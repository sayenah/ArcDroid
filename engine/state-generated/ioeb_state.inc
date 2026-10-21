/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_ioeb(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/ioeb.c:hs_invert",&hs_invert,sizeof(hs_invert));
   arc_state_region(s,"engine/arculator/src/ioeb.c:has_joystick_ports",&has_joystick_ports,sizeof(has_joystick_ports));
   arc_state_region(s,"engine/arculator/src/ioeb.c:ioeb_clock_select",&ioeb_clock_select,sizeof(ioeb_clock_select));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/ioeb.c:hs_invert");
   arc_state_value(s,&(hs_invert),sizeof(hs_invert));
   arc_state_object(s,"engine/arculator/src/ioeb.c:has_joystick_ports");
   arc_state_value(s,&(has_joystick_ports),sizeof(has_joystick_ports));
   arc_state_object(s,"engine/arculator/src/ioeb.c:ioeb_clock_select");
   arc_state_value(s,&(ioeb_clock_select),sizeof(ioeb_clock_select));
}
