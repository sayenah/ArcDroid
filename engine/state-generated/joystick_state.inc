/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_joystick(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/joystick.c:joystick_type",&joystick_type,sizeof(joystick_type));
   arc_state_region(s,"engine/arculator/src/joystick.c:joystick_a3010_present",&joystick_a3010_present,sizeof(joystick_a3010_present));
   arc_state_region(s,"engine/arculator/src/joystick.c:joystick_gamespad_present",&joystick_gamespad_present,sizeof(joystick_gamespad_present));
   arc_state_region(s,"engine/arculator/src/joystick.c:joystick_rtfm_present",&joystick_rtfm_present,sizeof(joystick_rtfm_present));
   arc_state_region(s,"engine/arculator/src/joystick.c:joystick_serial_port_present",&joystick_serial_port_present,sizeof(joystick_serial_port_present));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/joystick.c:joystick_type");
   arc_state_value(s,&(joystick_type),sizeof(joystick_type));
   arc_state_object(s,"engine/arculator/src/joystick.c:joystick_a3010_present");
   arc_state_value(s,&(joystick_a3010_present),sizeof(joystick_a3010_present));
   arc_state_object(s,"engine/arculator/src/joystick.c:joystick_gamespad_present");
   arc_state_value(s,&(joystick_gamespad_present),sizeof(joystick_gamespad_present));
   arc_state_object(s,"engine/arculator/src/joystick.c:joystick_rtfm_present");
   arc_state_value(s,&(joystick_rtfm_present),sizeof(joystick_rtfm_present));
   arc_state_object(s,"engine/arculator/src/joystick.c:joystick_serial_port_present");
   arc_state_value(s,&(joystick_serial_port_present),sizeof(joystick_serial_port_present));
}
