/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_printer(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/printer.c:gamespad",&gamespad,sizeof(gamespad));
   arc_state_region(s,"engine/arculator/src/printer.c:printer_busy",&printer_busy,sizeof(printer_busy));
   arc_state_region(s,"engine/arculator/src/printer.c:printer_ack",&printer_ack,sizeof(printer_ack));
   arc_state_region(s,"engine/arculator/src/printer.c:printer_irq_pending",&printer_irq_pending,sizeof(printer_irq_pending));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/printer.c:gamespad");
   arc_state_value(s,&((gamespad).reset),sizeof((gamespad).reset));
   arc_state_value(s,&((gamespad).clock),sizeof((gamespad).clock));
   arc_state_value(s,&((gamespad).data),sizeof((gamespad).data));
   arc_state_value(s,&((gamespad).data_out),sizeof((gamespad).data_out));
   arc_state_object(s,"engine/arculator/src/printer.c:printer_busy");
   arc_state_value(s,&(printer_busy),sizeof(printer_busy));
   arc_state_object(s,"engine/arculator/src/printer.c:printer_ack");
   arc_state_value(s,&(printer_ack),sizeof(printer_ack));
   arc_state_object(s,"engine/arculator/src/printer.c:printer_irq_pending");
   arc_state_value(s,&(printer_irq_pending),sizeof(printer_irq_pending));
}
