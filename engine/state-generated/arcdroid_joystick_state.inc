/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_platform_arcdroid_joystick(ArcState *s)
{
   arc_state_region(s,"platform/arcdroid_joystick.c:plat_joystick_state",&plat_joystick_state,sizeof(plat_joystick_state));
   arc_state_region(s,"platform/arcdroid_joystick.c:joystick_state",&joystick_state,sizeof(joystick_state));
   arc_state_region(s,"platform/arcdroid_joystick.c:joysticks_present",&joysticks_present,sizeof(joysticks_present));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"platform/arcdroid_joystick.c:plat_joystick_state");
   for (size_t ss_i0=0; ss_i0<sizeof(plat_joystick_state)/sizeof((plat_joystick_state)[0]); ++ss_i0) {
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).name),sizeof(((plat_joystick_state)[ss_i0]).name));
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).a),sizeof(((plat_joystick_state)[ss_i0]).a));
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).b),sizeof(((plat_joystick_state)[ss_i0]).b));
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).p),sizeof(((plat_joystick_state)[ss_i0]).p));
      for (size_t ss_i1=0; ss_i1<sizeof(((plat_joystick_state)[ss_i0]).axis)/sizeof((((plat_joystick_state)[ss_i0]).axis)[0]); ++ss_i1) {
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).axis)[ss_i1]).name),sizeof(((((plat_joystick_state)[ss_i0]).axis)[ss_i1]).name));
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).axis)[ss_i1]).id),sizeof(((((plat_joystick_state)[ss_i0]).axis)[ss_i1]).id));
      }
      for (size_t ss_i1=0; ss_i1<sizeof(((plat_joystick_state)[ss_i0]).button)/sizeof((((plat_joystick_state)[ss_i0]).button)[0]); ++ss_i1) {
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).button)[ss_i1]).name),sizeof(((((plat_joystick_state)[ss_i0]).button)[ss_i1]).name));
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).button)[ss_i1]).id),sizeof(((((plat_joystick_state)[ss_i0]).button)[ss_i1]).id));
      }
      for (size_t ss_i1=0; ss_i1<sizeof(((plat_joystick_state)[ss_i0]).pov)/sizeof((((plat_joystick_state)[ss_i0]).pov)[0]); ++ss_i1) {
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).pov)[ss_i1]).name),sizeof(((((plat_joystick_state)[ss_i0]).pov)[ss_i1]).name));
         arc_state_value(s,&(((((plat_joystick_state)[ss_i0]).pov)[ss_i1]).id),sizeof(((((plat_joystick_state)[ss_i0]).pov)[ss_i1]).id));
      }
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).nr_axes),sizeof(((plat_joystick_state)[ss_i0]).nr_axes));
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).nr_buttons),sizeof(((plat_joystick_state)[ss_i0]).nr_buttons));
      arc_state_value(s,&(((plat_joystick_state)[ss_i0]).nr_povs),sizeof(((plat_joystick_state)[ss_i0]).nr_povs));
   }
   arc_state_object(s,"platform/arcdroid_joystick.c:joystick_state");
   for (size_t ss_i0=0; ss_i0<sizeof(joystick_state)/sizeof((joystick_state)[0]); ++ss_i0) {
      arc_state_value(s,&(((joystick_state)[ss_i0]).axis),sizeof(((joystick_state)[ss_i0]).axis));
      arc_state_value(s,&(((joystick_state)[ss_i0]).button),sizeof(((joystick_state)[ss_i0]).button));
      arc_state_value(s,&(((joystick_state)[ss_i0]).pov),sizeof(((joystick_state)[ss_i0]).pov));
      arc_state_value(s,&(((joystick_state)[ss_i0]).plat_joystick_nr),sizeof(((joystick_state)[ss_i0]).plat_joystick_nr));
      arc_state_value(s,&(((joystick_state)[ss_i0]).axis_mapping),sizeof(((joystick_state)[ss_i0]).axis_mapping));
      arc_state_value(s,&(((joystick_state)[ss_i0]).button_mapping),sizeof(((joystick_state)[ss_i0]).button_mapping));
      arc_state_value(s,&(((joystick_state)[ss_i0]).pov_mapping),sizeof(((joystick_state)[ss_i0]).pov_mapping));
   }
   arc_state_object(s,"platform/arcdroid_joystick.c:joysticks_present");
   arc_state_value(s,&(joysticks_present),sizeof(joysticks_present));
}
