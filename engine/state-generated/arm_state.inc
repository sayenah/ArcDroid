/* Generated from the reviewed SS-1 inventory; GPL-2.0-only. */
#include "arc_state.h"
/* resetarm() constructs these immutable instruction lookup tables. Rebuild
 * their exact values without invoking reset or changing CPU execution state. */
static void arc_state_cpu_tables_rebuild(void *unused)
{
   (void)unused;
   const unsigned char bits[16]={0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
   for(unsigned i=0;i<65536u;++i)
      countbitstable[i]=4*(bits[i&15u]+bits[(i>>4u)&15u]+bits[(i>>8u)&15u]+bits[i>>12u]);
   for(unsigned i=0;i<4096u;++i) {
      unsigned value=i&255u,rotate=((i>>8u)&15u)*2u;
      rotatelookup[i]=rotate?(value>>rotate)|(value<<(32u-rotate)):value;
   }
}
#if defined(ARCDROID_TEST_FAULTS)
bool arc_engine_test_state_tables(void)
{
   for(unsigned i=0;i<65536u;++i) {
      unsigned count=0;
      for(unsigned bit=0;bit<16u;++bit)count+=(i>>bit)&1u;
      if(countbitstable[i]!=(int)(count*4u))return false;
   }
   for(unsigned i=0;i<4096u;++i) {
      uint32_t value=i&255u;
      for(unsigned bit=0;bit<((i>>8u)&15u)*2u;++bit)value=(value>>1u)|(value<<31u);
      if(rotatelookup[i]!=value)return false;
   }
   for(unsigned i=0;i<16384u;++i) {
      unsigned start=i<0x3000u?0u:i<0x3800u?0x3000u:0x3800u;
      if(memcmp(mem_speed[i],mem_speed[start],sizeof(mem_speed[0])))return false;
   }
   return true;
}
#endif
void arc_state_visit_engine_arculator_src_arm(ArcState *s)
{
   arc_state_region(s,"engine/arculator/src/arm.c:armregs",&armregs,sizeof(armregs));
   arc_state_region(s,"engine/arculator/src/arm.c:arm3_cache",&arm3_cache,sizeof(arm3_cache));
   arc_state_region(s,"engine/arculator/src/arm.c:arm3_cache_tag",&arm3_cache_tag,sizeof(arm3_cache_tag));
   arc_state_region(s,"engine/arculator/src/arm.c:arm3_slot",&arm3_slot,sizeof(arm3_slot));
   arc_state_region(s,"engine/arculator/src/arm.c:cyc_s",&cyc_s,sizeof(cyc_s));
   arc_state_region(s,"engine/arculator/src/arm.c:cyc_n",&cyc_n,sizeof(cyc_n));
   arc_state_region(s,"engine/arculator/src/arm.c:opcode",&opcode,sizeof(opcode));
   arc_state_region(s,"engine/arculator/src/arm.c:opcode2",&opcode2,sizeof(opcode2));
   arc_state_region(s,"engine/arculator/src/arm.c:opcode3",&opcode3,sizeof(opcode3));
   arc_state_region(s,"engine/arculator/src/arm.c:usrregs",&usrregs,sizeof(usrregs));
   arc_state_region(s,"engine/arculator/src/arm.c:userregs",&userregs,sizeof(userregs));
   arc_state_region(s,"engine/arculator/src/arm.c:superregs",&superregs,sizeof(superregs));
   arc_state_region(s,"engine/arculator/src/arm.c:fiqregs",&fiqregs,sizeof(fiqregs));
   arc_state_region(s,"engine/arculator/src/arm.c:irqregs",&irqregs,sizeof(irqregs));
   arc_state_region(s,"engine/arculator/src/arm.c:vidc_fetches",&vidc_fetches,sizeof(vidc_fetches));
   arc_state_region(s,"engine/arculator/src/arm.c:databort",&databort,sizeof(databort));
   arc_state_region(s,"engine/arculator/src/arm.c:prefabort",&prefabort,sizeof(prefabort));
   arc_state_region(s,"engine/arculator/src/arm.c:prefabort_next",&prefabort_next,sizeof(prefabort_next));
   arc_state_region(s,"engine/arculator/src/arm.c:clock_domain",&clock_domain,sizeof(clock_domain));
   arc_state_region(s,"engine/arculator/src/arm.c:mem_available_ts",&mem_available_ts,sizeof(mem_available_ts));
   arc_state_region(s,"engine/arculator/src/arm.c:refresh_ts",&refresh_ts,sizeof(refresh_ts));
   arc_state_region(s,"engine/arculator/src/arm.c:pending_reads",&pending_reads,sizeof(pending_reads));
   arc_state_region(s,"engine/arculator/src/arm.c:cache_fill_addr",&cache_fill_addr,sizeof(cache_fill_addr));
   arc_state_region(s,"engine/arculator/src/arm.c:min_timer",&min_timer,sizeof(min_timer));
   arc_state_region(s,"engine/arculator/src/arm.c:next_dma_source",&next_dma_source,sizeof(next_dma_source));
   arc_state_region(s,"engine/arculator/src/arm.c:last_cycle_length",&last_cycle_length,sizeof(last_cycle_length));
   arc_state_region(s,"engine/arculator/src/arm.c:cache_was_on",&cache_was_on,sizeof(cache_was_on));
   arc_state_region(s,"engine/arculator/src/arm.c:promote_fetch_to_n",&promote_fetch_to_n,sizeof(promote_fetch_to_n));
   arc_state_region(s,"engine/arculator/src/arm.c:arm_cpu_type",&arm_cpu_type,sizeof(arm_cpu_type));
   arc_state_region(s,"engine/arculator/src/arm.c:arm_cpu_speed",&arm_cpu_speed,sizeof(arm_cpu_speed));
   arc_state_region(s,"engine/arculator/src/arm.c:arm_mem_speed",&arm_mem_speed,sizeof(arm_mem_speed));
   arc_state_region(s,"engine/arculator/src/arm.c:arm_has_swp",&arm_has_swp,sizeof(arm_has_swp));
   arc_state_region(s,"engine/arculator/src/arm.c:arm_has_cp15",&arm_has_cp15,sizeof(arm_has_cp15));
   arc_state_region(s,"engine/arculator/src/arm.c:fpaena",&fpaena,sizeof(fpaena));
   arc_state_region(s,"engine/arculator/src/arm.c:irq",&irq,sizeof(irq));
   arc_state_region(s,"engine/arculator/src/arm.c:flaglookup",&flaglookup,sizeof(flaglookup));
   arc_state_region(s,"engine/arculator/src/arm.c:rotatelookup",&rotatelookup,sizeof(rotatelookup));
   arc_state_region(s,"engine/arculator/src/arm.c:armirq",&armirq,sizeof(armirq));
   arc_state_region(s,"engine/arculator/src/arm.c:ins",&ins,sizeof(ins));
   arc_state_region(s,"engine/arculator/src/arm.c:osmode",&osmode,sizeof(osmode));
   arc_state_region(s,"engine/arculator/src/arm.c:mode",&mode,sizeof(mode));
   arc_state_region(s,"engine/arculator/src/arm.c:pccache",&pccache,sizeof(pccache));
   arc_state_region(s,"engine/arculator/src/arm.c:pccache2",&pccache2,sizeof(pccache2));
   arc_state_region(s,"engine/arculator/src/arm.c:countbitstable",&countbitstable,sizeof(countbitstable));
   arc_state_region(s,"engine/arculator/src/arm.c:total_cycles",&total_cycles,sizeof(total_cycles));
   if (!arc_state_pass(s)) return;
   arc_state_object(s,"engine/arculator/src/arm.c:armregs");
   arc_state_value(s,&(armregs),sizeof(armregs));
   arc_state_object(s,"engine/arculator/src/arm.c:arm3_cache");
   arc_state_value(s,&(arm3_cache),sizeof(arm3_cache));
   arc_state_object(s,"engine/arculator/src/arm.c:arm3_cache_tag");
   arc_state_value(s,&(arm3_cache_tag),sizeof(arm3_cache_tag));
   arc_state_object(s,"engine/arculator/src/arm.c:arm3_slot");
   arc_state_value(s,&(arm3_slot),sizeof(arm3_slot));
   arc_state_object(s,"engine/arculator/src/arm.c:cyc_s");
   arc_state_value(s,&(cyc_s),sizeof(cyc_s));
   arc_state_object(s,"engine/arculator/src/arm.c:cyc_n");
   arc_state_value(s,&(cyc_n),sizeof(cyc_n));
   arc_state_object(s,"engine/arculator/src/arm.c:opcode");
   arc_state_value(s,&(opcode),sizeof(opcode));
   arc_state_object(s,"engine/arculator/src/arm.c:opcode2");
   arc_state_value(s,&(opcode2),sizeof(opcode2));
   arc_state_object(s,"engine/arculator/src/arm.c:opcode3");
   arc_state_value(s,&(opcode3),sizeof(opcode3));
   arc_state_object(s,"engine/arculator/src/arm.c:userregs");
   arc_state_value(s,&(userregs),sizeof(userregs));
   arc_state_object(s,"engine/arculator/src/arm.c:superregs");
   arc_state_value(s,&(superregs),sizeof(superregs));
   arc_state_object(s,"engine/arculator/src/arm.c:fiqregs");
   arc_state_value(s,&(fiqregs),sizeof(fiqregs));
   arc_state_object(s,"engine/arculator/src/arm.c:irqregs");
   arc_state_value(s,&(irqregs),sizeof(irqregs));
   arc_state_object(s,"engine/arculator/src/arm.c:vidc_fetches");
   arc_state_value(s,&(vidc_fetches),sizeof(vidc_fetches));
   arc_state_object(s,"engine/arculator/src/arm.c:databort");
   arc_state_value(s,&(databort),sizeof(databort));
   arc_state_object(s,"engine/arculator/src/arm.c:prefabort");
   arc_state_value(s,&(prefabort),sizeof(prefabort));
   arc_state_object(s,"engine/arculator/src/arm.c:prefabort_next");
   arc_state_value(s,&(prefabort_next),sizeof(prefabort_next));
   arc_state_object(s,"engine/arculator/src/arm.c:clock_domain");
   arc_state_value(s,&(clock_domain),sizeof(clock_domain));
   arc_state_object(s,"engine/arculator/src/arm.c:mem_available_ts");
   arc_state_value(s,&(mem_available_ts),sizeof(mem_available_ts));
   arc_state_object(s,"engine/arculator/src/arm.c:refresh_ts");
   arc_state_value(s,&(refresh_ts),sizeof(refresh_ts));
   arc_state_object(s,"engine/arculator/src/arm.c:pending_reads");
   arc_state_value(s,&(pending_reads),sizeof(pending_reads));
   arc_state_object(s,"engine/arculator/src/arm.c:cache_fill_addr");
   arc_state_value(s,&(cache_fill_addr),sizeof(cache_fill_addr));
   arc_state_object(s,"engine/arculator/src/arm.c:min_timer");
   arc_state_value(s,&(min_timer),sizeof(min_timer));
   arc_state_object(s,"engine/arculator/src/arm.c:next_dma_source");
   arc_state_value(s,&(next_dma_source),sizeof(next_dma_source));
   arc_state_object(s,"engine/arculator/src/arm.c:last_cycle_length");
   arc_state_value(s,&(last_cycle_length),sizeof(last_cycle_length));
   arc_state_object(s,"engine/arculator/src/arm.c:cache_was_on");
   arc_state_value(s,&(cache_was_on),sizeof(cache_was_on));
   arc_state_object(s,"engine/arculator/src/arm.c:promote_fetch_to_n");
   arc_state_value(s,&(promote_fetch_to_n),sizeof(promote_fetch_to_n));
   arc_state_object(s,"engine/arculator/src/arm.c:arm_cpu_type");
   arc_state_value(s,&(arm_cpu_type),sizeof(arm_cpu_type));
   arc_state_object(s,"engine/arculator/src/arm.c:arm_cpu_speed");
   arc_state_value(s,&(arm_cpu_speed),sizeof(arm_cpu_speed));
   arc_state_object(s,"engine/arculator/src/arm.c:arm_mem_speed");
   arc_state_value(s,&(arm_mem_speed),sizeof(arm_mem_speed));
   arc_state_object(s,"engine/arculator/src/arm.c:arm_has_swp");
   arc_state_value(s,&(arm_has_swp),sizeof(arm_has_swp));
   arc_state_object(s,"engine/arculator/src/arm.c:arm_has_cp15");
   arc_state_value(s,&(arm_has_cp15),sizeof(arm_has_cp15));
   arc_state_object(s,"engine/arculator/src/arm.c:fpaena");
   arc_state_value(s,&(fpaena),sizeof(fpaena));
   arc_state_object(s,"engine/arculator/src/arm.c:irq");
   arc_state_value(s,&(irq),sizeof(irq));
   arc_state_object(s,"engine/arculator/src/arm.c:flaglookup");
   arc_state_value(s,&(flaglookup),sizeof(flaglookup));
   arc_state_object(s,"engine/arculator/src/arm.c:armirq");
   arc_state_value(s,&(armirq),sizeof(armirq));
   arc_state_object(s,"engine/arculator/src/arm.c:ins");
   arc_state_value(s,&(ins),sizeof(ins));
   arc_state_object(s,"engine/arculator/src/arm.c:osmode");
   arc_state_value(s,&(osmode),sizeof(osmode));
   arc_state_object(s,"engine/arculator/src/arm.c:mode");
   arc_state_value(s,&(mode),sizeof(mode));
   arc_state_object(s,"engine/arculator/src/arm.c:pccache");
   arc_state_value(s,&(pccache),sizeof(pccache));
   if(arc_state_loading(s))arc_state_action(s,arc_state_cpu_tables_rebuild,NULL,NULL);
   arc_state_object(s,"engine/arculator/src/arm.c:total_cycles");
   arc_state_value(s,&(total_cycles),sizeof(total_cycles));
   if(arc_state_loading(s)) {
      ARC_STATE_BOUND(s,mode,0,3);ARC_STATE_BOUND(s,arm3_slot,0,63);ARC_STATE_MATCH(s,arm_cpu_type);ARC_STATE_MATCH(s,arm_has_cp15);ARC_STATE_MATCH(s,arm_has_swp);
   }
}
