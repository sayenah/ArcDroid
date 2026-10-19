/* ArcDroid save-state CPU bank and biased instruction-fetch bindings. GPL-2.0-only. */
static void arc_state_cpu_extra(ArcState *s)
{
   if(!arc_state_pass(s))return;
   arc_state_object(s,"engine/arculator/src/arm.c:usrregs");
   for(unsigned i=0;i<16u;++i)arc_state_typed_reference(s,&usrregs[i],0,4,4,ARC_STATE_REFERENCE_REGISTER);
   /* A disabled fetch cache is derived, and its stale native pointer is unused.
    * Otherwise pccache identifies the page bias of the fetch pointer. */
   arc_state_object(s,"engine/arculator/src/arm.c:pccache2");
   uint32_t page;arc_state_project(s,&page,&pccache,sizeof(page));
   if(page!=0xffffffffu&&page>=0x4000u){arc_state_fail(s,"fetch page");return;}
   if(page!=0xffffffffu)arc_state_typed_reference(s,&pccache2,(intptr_t)((uint64_t)page<<12),4,4,ARC_STATE_REFERENCE_MEMORY);
}
