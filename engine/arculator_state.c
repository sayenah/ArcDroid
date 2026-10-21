/* ArcDroid Arculator snapshot traversal. GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_82c711(ArcState *s);
void arc_state_visit_engine_arculator_src_82c711_fdc(ArcState *s);
void arc_state_visit_engine_arculator_src_arm(ArcState *s);
void arc_state_visit_engine_arculator_src_cmos(ArcState *s);
void arc_state_visit_engine_arculator_src_cp15(ArcState *s);
void arc_state_visit_engine_arculator_src_disc(ArcState *s);
void arc_state_visit_engine_arculator_src_disc_adf(ArcState *s);
void arc_state_visit_engine_arculator_src_disc_jfd(ArcState *s);
void arc_state_visit_engine_arculator_src_ds2401(ArcState *s);
void arc_state_visit_engine_arculator_src_fpa(ArcState *s);
void arc_state_visit_engine_arculator_src_ide(ArcState *s);
void arc_state_visit_engine_arculator_src_ioc(ArcState *s);
void arc_state_visit_engine_arculator_src_ioeb(ArcState *s);
void arc_state_visit_engine_arculator_src_joystick(ArcState *s);
void arc_state_visit_engine_arculator_src_keyboard(ArcState *s);
void arc_state_visit_engine_arculator_src_lc(ArcState *s);
void arc_state_visit_engine_arculator_src_mem(ArcState *s);
void arc_state_visit_engine_arculator_src_memc(ArcState *s);
void arc_state_visit_engine_arculator_src_printer(ArcState *s);
void arc_state_visit_engine_arculator_src_sound(ArcState *s);
void arc_state_visit_engine_arculator_src_timer(ArcState *s);
void arc_state_visit_engine_arculator_src_vidc(ArcState *s);
void arc_state_visit_engine_arculator_src_wd1770(ArcState *s);
void arc_state_visit_engine_arculator_backend(ArcState *s);
void arc_state_visit_libretro_arcdroid_libretro(ArcState *s);
void arc_state_visit_platform_arcdroid_input(ArcState *s);
void arc_state_visit_platform_arcdroid_joystick(ArcState *s);
void arc_state_visit_platform_arcdroid_sound(ArcState *s);
void arc_state_visit_platform_arcdroid_video(ArcState *s);
void arc_state_visit_engine_arculator_src_arm_extra(ArcState *s);
void arc_state_visit_engine_arculator_src_mem_extra(ArcState *s);
void arc_state_visit_engine_arculator_src_vidc_extra(ArcState *s);
void arc_state_visit_platform_arcdroid_video_extra(ArcState *s);
void arc_state_visit_engine_arculator_src_disc_adf_extra(ArcState *s);
void arc_state_validate_timers(ArcState *s);
void arc_arculator_static_visit(ArcState *s)
{
   arc_state_visit_engine_arculator_src_82c711(s);
   arc_state_visit_engine_arculator_src_82c711_fdc(s);
   arc_state_visit_engine_arculator_src_arm(s);
   arc_state_visit_engine_arculator_src_cmos(s);
   arc_state_visit_engine_arculator_src_cp15(s);
   arc_state_visit_engine_arculator_src_disc(s);
   arc_state_visit_engine_arculator_src_disc_adf(s);
   arc_state_visit_engine_arculator_src_disc_jfd(s);
   arc_state_visit_engine_arculator_src_ds2401(s);
   arc_state_visit_engine_arculator_src_fpa(s);
   arc_state_visit_engine_arculator_src_ide(s);
   arc_state_visit_engine_arculator_src_ioc(s);
   arc_state_visit_engine_arculator_src_ioeb(s);
   arc_state_visit_engine_arculator_src_joystick(s);
   arc_state_visit_engine_arculator_src_keyboard(s);
   arc_state_visit_engine_arculator_src_lc(s);
   arc_state_visit_engine_arculator_src_mem(s);
   arc_state_visit_engine_arculator_src_memc(s);
   arc_state_visit_engine_arculator_src_printer(s);
   arc_state_visit_engine_arculator_src_sound(s);
   arc_state_visit_engine_arculator_src_timer(s);
   arc_state_visit_engine_arculator_src_vidc(s);
   arc_state_visit_engine_arculator_src_wd1770(s);
   arc_state_visit_engine_arculator_backend(s);

   arc_state_visit_platform_arcdroid_input(s);
   arc_state_visit_platform_arcdroid_joystick(s);
   arc_state_visit_platform_arcdroid_sound(s);
   arc_state_visit_platform_arcdroid_video(s);
 }
void arc_arculator_state_visit(ArcState *s)
{
   arc_arculator_static_visit(s);
   arc_state_visit_engine_arculator_src_arm_extra(s);
   arc_state_visit_engine_arculator_src_mem_extra(s);
   arc_state_visit_engine_arculator_src_disc_adf_extra(s);
   arc_state_visit_engine_arculator_src_vidc_extra(s);
   arc_state_visit_platform_arcdroid_video_extra(s);
   arc_state_validate_timers(s);
}
