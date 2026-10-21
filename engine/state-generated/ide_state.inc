/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
void arc_state_visit_engine_arculator_src_ide(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/ide.c:ide_internal",&ide_internal,sizeof(ide_internal));
   arc_state_region(s,"engine/arculator/src/ide.c:selected_backend",&selected_backend,sizeof(selected_backend));
   arc_state_target(s,&ide_internal.timer,ARC_STATE_REFERENCE_TIMER);
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/ide.c:ide_internal");
   arc_state_value(s,&((ide_internal).status),sizeof((ide_internal).status));
   arc_state_value(s,&((ide_internal).error),sizeof((ide_internal).error));
   arc_state_value(s,&((ide_internal).features),sizeof((ide_internal).features));
   arc_state_value(s,&((ide_internal).count),sizeof((ide_internal).count));
   arc_state_value(s,&((ide_internal).sector),sizeof((ide_internal).sector));
   arc_state_value(s,&((ide_internal).cylinder_lo),sizeof((ide_internal).cylinder_lo));
   arc_state_value(s,&((ide_internal).cylinder_hi),sizeof((ide_internal).cylinder_hi));
   arc_state_value(s,&((ide_internal).head_drive),sizeof((ide_internal).head_drive));
   arc_state_value(s,&((ide_internal).device_control),sizeof((ide_internal).device_control));
   arc_state_value(s,&((ide_internal).command),sizeof((ide_internal).command));
   arc_state_value(s,&((ide_internal).position),sizeof((ide_internal).position));
   arc_state_value(s,&((ide_internal).transfer_index),sizeof((ide_internal).transfer_index));
   arc_state_value(s,&((ide_internal).transfer_count),sizeof((ide_internal).transfer_count));
   arc_state_value(s,&((ide_internal).transfer_lba),sizeof((ide_internal).transfer_lba));
   arc_state_value(s,&((ide_internal).spt),sizeof((ide_internal).spt));
   arc_state_value(s,&((ide_internal).heads),sizeof((ide_internal).heads));
   arc_state_value(s,&((ide_internal).cylinders),sizeof((ide_internal).cylinders));
   arc_state_value(s,&((ide_internal).active),sizeof((ide_internal).active));
   arc_state_value(s,&((ide_internal).write_transfer),sizeof((ide_internal).write_transfer));
   arc_state_value(s,&((ide_internal).read_only_latch),sizeof((ide_internal).read_only_latch));
   arc_state_value(s,&((ide_internal).reset_pending),sizeof((ide_internal).reset_pending));
   arc_state_value(s,&((ide_internal).timer_registered),sizeof((ide_internal).timer_registered));
   arc_state_value(s,&((ide_internal).irq_active),sizeof((ide_internal).irq_active));
   arc_state_value(s,&((ide_internal).irq_enabled),sizeof((ide_internal).irq_enabled));
   arc_state_value(s,&((ide_internal).unsupported_seen),sizeof((ide_internal).unsupported_seen));
   arc_state_value(s,&((ide_internal).sector_buffer),sizeof((ide_internal).sector_buffer));
   arc_state_value(s,&((ide_internal).transaction),sizeof((ide_internal).transaction));
   arc_state_value(s,&(((ide_internal).backend).sectors),sizeof(((ide_internal).backend).sectors));
   arc_state_value(s,&(((ide_internal).backend).writable),sizeof(((ide_internal).backend).writable));
   arc_state_value(s,&(((ide_internal).timer).ts_integer),sizeof(((ide_internal).timer).ts_integer));
   arc_state_value(s,&(((ide_internal).timer).ts_frac),sizeof(((ide_internal).timer).ts_frac));
   arc_state_value(s,&(((ide_internal).timer).enabled),sizeof(((ide_internal).timer).enabled));
   arc_state_typed_reference(s,(void*)&(((ide_internal).timer).prev),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_typed_reference(s,(void*)&(((ide_internal).timer).next),0,1,1,ARC_STATE_REFERENCE_TIMER);
   arc_state_object(s,"engine/arculator/src/ide.c:selected_backend");
   arc_state_value(s,&((selected_backend).sectors),sizeof((selected_backend).sectors));
   arc_state_value(s,&((selected_backend).writable),sizeof((selected_backend).writable));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,ide_internal.position,0,ARC_IDE_SECTOR_SIZE);ARC_STATE_BOUND(s,ide_internal.transfer_count,0,ARC_IDE_MAX_SECTORS);ARC_STATE_BOUND(s,ide_internal.transfer_index,0,ARC_IDE_MAX_SECTORS);
   }
}
