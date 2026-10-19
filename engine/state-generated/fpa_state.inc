/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_fpa(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/fpa.c:fparegs",&fparegs,sizeof(fparegs));
   arc_state_region(s,"engine/arculator/src/fpa.c:fpsr",&fpsr,sizeof(fpsr));
   arc_state_region(s,"engine/arculator/src/fpa.c:fpcr",&fpcr,sizeof(fpcr));
   arc_state_region(s,"engine/arculator/src/fpa.c:fpu_type",&fpu_type,sizeof(fpu_type));
   arc_state_region(s,"engine/arculator/src/fpa.c:f32",&f32,sizeof(f32));
   arc_state_region(s,"engine/arculator/src/fpa.c:f64",&f64,sizeof(f64));
   arc_state_region(s,"engine/arculator/src/fpa.c:__x",&__x,sizeof(__x));
   arc_state_region(s,"engine/arculator/src/fpa.c:__y",&__y,sizeof(__y));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/fpa.c:fparegs");
   arc_state_value(s,&(fparegs),sizeof(fparegs));
   arc_state_object(s,"engine/arculator/src/fpa.c:fpsr");
   arc_state_value(s,&(fpsr),sizeof(fpsr));
   arc_state_object(s,"engine/arculator/src/fpa.c:fpcr");
   arc_state_value(s,&(fpcr),sizeof(fpcr));
   arc_state_object(s,"engine/arculator/src/fpa.c:fpu_type");
   arc_state_value(s,&(fpu_type),sizeof(fpu_type));
   arc_state_object(s,"engine/arculator/src/fpa.c:f32");
   arc_state_value(s,&(f32),sizeof(f32));
   arc_state_object(s,"engine/arculator/src/fpa.c:f64");
   arc_state_value(s,&(f64),sizeof(f64));
   arc_state_object(s,"engine/arculator/src/fpa.c:__x");
   arc_state_value(s,&(__x),sizeof(__x));
   arc_state_object(s,"engine/arculator/src/fpa.c:__y");
   arc_state_value(s,&(__y),sizeof(__y));
}
