/* Public tuples, exhaustive category/installed matrix, ranking and failures.
 * Authored tests, never firmware bytes. GPL-2.0-only. */
#include "rpc_selection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct { unsigned model, os, cpu, family; } expected_pairs[] = {
   {2,4,4,8}, {2,2,4,4}, {1,1,4,4}, {3,4,16,8}, {3,3,16,8}, {4,4,8,8}, {4,2,8,4}
};
static unsigned checks;
static ArcRpcSelection select_set(const ArcRpcCompatibility *d, size_t n, unsigned installed,
   bool failure, unsigned m, unsigned f)
{
   ArcRpcFirmwareInventory i = {.installed=installed,.vfs_failure=failure};
   ++checks;
   return arc_riscpc_select(d,n,&i,(ArcRpcSelectionModel)m,(ArcRpcSelectionOs)f);
}
static void expect(ArcRpcCompatibility d, unsigned installed, unsigned reason, unsigned m, unsigned f)
{
   ArcRpcSelection r=select_set(&d,1,installed,false,0,0);
   assert(r.reason==reason && (unsigned)r.model==m && (unsigned)r.os==f);
}
static void public_tuples(void)
{
   size_t n=0;const ArcRpcFirmwareFingerprint *t=arc_riscpc_firmware_catalogue(&n);
   assert(n==4);
   const size_t sizes[]={2097152,4194304,4194304,4194304};
   const uint32_t crc[]={0x541b1415,0xa9822c2c,0x63fc131a,0x211cf888};
   const char *sha[]={"c31318e712ee7de92e46f9df6c791b614d4c9304","3cf75aa4d4dc8fe57110a124fbd1560d46c549b1",
      "71cbc9f12debaa7aa2aeb91d38682e6c5c4a34a5","c5fe0645e48894fb4b245abeefdc9a65d659af59"};
   for(unsigned j=0;j<4;++j){
      assert((unsigned)t[j].os==j+1 && t[j].size==sizes[j] && t[j].crc32==crc[j] && !strcmp(t[j].sha1,sha[j]));
      ArcDigest d={sizes[j],crc[j],{0}};strcpy(d.sha1,sha[j]);
      assert(arc_riscpc_firmware_recognize(&d,t,n)==t+j);
      ++d.size;assert(!arc_riscpc_firmware_recognize(&d,t,n));--d.size;
      d.crc32^=1;assert(!arc_riscpc_firmware_recognize(&d,t,n));d.crc32^=1;
      d.sha1[0]^=1;assert(!arc_riscpc_firmware_recognize(&d,t,n));
   }
   assert(!arc_riscpc_firmware_recognize(NULL,t,n));
   /* Persistence tags must keep their pre-R3a values. */
   assert(ARC_OS_UNKNOWN==0 && ARC_OS_TEST==1 && ARC_OS_RISCOS_311==2 && ARC_OS_RISCOS_310==3 &&
      ARC_OS_RISCOS_319==4 && ARC_OS_RISCOS_300==5 && ARC_OS_RISCOS_201==6 && ARC_OS_RISCOS_200==7 &&
      ARC_OS_ARTHUR_120==8 && ARC_OS_ARTHUR_030==9);
   assert(ARC_MACHINE_A3000==0 && ARC_MACHINE_A3010==1 && ARC_MACHINE_A3020==2 && ARC_MACHINE_A4000==3 &&
      ARC_MACHINE_COUNT==4 && ARC_CPU_ARM2==0 && ARC_CPU_ARM250==1);
}
static void matrix(void)
{
   for(unsigned cpu=0;cpu<64;++cpu)for(unsigned os=0;os<128;++os)for(unsigned installed=0;installed<16;++installed){
      ArcRpcCompatibility d={(uint8_t)cpu,(uint8_t)os};
      ArcRpcSelection r=select_set(&d,1,installed<<1,false,0,0);
      unsigned c=cpu?cpu:63,o=os?os:127;
      if((c&3)||(o&3)){assert(r.reason==ARC_RPC_SELECTION_LEGACY);continue;}
      bool feasible=(c&28)&&(o&12);unsigned qualified=0,model=0,firmware=0;
      for(unsigned p=0;p<7;++p)if((c&expected_pairs[p].cpu)&&(o&expected_pairs[p].family)){
         qualified|=1u<<expected_pairs[p].os;
         if(!model && ((installed<<1)&(1u<<expected_pairs[p].os))){model=expected_pairs[p].model;firmware=expected_pairs[p].os;}
      }
      unsigned reason=model?ARC_RPC_SELECTION_SELECTED:!feasible?ARC_RPC_SELECTION_UNSUPPORTED:
         !qualified?ARC_RPC_SELECTION_UNQUALIFIED:ARC_RPC_SELECTION_MISSING_FIRMWARE;
      assert((unsigned)r.reason==reason && (unsigned)r.model==model && (unsigned)r.os==firmware && r.needed==qualified);
   }
   for(unsigned m=1;m<=4;++m)for(unsigned os=1;os<=4;++os){
      bool q=false;for(unsigned p=0;p<7;++p)if(expected_pairs[p].model==m&&expected_pairs[p].os==os)q=true;
      assert(arc_riscpc_selection_qualified((ArcRpcSelectionModel)m,(ArcRpcSelectionOs)os)==q);
      ArcRpcCompatibility contrary={32,64};
      ArcRpcSelection r=select_set(&contrary,1,30,false,m,os);
      assert(r.reason==(q?ARC_RPC_SELECTION_SELECTED:ARC_RPC_SELECTION_UNQUALIFIED));
      assert(!q || ((unsigned)r.model==m&&(unsigned)r.os==os));
      r=select_set(&contrary,1,0,true,m,os);
      assert(r.reason==(q?ARC_RPC_SELECTION_UNREADABLE_INVENTORY:ARC_RPC_SELECTION_UNQUALIFIED));
   }
   /* Every CPU and OS category paired with every other across two discs. */
   for(unsigned c=0;c<7;++c)for(unsigned o=0;o<8;++o)
   for(unsigned c2=0;c2<7;++c2)for(unsigned o2=0;o2<8;++o2){
      unsigned cp=c?1u<<(c-1):0,op=o?1u<<(o-1):0;
      unsigned cp2=c2?1u<<(c2-1):0,op2=o2?1u<<(o2-1):0;
      ArcRpcCompatibility ds[]={{(uint8_t)cp,(uint8_t)op},{(uint8_t)cp2,(uint8_t)op2}};
      ArcRpcSelection a=select_set(ds,2,30,false,0,0);
      ArcRpcCompatibility reversed[]={ds[1],ds[0]};
      ArcRpcSelection b=select_set(reversed,2,30,false,0,0);
      assert(a.reason==b.reason&&a.model==b.model&&a.os==b.os&&a.needed==b.needed&&a.incomplete==b.incomplete);
      unsigned cm=(cp?cp:63)&(cp2?cp2:63),om=(op?op:127)&(op2?op2:127);
      bool eligible=(cp&&op&&!(cp&3)&&!(op&3)) || (cp2&&op2&&!(cp2&3)&&!(op2&3));
      if(!eligible)assert(a.reason==ARC_RPC_SELECTION_LEGACY);
      else if(!cm||!om)assert(a.reason==ARC_RPC_SELECTION_CONFLICTING_DISCS);
      else expect((ArcRpcCompatibility){(uint8_t)cm,(uint8_t)om},30,a.reason,a.model,a.os);
   }
}
static void boundaries(void)
{
   expect((ArcRpcCompatibility){20,12},8,ARC_RPC_SELECTION_SELECTED,3,3); /* 3.70-only boundary */
   expect((ArcRpcCompatibility){28,12},30,ARC_RPC_SELECTION_SELECTED,2,4);
   expect((ArcRpcCompatibility){4,4},2,ARC_RPC_SELECTION_SELECTED,1,1); /* fallback model */
   expect((ArcRpcCompatibility){24,12},30,ARC_RPC_SELECTION_SELECTED,3,4); /* SA before A7000 */
   expect((ArcRpcCompatibility){8,4},30,ARC_RPC_SELECTION_SELECTED,4,2);
   expect((ArcRpcCompatibility){4,8},8,ARC_RPC_SELECTION_MISSING_FIRMWARE,0,0); /* 3.70 unqualified on 710/610 */
   expect((ArcRpcCompatibility){32,8},30,ARC_RPC_SELECTION_UNSUPPORTED,0,0);
   expect((ArcRpcCompatibility){4,16},30,ARC_RPC_SELECTION_UNSUPPORTED,0,0);
   expect((ArcRpcCompatibility){0,8},30,ARC_RPC_SELECTION_LEGACY,0,0);
   expect((ArcRpcCompatibility){4,0},30,ARC_RPC_SELECTION_LEGACY,0,0);
   expect((ArcRpcCompatibility){1,8},30,ARC_RPC_SELECTION_LEGACY,0,0); /* ARM3 */
   expect((ArcRpcCompatibility){4,2},30,ARC_RPC_SELECTION_LEGACY,0,0); /* older OS */
   ArcRpcCompatibility ds[]={{4,4},{16,8}};
   assert(select_set(ds,2,0,true,0,0).reason==ARC_RPC_SELECTION_CONFLICTING_DISCS);
   assert(select_set(ds,1,0,true,0,0).reason==ARC_RPC_SELECTION_UNREADABLE_INVENTORY);
   assert(select_set(ds,2,30,false,3,4).reason==ARC_RPC_SELECTION_SELECTED);
   ds[1]=(ArcRpcCompatibility){0,0};
   assert(select_set(ds,2,30,false,0,0).model==ARC_RPC_SELECT_ARM710);
   ds[0]=(ArcRpcCompatibility){32,64};
   assert(select_set(ds,2,0,true,0,0).reason==ARC_RPC_SELECTION_UNSUPPORTED);
   assert(select_set(NULL,0,30,false,1,4).reason==ARC_RPC_SELECTION_UNQUALIFIED);
   assert(select_set(NULL,0,30,false,2,0).os==ARC_RPC_OS_371);
   assert(select_set(NULL,0,30,false,0,1).model==ARC_RPC_SELECT_ARM610);
   assert(select_set(NULL,0,30,false,99,0).reason==ARC_RPC_SELECTION_UNSUPPORTED);
   assert(select_set(NULL,0,30,false,0,99).reason==ARC_RPC_SELECTION_UNSUPPORTED);
   ArcRpcCompatibility split_hints[]={{4,0},{0,8}};
   assert(select_set(split_hints,2,30,false,0,0).reason==ARC_RPC_SELECTION_LEGACY);
   ArcRpcCompatibility legacy_conflict[]={{1,8},{2,8}};
   assert(select_set(legacy_conflict,2,30,false,0,0).reason==ARC_RPC_SELECTION_LEGACY);
   ds[0]=(ArcRpcCompatibility){16,8};
   ArcRpcSelection s=select_set(ds,1,8,true,0,0);
   assert(s.reason==ARC_RPC_SELECTION_SELECTED&&s.incomplete&&s.os==ARC_RPC_OS_370);
   ArcRpcFirmwareInventory i={.bounded=true};
   s=arc_riscpc_select(ds,1,&i,0,0);
   assert(s.reason==ARC_RPC_SELECTION_MISSING_FIRMWARE&&s.incomplete&&s.needed==24);
   char text[256];arc_riscpc_selection_diagnostic(&s,text,sizeof(text));
   assert(strstr(text,"needed=3.70,3.71")&&strstr(text,"inventory=incomplete"));
   ArcRpcCompatibility a=arc_riscpc_compatibility(true,0xffffffff);
   assert(a.cpu==63&&a.os==127);
   a=arc_riscpc_compatibility(false,0xffffffff);assert(!a.cpu&&!a.os);
}
int main(void)
{
   public_tuples();matrix();boundaries();
   printf("R3a selection: %u matrix/boundary checks, public tuples and pinned persistence enums\n",checks);
   return 0;
}
