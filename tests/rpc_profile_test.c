/* Exact authored counter expectations, independent of diagnostic decoder. GPL-2.0-only. */
#define ARC_RPC_OBSERVERS_TEST
#include "rpc_observer_test.c"
#include "rpc_observer.h"
#include "rpc_profile.h"
static uint64_t test_clock(void) { static uint64_t now;return now+=10; }
static ArcRpcProfile counts(void)
{ ArcRpcProfile p;arc_rpc_profile_snapshot(&p);return p; }
static void profile_fixture(uint32_t pc,unsigned stride)
{
    arc_rpc_observer_retire(false);arc_rpc_mem_bind(NULL,0,0);
    cpu_reset(CPUModel_SA110);cp_write(1,0x10);mode32(pc,0);
    memset(rom,0,1024);memset(ram00,0,1024);memset(ram01,0,1024);arc_rpc_cpu_idle=false;
    arc_rpc_profile_configure(stride,test_clock);
}
static void filter(void *p,const ArcRpcObservation *e) { (void)p;(void)e; }
static void shape(void)
{
    for(unsigned path=0;path<3;++path){
        profile_fixture(0x100,0);
        if(path){ArcRpcObserveFilter f={.modes=~0u,.swi=0,.swi_mask=0,.kinds=1,.callback=filter};assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));}
        if(path==2)arc_rpc_observer_trace(true,false);
        rom[64]=0xe3a00001;rom[65]=0xe2800001;rom[66]=0xea000000;
        assert(arc_rpc_arm_exec_count(2)==2);assert(counts().open_block==2);
        assert(arc_rpc_arm_exec_count(1)==1);
        ArcRpcProfile p=counts();assert(p.attempts==3 && p.fetched==3 && p.condition_passed==3);
        assert(p.classes[0]==2 && p.classes[2]==1 && p.candidates==3);
        assert(p.blocks[2]==1 && p.block_instructions==3 && p.block_max==3 && !p.open_block);
        assert(p.exits[ARC_RPC_EXIT_E1]==1 && p.exits[ARC_RPC_EXIT_E5]==2 && p.branches==1);
        assert(!p.clock_reads);
    }
    profile_fixture(0x100,0);rom[64]=0x0a000000;rom[65]=0x01a0f001;
    arc_rpc_arm_exec_count(2);assert(!counts().branches && !counts().pc_writes && !counts().exits[ARC_RPC_EXIT_E1] && counts().open_block==2);
    profile_fixture(0x100,1);rom[64]=0x0f000007;rom[65]=0xe1a0f001;arm.reg[1]=0x10c;
    arc_rpc_arm_exec_count(2);ArcRpcProfile p=counts();
    assert(p.classes[11]==1 && p.classes[0]==1 && p.condition_passed==1 && p.candidates==2);
    assert(!p.exits[ARC_RPC_EXIT_E10] && p.pc_writes==1 && p.blocks[1]==1);
    assert(p.samples[ARC_RPC_COST_FETCH]==2 && p.clock_reads>0 && p.sampled_ns[ARC_RPC_COST_FETCH]>0 && p.sampled_ns[ARC_RPC_COST_PROFILE]>0);
    profile_fixture(0x100,0);for(unsigned i=0;i<65;++i)rom[64+i]=0xe1a00000;
    rom[129]=0xeafffffe;arc_rpc_arm_exec_count(66);p=counts();
    assert(p.blocks[64]==1 && p.block_instructions==66 && p.block_max==66);
}
static void mix(void)
{
    profile_fixture(0x100,1);arm.reg[1]=0x10002000;arm.reg[2]=3;
    const uint32_t program[]={0xe3a00001,0xe0000290,0xe5910000,0xe5810000,
        0xe8910005,0xe8810005,0xe1010092,0xe1d100b0,0xe10f3000,
        0xee103f10,0xee073f15,0xef000007};
    memcpy(rom+64,program,sizeof(program));arc_rpc_arm_exec_count(12);
    ArcRpcProfile p=counts();
    const uint64_t expected[]={1,1,0,1,1,1,1,1,1,1,2,1,0};
    assert(!memcmp(p.classes,expected,sizeof(expected)));
    assert(p.attempts==12 && p.fetched==12 && p.condition_passed==12);
    assert(p.reads==5 && p.writes==4); /* LDR/LDM2/SWP/LDRH and STR/STM2/SWP */
    assert(p.exits[ARC_RPC_EXIT_E3]==7 && p.exits[ARC_RPC_EXIT_E11]==1);
    assert(p.exits[ARC_RPC_EXIT_E10]==1 && !p.exits[ARC_RPC_EXIT_E2]);
    assert(p.samples[ARC_RPC_COST_TIMING]>0 && p.samples[ARC_RPC_COST_DISPATCH]>0);
}
static void churn(void)
{
    profile_fixture(0x10000000,0);ram00[0]=0xe5810000;arm.reg[1]=0x12000040;arm.reg[0]=0x1234;
    arc_rpc_arm_exec_count(1);ArcRpcProfile p=counts();
    assert(ram00[16]==0x1234 && p.writes==1 && p.recent_writes==1 && p.recent_pages==1);
    assert(p.exits[ARC_RPC_EXIT_E9]==1 && p.blocks[0]==1);
    ram00[1]=0xe5c10001;ram00[2]=0xe1c100b2;ram00[3]=0xe8810005;
    arc_rpc_arm_exec_count(3);p=counts();assert(p.recent_writes==6 && p.recent_pages==1);
    assert(p.exits[ARC_RPC_EXIT_E9]==4);
    /* The second aperture half is a different offset, and ROM stores are ignored. */
    ram00[4]=0xe5810000;arm.reg[1]=0x11000040;arc_rpc_arm_exec_count(1);
    ram00[5]=0xe5810000;arm.reg[1]=0x100;arc_rpc_arm_exec_count(1);
    assert(counts().recent_writes==6);
    arc_rpc_profile_reset();ram00[6]=0xe5810000;arm.reg[1]=0x12000040;arc_rpc_arm_exec_count(1);
    p=counts();assert(p.recent_writes==7 && p.recent_pages==2 && p.epoch==1);
    profile_fixture(0x14000000,0);ram01[0]=0xe5810000;ram01[1]=0xe5c10001;
    arm.reg[1]=0x16000040;arm.reg[0]=0x4567;arc_rpc_arm_exec_count(2);p=counts();
    assert(p.recent_writes==2 && p.recent_pages==1 && p.exits[ARC_RPC_EXIT_E9]==2 && ram01[16]==0x6767);
    profile_fixture(0x100,0);rom[64]=0xe1a00000;arc_rpc_arm_exec_count(1);
    arc_rpc_profile_reset();p=counts();assert(p.censored_blocks==1 && p.censored_instructions==1 && !p.block_instructions && !p.open_block);
    /* Exercise VRAM backing alias and the inclusive recent-window endpoint. */
    profile_fixture(0x02000000,0);vram[0]=0xe5810000;arm.reg[1]=0x02200040;
    arc_rpc_arm_exec_count(1);assert(counts().recent_writes==1 && vram[16]==0);
    for(unsigned i=0;i<2;++i){
        arc_rpc_profile_configure(0,test_clock);arc_rpc_profile_begin(0,19,0);
        arc_rpc_profile_fetched(ram00,0xe1a00000,true);arc_rpc_profile_end(4,19,0,false);
        for(unsigned k=0;k<ARC_RPC_PROFILE_RECENT+i;++k){arc_rpc_profile_begin(0,19,0);if(k+1<ARC_RPC_PROFILE_RECENT+i)arc_rpc_profile_end(4,19,0,false);}
        arc_rpc_profile_store(ram00,0);assert(counts().recent_writes==(i?0:1));
    }
}
static void sampling(void)
{
    profile_fixture(0x100,2);
    /* Unsampled outer scopes suppress child sampling too. */
    for(unsigned i=0;i<2;++i){int outer=arc_rpc_profile_enter(ARC_RPC_COST_BUS);
        int inner=arc_rpc_profile_enter(ARC_RPC_COST_DEVICES);
        arc_rpc_profile_leave(inner);arc_rpc_profile_leave(outer);}
    ArcRpcProfile p=counts();assert(p.samples[ARC_RPC_COST_BUS]==1 && p.samples[ARC_RPC_COST_DEVICES]==1);
}
static void exits(void)
{
    profile_fixture(0x100,0);rom[64]=0xe321f010;arc_rpc_arm_exec_count(1);
    assert(counts().exits[ARC_RPC_EXIT_E4]==1);
    profile_fixture(0x100,0);rom[64]=0xee0f0f58;arc_rpc_arm_exec_count(1);
    assert(arc_rpc_cpu_idle && counts().exits[ARC_RPC_EXIT_E4]==1 && counts().batch_idle==1);
    for(unsigned bit=1;bit<=2;++bit){profile_fixture(0x100,0);rom[64]=0xe1a00000;arm.event=bit;
        arc_rpc_arm_exec_count(1);assert(counts().exits[ARC_RPC_EXIT_E6]==1 && counts().blocks[0]==1);}
    profile_fixture(0x04000000,0);arc_rpc_arm_exec_count(1);
    assert(counts().fetched==0 && counts().exits[ARC_RPC_EXIT_E7B]==1);
    profile_fixture(0x100,0);rom[64]=0xe5910000;arm.reg[1]=0x00400000;
    uint32_t *table=ram00+0x4000/4;memset(table,0,16384);table[0]=0xc02;enable(CPUModel_SA110,0);
    arc_rpc_arm_exec_count(1);ArcRpcProfile p=counts();
    assert(p.exits[ARC_RPC_EXIT_E7A]==1 && p.reads==0 && p.blocks[0]==1);
    profile_fixture(0x100,0);rom[64]=0xe591f000;arm.reg[1]=0x00400000;
    table=ram00+0x4000/4;memset(table,0,16384);table[0]=0xc02;enable(CPUModel_SA110,0);
    arc_rpc_arm_exec_count(1);p=counts();assert(p.exits[ARC_RPC_EXIT_E7A]==1 && !p.pc_writes && !p.exits[ARC_RPC_EXIT_E1]);
    profile_fixture(0x100,0);rom[64]=0xe8810005;arm.reg[1]=0x100ffffc;arm.reg[0]=0x1234;arm.reg[2]=0x4567;
    table=ram00+0x4000/4;memset(table,0,16384);table[0]=0xc02;table[0x100]=0x10000c02;enable(CPUModel_SA110,0);
    arc_rpc_arm_exec_count(1);p=counts();assert(p.writes==1 && p.exits[ARC_RPC_EXIT_E7A]==1 && ram00[0xffffc/4]==0x1234 && !ram00[0x100000/4]);
    profile_fixture(0x100,0);rom[64]=0xec000000;arc_rpc_arm_exec_count(1);
    assert(counts().exits[ARC_RPC_EXIT_E10]==1);
    profile_fixture(0x100,0);rom[64]=0xee070f11;arc_rpc_arm_exec_count(1);
    assert(counts().classes[10]==1 && counts().exits[ARC_RPC_EXIT_E4]==1 && !counts().exits[ARC_RPC_EXIT_E11]);
    /* Actual batch stop, no fabricated translator guard miss. */
    profile_fixture(0x100,0);rom[64]=rom[65]=0xe1a00000;
    ArcRpcScheduler clock;assert(arc_rpc_scheduler_init(&clock,100));
    ArcRpcCpuBatch batch={&clock,NULL,10};
    /* A disabled timing component needs a uint64_t bus boundary. */
    uint64_t bus=0;batch.bus_until=&bus;
    arc_rpc_arm_exec_batch(2,&batch);p=counts();assert(p.attempts==1 && p.batch_boundary==1 && p.open_block==1);
    arc_rpc_profile_fault();p=counts();assert(p.faults==1 && p.exits[ARC_RPC_EXIT_E8]==1 && !p.open_block);
}
int main(void)
{
    arm_init();protected=1;
    if(setjmp(boundary)){fputs("unexpected profile guest fault\n",stderr);return 1;}
    config.mem_size=64;config.vram_size=2;machine.cpu_model=CPUModel_SA110;machine.model=Model_RPCSA110;
    mem_init();
    /* Retained translator canonical tests run with live profiling and sampling. */
    arc_rpc_profile_configure(1,test_clock);
    for(unsigned m=0;m<4;++m){interpreter_equivalence((CPUModel)m);timing_events((CPUModel)m);identity_equality((CPUModel)m);}
    shape();mix();churn();sampling();exits();arc_rpc_memory_close();
    puts("profile: exact authored classes, lengths, exits, aliases, permissions and recent-window boundaries");return 0;
}
