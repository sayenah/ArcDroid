/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_82c711_fdc(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/82c711_fdc.c:c82c711_fdc_funcs",&c82c711_fdc_funcs,sizeof(c82c711_fdc_funcs));
   arc_state_region(s,"engine/arculator/src/82c711_fdc.c:_fdc",&_fdc,sizeof(_fdc));
   arc_state_region(s,"engine/arculator/src/82c711_fdc.c:c82c711_fdc_initialized",&c82c711_fdc_initialized,sizeof(c82c711_fdc_initialized));
   arc_state_target(s,&_fdc.timer,ARC_STATE_REFERENCE_TIMER);
   arc_state_target(s,&_fdc,ARC_STATE_REFERENCE_CONTROLLER);
   arc_state_target(s,&c82c711_fdc_funcs,ARC_STATE_REFERENCE_FDC_FUNCTIONS);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/82c711_fdc.c:c82c711_fdc_funcs");
   arc_state_object(s,"engine/arculator/src/82c711_fdc.c:_fdc");
   arc_state_value(s,&((_fdc).dor),sizeof((_fdc).dor));
   arc_state_value(s,&((_fdc).stat),sizeof((_fdc).stat));
   arc_state_value(s,&((_fdc).command),sizeof((_fdc).command));
   arc_state_value(s,&((_fdc).dat),sizeof((_fdc).dat));
   arc_state_value(s,&((_fdc).st0),sizeof((_fdc).st0));
   arc_state_value(s,&((_fdc).head),sizeof((_fdc).head));
   arc_state_value(s,&((_fdc).track),sizeof((_fdc).track));
   arc_state_value(s,&((_fdc).sector),sizeof((_fdc).sector));
   arc_state_value(s,&((_fdc).drive),sizeof((_fdc).drive));
   arc_state_value(s,&((_fdc).lastdrive),sizeof((_fdc).lastdrive));
   arc_state_value(s,&((_fdc).pos),sizeof((_fdc).pos));
   arc_state_value(s,&((_fdc).params),sizeof((_fdc).params));
   arc_state_value(s,&((_fdc).res),sizeof((_fdc).res));
   arc_state_value(s,&((_fdc).pnum),sizeof((_fdc).pnum));
   arc_state_value(s,&((_fdc).ptot),sizeof((_fdc).ptot));
   arc_state_value(s,&((_fdc).rate),sizeof((_fdc).rate));
   arc_state_value(s,&((_fdc).density),sizeof((_fdc).density));
   arc_state_value(s,&((_fdc).specify),sizeof((_fdc).specify));
   arc_state_value(s,&((_fdc).eot),sizeof((_fdc).eot));
   arc_state_value(s,&((_fdc).lock),sizeof((_fdc).lock));
   arc_state_value(s,&((_fdc).perp),sizeof((_fdc).perp));
   arc_state_value(s,&((_fdc).config),sizeof((_fdc).config));
   arc_state_value(s,&((_fdc).pretrk),sizeof((_fdc).pretrk));
   arc_state_value(s,&((_fdc).dma_dat),sizeof((_fdc).dma_dat));
   arc_state_value(s,&((_fdc).written),sizeof((_fdc).written));
   arc_state_value(s,&((_fdc).tc),sizeof((_fdc).tc));
   arc_state_value(s,&((_fdc).format_dat),sizeof((_fdc).format_dat));
   arc_state_value(s,&((_fdc).format_state),sizeof((_fdc).format_state));
   arc_state_value(s,&((_fdc).data_ready),sizeof((_fdc).data_ready));
   arc_state_value(s,&((_fdc).inread),sizeof((_fdc).inread));
   arc_state_value(s,&((_fdc).discint),sizeof((_fdc).discint));
   arc_state_value(s,&((_fdc).paramstogo),sizeof((_fdc).paramstogo));
   arc_state_value(s,&((_fdc).fdc_reset_stat),sizeof((_fdc).fdc_reset_stat));
   arc_state_value(s,&(((_fdc).timer).ts_integer),sizeof(((_fdc).timer).ts_integer));
   arc_state_value(s,&(((_fdc).timer).ts_frac),sizeof(((_fdc).timer).ts_frac));
   arc_state_value(s,&(((_fdc).timer).enabled),sizeof(((_fdc).timer).enabled));
   arc_state_typed_reference(s,(void*)&(((_fdc).timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((_fdc).timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_object(s,"engine/arculator/src/82c711_fdc.c:c82c711_fdc_initialized");
   arc_state_value(s,&(c82c711_fdc_initialized),sizeof(c82c711_fdc_initialized));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,_fdc.drive,0,3);ARC_STATE_BOUND(s,_fdc.lastdrive,0,3);ARC_STATE_BOUND(s,_fdc.head,0,1);ARC_STATE_BOUND(s,_fdc.pnum,0,16);ARC_STATE_BOUND(s,_fdc.ptot,0,16);ARC_STATE_BOUND(s,_fdc.pos,0,256);ARC_STATE_BOUND(s,_fdc.paramstogo,0,16);
   }
}
