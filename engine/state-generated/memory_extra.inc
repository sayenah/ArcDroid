/* ArcDroid save-state memory bindings. GPL-2.0-only. */
static void arc_state_memory_extra(ArcState *s)
{
   arc_state_buffer(s,"memory.ram",ram,(size_t)realmemsize*1024u);
   arc_state_region(s,"memory.rom",rom,0x200000u);
   arc_state_region(s,"memory.arcrom",rom_arcrom,0x10000u);
   arc_state_region(s,"memory.fifth",rom_5th_column,0x20000u);
   if(arc_state_pass(s)) {
      arc_state_object(s,"engine/arculator/src/mem.c:mempoint");
      arc_state_reference_array(s,mempoint,0x4000u,4096u,4096u,4096u,ARC_STATE_REFERENCE_MEMORY);
   }
}
