/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_cmos(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/cmos.c:year_original",&year_original,sizeof(year_original));
   arc_state_region(s,"engine/arculator/src/cmos.c:year_written",&year_written,sizeof(year_written));
   arc_state_region(s,"engine/arculator/src/cmos.c:year_overlay",&year_overlay,sizeof(year_overlay));
   arc_state_region(s,"engine/arculator/src/cmos.c:cmos_changed",&cmos_changed,sizeof(cmos_changed));
   arc_state_region(s,"engine/arculator/src/cmos.c:i2c_clock",&i2c_clock,sizeof(i2c_clock));
   arc_state_region(s,"engine/arculator/src/cmos.c:i2c_data",&i2c_data,sizeof(i2c_data));
   arc_state_region(s,"engine/arculator/src/cmos.c:i2c",&i2c,sizeof(i2c));
   arc_state_region(s,"engine/arculator/src/cmos.c:cmos",&cmos,sizeof(cmos));
   arc_state_region(s,"engine/arculator/src/cmos.c:systemtime",&systemtime,sizeof(systemtime));
   arc_state_target(s,&cmos.timer,ARC_STATE_REFERENCE_TIMER);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/cmos.c:year_original");
   arc_state_value(s,&year_original,sizeof(year_original));
   arc_state_object(s,"engine/arculator/src/cmos.c:year_written");
   arc_state_value(s,&year_written,sizeof(year_written));
   arc_state_object(s,"engine/arculator/src/cmos.c:year_overlay");
   arc_state_value(s,&year_overlay,sizeof(year_overlay));
   arc_state_object(s,"engine/arculator/src/cmos.c:cmos_changed");
   arc_state_value(s,&(cmos_changed),sizeof(cmos_changed));
   arc_state_object(s,"engine/arculator/src/cmos.c:i2c_clock");
   arc_state_value(s,&(i2c_clock),sizeof(i2c_clock));
   arc_state_object(s,"engine/arculator/src/cmos.c:i2c_data");
   arc_state_value(s,&(i2c_data),sizeof(i2c_data));
   arc_state_object(s,"engine/arculator/src/cmos.c:i2c");
   arc_state_value(s,&((i2c).state),sizeof((i2c).state));
   arc_state_value(s,&((i2c).last_data),sizeof((i2c).last_data));
   arc_state_value(s,&((i2c).pos),sizeof((i2c).pos));
   arc_state_value(s,&((i2c).transmit),sizeof((i2c).transmit));
   arc_state_value(s,&((i2c).byte),sizeof((i2c).byte));
   arc_state_object(s,"engine/arculator/src/cmos.c:cmos");
   arc_state_value(s,&((cmos).device_addr),sizeof((cmos).device_addr));
   arc_state_value(s,&((cmos).state),sizeof((cmos).state));
   arc_state_value(s,&((cmos).addr),sizeof((cmos).addr));
   arc_state_value(s,&((cmos).rw),sizeof((cmos).rw));
   arc_state_value(s,&((cmos).ram),sizeof((cmos).ram));
   arc_state_value(s,&((cmos).rtc_ram),sizeof((cmos).rtc_ram));
   arc_state_value(s,&(((cmos).timer).ts_integer),sizeof(((cmos).timer).ts_integer));
   arc_state_value(s,&(((cmos).timer).ts_frac),sizeof(((cmos).timer).ts_frac));
   arc_state_value(s,&(((cmos).timer).enabled),sizeof(((cmos).timer).enabled));
   arc_state_typed_reference(s,(void*)&(((cmos).timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((cmos).timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_object(s,"engine/arculator/src/cmos.c:systemtime");
   arc_state_value(s,&((systemtime).msec),sizeof((systemtime).msec));
   arc_state_value(s,&((systemtime).sec),sizeof((systemtime).sec));
   arc_state_value(s,&((systemtime).min),sizeof((systemtime).min));
   arc_state_value(s,&((systemtime).hour),sizeof((systemtime).hour));
   arc_state_value(s,&((systemtime).day),sizeof((systemtime).day));
   arc_state_value(s,&((systemtime).mon),sizeof((systemtime).mon));
   arc_state_value(s,&((systemtime).year),sizeof((systemtime).year));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,year_written,0,7);ARC_STATE_BOUND(s,year_overlay,0,1);
      ARC_STATE_BOUND(s,i2c.pos,0,8);/* Sequential transfers increment the int cursor past 255; RAM accesses mask it. */
      if(cmos.addr<0)arc_state_fail(s,"cmos.addr range");ARC_STATE_BOUND(s,systemtime.mon,1,12);ARC_STATE_BOUND(s,systemtime.day,1,31);ARC_STATE_BOUND(s,systemtime.msec,0,999);ARC_STATE_BOUND(s,systemtime.sec,0,60);ARC_STATE_BOUND(s,systemtime.min,0,59);ARC_STATE_BOUND(s,systemtime.hour,0,23);
   }
}
