/* Authored instruction and descriptor fixtures. GPL-2.0-only. */
#define main cpu_fixture_main
#include "rpc_cpu_test.c"
#undef main
#include "rpc_observer.h"

static ArcRpcObservation seen[128];
static unsigned seen_count,action;
static uint32_t expected_pc,expected_sp,expected_mode;
static uint64_t watch;
static void observe(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;assert(seen_count<128);seen[seen_count++]=*e;
    if(action==11 && e->kind!=ARC_RPC_OBS_SWI)
        assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,0x104,16,0x10008000,100));
    if(e->kind!=ARC_RPC_OBS_SWI)return;
    if(action==12){watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,100);assert(!watch);}
    if(action==13){watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,2);assert(watch);}
    if(action==1)watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,100);
    if(action==5){
        uint64_t first=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc+4,expected_mode,expected_sp,100);
        watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,100);assert(first&&watch>first);
        assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,32,expected_sp,100));
        assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc+1,expected_mode,expected_sp,100));
        assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,0));
        assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,ARC_RPC_WATCH_ATTEMPT_LIMIT+1u));
    }
    if(action==6){watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,expected_pc,expected_mode,expected_sp,1);assert(watch);}
    if(action==10)assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,NULL));
    if(action==9){
        arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);uint32_t registers[13]={0};ARMState before=arm;
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,
            arc_rpc_observer_generation(ARC_RPC_OBS_DISC),1,registers,0));assert(!memcmp(&before,&arm,sizeof(arm)));
    }
    if(action==8){
        watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,e->pc+4,e->mode,e->registers[13],100);assert((watch!=0)==!e->privileged);
        uint32_t registers[13]={0};assert(arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,
            arc_rpc_observer_generation(ARC_RPC_OBS_DISC),1,registers,0));
    }
    if(action==2){
        uint32_t result[13]={123,456};uint64_t g=arc_rpc_observer_generation(ARC_RPC_OBS_DISC);
        ARMState before=arm;
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,0x8000,result,0));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,4,result,0));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,1,result,1));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,1,result,0x80000000));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,1,result,0x40000000));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order-1,e->epoch,g,1,result,0));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch-1,g,1,result,0));
        assert(!memcmp(&before,&arm,sizeof(arm)));
        assert(arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,3,result,0x30000000));
        assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,e->order,e->epoch,g,1,result,0));
    }
    if(action==3){uint32_t value;assert(!arc_rpc_observer_take_answer(ARC_RPC_OBS_DISC,&value));
        assert(arc_rpc_observer_answer(ARC_RPC_OBS_DISC,99));
        assert(!arc_rpc_observer_take_answer(ARC_RPC_OBS_DISC,&value));action=4;
    }else if(action==4){uint32_t value;assert(arc_rpc_observer_take_answer(ARC_RPC_OBS_DISC,&value)&&value==99);action=0;}
}
static void arm_filter(uint32_t modes,uint32_t swi,uint32_t mask)
{
    ArcRpcObserveFilter f={.modes=modes,.swi=swi,.swi_mask=mask,.kinds=7,
        .registers=0x30,.results=3,.callback=observe};
    assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));
}
static void fixture(CPUModel model,uint32_t mode,uint32_t pc)
{
    arc_rpc_mem_bind(NULL,0,0);arc_rpc_observer_retire(false);
    seen_count=action=0;watch=0;cpu_reset(model);
    if(mode&16)cp_write(1,cp_read(1)|0x10);
    updatemode(mode);arm.reg[16]=mode;arm.reg[15]=pc+8;
    if(!(mode&16))arm.reg[15]|=mode;
    arm.reg[13]=0x10008000;arm.reg[14]=0x1234;arm.event=0;
    memset(rom,0,256);memset(ram00,0,256);
    /* Normal SWI handler returns via MOVS pc,lr. */
    rom[2]=0xe1b0f00e;
    rom[3]=0xe1b0f00e;
}
static void events(CPUModel model)
{
    for(unsigned m=0;m<4;++m){uint32_t mode=(m&1?0x10:0)|(m&2?3:0);
        fixture(model,mode,(mode&16)?0x10000000:0x100);arm_filter(~0u,0,0);
        for(unsigned r=0;r<13;++r)arm.reg[r]=0x100+r;
        arm.reg[(mode&16)?16:15]|=0x80000000;
        uint32_t *code=(mode&16)?ram00:rom+0x100/4;
        code[0]=0x0f000007; /* EQ, with Z clear: no SWI */
        arc_rpc_arm_exec_count(1);assert(!seen_count);
        code[1]=0xef020007;arc_rpc_arm_exec_count(1);
        assert(seen[0].kind==ARC_RPC_OBS_SWI && seen[0].swi==0x20007 && seen[0].pc==((mode&16)?0x10000004:0x104));
        /* In 26-bit mode the VA is masked to the ROM aperture. */
        if(!(mode&16))continue;
        assert(seen[0].mode==mode && seen[0].width==32 && seen[0].privileged==((mode&15)!=0));
        assert(seen[0].backing.kind==ARC_RPC_BACKING_RAM0 && seen[0].backing.offset==4);
        assert(seen[0].nzcv==0x80000000 && seen[0].registers[4]==0x104 && seen[0].registers[5]==0x105);
        assert(seen[0].registers[6]==0 && seen[0].register_mask==0x603f);
    }
    for(unsigned width=0;width<2;++width){uint32_t mode=width?0x10:0;
        fixture(model,mode,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
        rom[0x104/4]=0x01a00000; /* EQ fails after return, continuation still fires. */
        expected_pc=0x104;expected_mode=mode;expected_sp=arm.reg[13];action=1;
        arc_rpc_arm_exec_count(1);assert(watch && seen_count>=1);assert(seen[0].swi==7 && seen[0].pc==0x100);
        assert(seen[0].width==(width?32:26) && seen[0].backing.kind==ARC_RPC_BACKING_ROM);
        action=0;arc_rpc_arm_exec_count(1);assert(arm.mode==mode);
        arc_rpc_arm_exec_count(1);
        assert(seen[seen_count-1].kind==ARC_RPC_OBS_CONTINUATION && seen[seen_count-1].matched);
        assert(seen[seen_count-1].pc==0x104 && seen[seen_count-1].watch==watch);
        unsigned count=seen_count;arm.reg[15]=(width?0x10c:0x10c|mode);arc_rpc_arm_exec_count(1);assert(seen_count==count);
    }
    for(unsigned source=0;source<4;++source)for(unsigned target=0;target<4;++target){
        const uint32_t modes[]={0,16,3,19};
        if(source==target && source<2)continue;
        fixture(model,modes[source],0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
        expected_pc=0x104;expected_mode=modes[target];expected_sp=arm.reg[13];action=12;
        arc_rpc_arm_exec_count(1);action=0;arc_rpc_arm_exec_count(2);
        for(unsigned i=0;i<seen_count;++i)assert(seen[i].kind!=ARC_RPC_OBS_CONTINUATION);
    }
    for(unsigned policy=0;policy<3;++policy){
        fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
        expected_pc=0x104;expected_mode=0x10;expected_sp=arm.reg[13];action=policy==2?6:5;
        arc_rpc_arm_exec_count(1);action=0;if(policy==1)arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);
        unsigned first=seen_count;arc_rpc_arm_exec_count(2);
        unsigned continuations=0;for(unsigned i=first;i<seen_count;++i)if(seen[i].kind==ARC_RPC_OBS_CONTINUATION){++continuations;assert(seen[i].watch==watch&&seen[i].matched);}
        assert(continuations==(policy==0));
    }
    /* Exact filter: full mode, condition and X are distinct. */
    fixture(model,0x10,0x100);arm_filter(1u<<0x10,7,0xffffff);
    rom[0x100/4]=0xef020007;arc_rpc_arm_exec_count(1);assert(!seen_count);
    fixture(model,0x13,0x100);arm_filter(1u<<0x10,7,0xffffff);
    rom[0x100/4]=0xef000007;arc_rpc_arm_exec_count(1);assert(!seen_count);
    fixture(model,0x10,0x100);arm_filter(1u<<0x10,7,0xffffff);
    rom[0x100/4]=0xef000007;arc_rpc_arm_exec_count(1);assert(seen_count==1);
    /* Disqualification ends the watch; later address/SP rematches never fire. */
    for(unsigned wrong=0;wrong<2;++wrong){
        fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000005;
        expected_pc=wrong?0x104:0x108;expected_mode=0x10;expected_sp=arm.reg[13]+(wrong?4:0);action=1;
        arc_rpc_arm_exec_count(1);action=0;arc_rpc_arm_exec_count(2);
        assert(seen[seen_count-1].kind==ARC_RPC_OBS_CONTINUATION && !seen[seen_count-1].matched);
        unsigned count=seen_count;arm.reg[15]=expected_pc+8;arm.reg[13]=expected_sp;arc_rpc_arm_exec_count(1);assert(seen_count==count);
    }
    /* A failed return fetch produces only its completed Abort transition. */
    fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000005;
    expected_pc=0x03000000;expected_mode=0x10;expected_sp=arm.reg[13];action=1;
    arc_rpc_arm_exec_count(1);action=0;arm.reg[14]=expected_pc;arc_rpc_arm_exec_count(1);
    unsigned count=seen_count;arc_rpc_arm_exec_count(1);
    for(unsigned i=count;i<seen_count;++i)assert(seen[i].kind!=ARC_RPC_OBS_CONTINUATION);
    assert(arm.mode==0x17);
    /* Nested SWI and FIQ at the SWI's ordinary event check report separately. */
    fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;rom[2]=0xef020006;
    arm.event=3;arc_rpc_arm_exec_count(1);
    assert(seen_count==3 && seen[1].old_mode==0x10 && seen[1].mode==0x13 && seen[2].old_mode==0x13 && seen[2].mode==0x11);
    assert(seen[1].pc==0x100 && seen[2].pc==0x100 && seen[1].backing.offset==0x100);
    fixture(model,0x13,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
    arc_rpc_arm_exec_count(1);rom[2]=0xef020006;arc_rpc_arm_exec_count(1);
    assert(seen[0].swi==7 && seen[1].swi==0x20006 && seen[1].pc==8);
    fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;action=10;
    arc_rpc_arm_exec_count(1);assert(seen_count==1 && arc_rpc_observer_test_events()==seen[0].order);
    assert(arc_rpc_observer_test_origin().mode==16); /* Disarm suppresses context updates too. */
    /* CP15 program-width clear: report only after full updatemode completion. */
    fixture(model,0x13,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xee010f10;arm.reg[0]=0;
    arc_rpc_arm_exec_count(1);assert(seen_count==1 && seen[0].old_mode==0x13 && seen[0].mode==3 && seen[0].width==26);
}
static void completion(CPUModel model)
{
    for(unsigned width=0;width<2;++width)for(unsigned interrupts=0;interrupts<4;++interrupts)
    for(unsigned masks=0;masks<4;++masks){
        uint32_t mode=width?0x13:3;fixture(model,mode,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
        uint32_t flags=0x80000000|(masks<<6)|mode;
        arm.reg[16]=flags;if(!width)arm.reg[15]=0x108|mode|0x80000000|(masks<<26);
        ARMState before=arm;arm.event=interrupts;action=2;arc_rpc_arm_exec_count(1);
        assert(arm.reg[0]==123 && arm.reg[1]==456);
        bool fiq=(interrupts&2)&&!(masks&1),irq=(interrupts&1)&&!(masks&2)&&!fiq;
        if(!fiq && !irq){
            assert(arm.mode==before.mode && arm.reg[15]==(width?before.reg[15]+4:((before.reg[15]&~0x30000000u)|0x30000000u)+4));
            assert(!memcmp(arm.spsr,before.spsr,sizeof(arm.spsr)));
            assert(!memcmp(arm.super_reg,before.super_reg,sizeof(arm.super_reg)));
            assert(!memcmp(arm.fiq_reg,before.fiq_reg,sizeof(arm.fiq_reg)));
            assert((arm.reg[width?16:15]&0xf0000000u)==0xb0000000u);
        }else assert((arm.mode&15)==(fiq?1u:2u));
        uint32_t result[13]={0};assert(!arc_rpc_observer_complete(ARC_RPC_OBS_DISC,seen[0].order,seen[0].epoch,
            arc_rpc_observer_generation(ARC_RPC_OBS_DISC),1,result,0));
    }
    fixture(model,0x10,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
    action=9;arc_rpc_arm_exec_count(1);assert(arm.mode==0x13); /* stale callback did not complete */
    for(unsigned width=0;width<2;++width){
        fixture(model,width?0x13:3,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;
        action=8;arc_rpc_arm_exec_count(1);action=0;arc_rpc_arm_exec_count(1);
        assert(!watch && seen_count==1); /* SVC completion cannot arm a watch. */
    }
    fixture(model,3,0x3fffffc);arm_filter(~0u,0,0);
    /* MMU maps final 26-bit PC to backed RAM without altering raw PC arithmetic. */
    uint32_t *table=ram00+(0x4000/4);table[0x3f]=0x10000c02;cp_write(2,0x10004000);cp_write(3,3);cp_write(1,1);
    ram00[0xffffc/4]=0xef000007;arm.reg[15]=0x04000007;action=2;arc_rpc_arm_exec_count(1);
    assert(arm.reg[15]==0x3400000b && arm.mode==3);
    fixture(model,0x13,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000007;action=3;
    arc_rpc_arm_exec_count(1);arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);assert(action==0);
    /* Inactive execution performs no origin capture at all. */
    arc_rpc_observer_retire(false);uint64_t attempts=arc_rpc_observer_test_attempts();
    assert(!arc_rpc_observer_test_origin().epoch);arc_rpc_arm_exec_count(1);
    assert(!arc_rpc_observer_test_origin().epoch);
    assert(arc_rpc_observer_test_attempts()==attempts);
}
/* Long batches without watches must feed later instruction deadlines and
 * switch to checked fetches immediately when a SWI admits a watch. */
static void interpreter_equivalence(CPUModel model)
{
    uint32_t words[]={0xe3a01201,0xe3a02001,0xe4812004,0xe2822001,0xe3520014,
        0x1afffffb,0xef000007,0xe1a00002,0xeafffffe};
    uint32_t saved[9];memcpy(saved,rom+0x100/4,sizeof(saved));
    for(unsigned width=0;width<2;++width){
        ARMState baseline;uint32_t data[32];
        for(unsigned path=0;path<3;++path){
            fixture(model,width?16:0,0x100);memcpy(rom+0x100/4,words,sizeof(words));
            if(path){ArcRpcObserveFilter f={.modes=~0u,.kinds=path==1?ARC_RPC_OBS_SWI:ARC_RPC_OBS_SWI|ARC_RPC_OBS_MODE};
                assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));}
            arc_rpc_arm_exec_count(200);
            if(!path){baseline=arm;memcpy(data,ram00,sizeof(data));}
            else {assert(!memcmp(&baseline,&arm,sizeof(arm)));assert(!memcmp(data,ram00,sizeof(data)));}
        }
    }
    memcpy(rom+0x100/4,saved,sizeof(saved));
}
static void cheap_events(CPUModel model)
{
    uint32_t saved_words[1024];memcpy(saved_words,rom+0x100/4,sizeof(saved_words));
    for(unsigned expiry=0;expiry<2;++expiry){
        fixture(model,16,0x100);
        ArcRpcObserveFilter f={.modes=~0u,.kinds=ARC_RPC_OBS_SWI|ARC_RPC_OBS_CONTINUATION|ARC_RPC_OBS_WATCH_LOST,.callback=observe};
        assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));assert(!arc_rpc_observer_context);
        for(unsigned i=0;i<1000;++i)rom[0x100/4+i]=0xe1a00000;
        for(unsigned i=0;i<5;++i)assert(arc_rpc_arm_exec_count(200)==200);
        assert(arc_rpc_observer_test_attempts()>=1000 && !seen_count && !arc_rpc_observer_test_origin().epoch);
        uint64_t base=arc_rpc_observer_test_attempts();uint32_t pc=PC;
        rom[pc/4]=0xef000007;rom[pc/4+1]=0x01a00000;
        expected_pc=pc+4;expected_mode=16;expected_sp=arm.reg[13];action=expiry?6:1;
        assert(arc_rpc_arm_exec_count(8)==1); /* admission ends the cheap batch */
        assert(seen_count==1 && seen[0].pc==pc && seen[0].instruction==base+1 && watch);
        action=0;arc_rpc_arm_exec_count(1);arc_rpc_arm_exec_count(1);
        assert(!arc_rpc_observer_watch_live(ARC_RPC_OBS_DISC,watch) && !arc_rpc_observer_context);
        assert(seen_count==2 && seen[1].instruction==base+3);
        assert(seen[1].kind==(expiry?ARC_RPC_OBS_WATCH_LOST:ARC_RPC_OBS_CONTINUATION));
        if(!expiry)assert(seen[1].matched); /* condition-failed instruction still matches */
    }
    ArcRpcObservation saved[2];
    for(unsigned tracing=0;tracing<2;++tracing){
        fixture(model,16,0x100);
        ArcRpcObserveFilter f={.modes=~0u,.kinds=ARC_RPC_OBS_SWI,.callback=observe};
        assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));arc_rpc_observer_trace(tracing,false);
        rom[0x100/4]=0xef000007;rom[0x104/4]=0xef000007;
        uint64_t base_order=arc_rpc_observer_test_events(),base_attempt=arc_rpc_observer_test_attempts();
        arc_rpc_arm_exec_count(4);assert(seen_count==2);
        for(unsigned i=0;i<2;++i){seen[i].order-=base_order;seen[i].instruction-=base_attempt;
            if(!tracing)saved[i]=seen[i];else assert(saved[i].order==seen[i].order && saved[i].instruction==seen[i].instruction);}
    }
    memcpy(rom+0x100/4,saved_words,sizeof(saved_words));
}
static void inspector(CPUModel model)
{
    fixture(model,0x13,0x100);uint32_t value=0;uint8_t byte;
    rom[0x100/4]=0x11223344;assert(arc_rpc_inspect_word(0x100,false,&value)&&value==0x11223344);
    ram00[0]=0x12345678;assert(arc_rpc_inspect_word(0x12000000,true,&value)&&value==0x12345678);
    assert(arc_rpc_inspect_byte(0x10000001,false,&byte)&&byte==0x56);
    assert(!arc_rpc_inspect_word(0x10000001,true,&value));
    assert(!arc_rpc_inspect_word(0x03000000,true,&value));assert(!arc_rpc_inspect_byte(0x04000000,true,&byte));
    uint32_t *table=ram00+(0x4000/4),*pages=ram00+(0x8000/4);
    for(unsigned ap=0;ap<4;++ap)for(unsigned sr=0;sr<4;++sr)
    for(unsigned domain=0;domain<4;++domain)for(unsigned privilege=0;privilege<2;++privilege){
        cpu_reset(model);table[4]=0x10000002|(ap<<10);enable(model,sr<<8);cp_write(3,domain);
        unsigned allowed=domain==3 || (domain==1 && (ap>=2 || (ap==1 && privilege) || (ap==0 && (sr==2 || (sr==1 && privilege)))));
        value=0xfeed;bool ok=arc_rpc_inspect_word(0x00400000,privilege!=0,&value);
        assert(ok==allowed);if(ok)assert(value==0x12345678);else assert(value==0xfeed);
    }
    for(unsigned page=0;page<2;++page){
        cpu_reset(model);table[4]=0x10008001;memset(pages,0,1024);enable(model,0);
        for(unsigned sub=0;sub<4;++sub){uint32_t desc=0x14000000|(page?2:1)|(3u<<(4+sub*2));
            if(page)pages[0]=desc;else for(unsigned k=0;k<16;++k)pages[k]=desc;
            uint32_t offset=sub*(page?1024:16384);ram01[offset/4]=0xabc00000+sub;
            for(unsigned want=0;want<4;++want){value=0xfeed;
                bool ok=arc_rpc_inspect_word(0x00400000+want*(page?1024:16384),false,&value);
                assert(ok==(sub==want));if(ok)assert(value==0xabc00000+want);}
        }
    }
    /* Large-page boundary, distinct physical banks and all four AP fields. */
    cpu_reset(model);table[4]=0x10008001;enable(model,0);
    for(unsigned i=0;i<16;++i)pages[i]=0x10000ff1;
    for(unsigned i=16;i<32;++i)pages[i]=0x14000ff1;
    ram00[65532/4]=0x474c2211;ram01[0]=0x11220050;
    char large[8];ArcRpcInspectResult cross=arc_rpc_inspect_string(0x0040fffe,false,large,sizeof(large));
    assert(cross.status==ARC_RPC_INSPECT_OK && !strcmp(large,"LGP"));
    assert(arc_rpc_inspect_word(0x0040fffc,false,&value)&&value==0x474c2211);
    assert(arc_rpc_inspect_word(0x00410000,false,&value)&&value==0x11220050);
    cross=arc_rpc_inspect_bytes(0x0040fffe,false,large,4);assert(cross.status==ARC_RPC_INSPECT_OK&&cross.copied==4);
    for(unsigned i=16;i<32;++i)pages[i]=0x14000001;
    cross=arc_rpc_inspect_string(0x0040fffe,false,large,sizeof(large));assert(cross.status==ARC_RPC_INSPECT_REFUSED&&!strcmp(large,"LG"));
    cpu_reset(model);table[4]=0x10000c02;table[5]=0x14000c02;enable(model,0);
    assert(mem_read32(0x00400000)==0x12345678);table[4]=0x14000c02;ram01[0]=0x87654321;
    assert(arc_rpc_inspect_word(0x00400000,true,&value)&&value==0x12345678); /* stale hit */
    uint32_t va=0x00400000,alias=0x10500000;assert(tlb_slot(va)==tlb_slot(alias));
    table[0x105]=0x14000c02;assert(arc_rpc_inspect_word(alias,true,&value)&&value==0x87654321);
    assert(arc_rpc_inspect_word(va,true,&value)&&value==0x12345678); /* probe must not evict */
    flush(model);assert(arc_rpc_inspect_word(va,true,&value)&&value==0x87654321);
    /* Small-page crossings and prefix refusal, strings and bounds. */
    cpu_reset(model);table[4]=0x10008001;pages[0]=0x10000ff2;pages[1]=0x14000ff2;enable(model,0);
    ((uint8_t *)ram00)[4094]='A';((uint8_t *)ram00)[4095]='B';((uint8_t *)ram01)[0]='C';((uint8_t *)ram01)[1]=13;
    char text[8];((uint8_t *)ram00)[2048]=0;
    assert(arc_rpc_inspect_string(0x00400800,false,text,1).status==ARC_RPC_INSPECT_OK && !text[0]);
    assert(arc_rpc_inspect_string(0x00400800,false,text,0).status==ARC_RPC_INSPECT_REFUSED);
    assert(arc_rpc_inspect_bytes(0,false,text,0).status==ARC_RPC_INSPECT_OK);
    ArcRpcInspectResult r=arc_rpc_inspect_string(0x00400ffe,false,text,sizeof(text));
    assert(r.status==ARC_RPC_INSPECT_OK && r.copied==3 && !strcmp(text,"ABC"));
    r=arc_rpc_inspect_string(0x00400ffe,false,text,3);assert(r.status==ARC_RPC_INSPECT_TRUNCATED && !strcmp(text,"AB"));
    pages[1]=0;memset(text,0xa5,sizeof(text));r=arc_rpc_inspect_bytes(0x00400ffe,false,text,4);
    assert(r.status==ARC_RPC_INSPECT_REFUSED && r.copied==2 && text[0]=='A' && text[1]=='B' && (uint8_t)text[2]==0xa5);
    r=arc_rpc_inspect_string(0x00400ffe,false,text,sizeof(text));assert(r.status==ARC_RPC_INSPECT_REFUSED&&!strcmp(text,"AB"));
    for(unsigned type=0;type<=3;type+=3){
        pages[1]=0x14000ff0|type;value=0xfeed;
        assert(!arc_rpc_inspect_word(0x00401000,false,&value) && value==0xfeed);
    }
    pages[0]=0x10000ff2;((uint8_t *)ram00)[1023]='Q';((uint8_t *)ram00)[1024]='S';((uint8_t *)ram00)[1025]=13;
    r=arc_rpc_inspect_string(0x004003ff,false,text,sizeof(text));assert(r.status==ARC_RPC_INSPECT_OK&&!strcmp(text,"QS"));
    pages[0]=0x10000f32; /* subpage 0/2/3 allowed; 1 denied */
    ((uint8_t *)ram00)[1023]='Q';r=arc_rpc_inspect_bytes(0x004003ff,false,text,2);assert(r.copied==1 && r.status==ARC_RPC_INSPECT_REFUSED);
    assert(arc_rpc_inspect_bytes(UINT32_MAX,true,text,2).status==ARC_RPC_INSPECT_REFUSED);
    char oversized[257];assert(arc_rpc_inspect_string(va,true,oversized,sizeof(oversized)).status==ARC_RPC_INSPECT_REFUSED);assert(arc_rpc_inspect_bytes(va,true,oversized,257).status==ARC_RPC_INSPECT_REFUSED);
    assert(!arc_rpc_inspect_word(UINT32_MAX,true,&value));
    table[4]=0x03000c02;assert(!arc_rpc_inspect_word(va,true,&value));
    table[4]=0x03000001;assert(!arc_rpc_inspect_word(va,true,&value));
    cp_write(2,0x03000000);assert(!arc_rpc_inspect_word(va,true,&value));
    cp_write(2,0x04000000);assert(!arc_rpc_inspect_word(va,true,&value));
    uint32_t *allocated_vram=vram;vram=NULL;cp_write(2,0x02000000);
    assert(!arc_rpc_inspect_word(va,true,&value));cp_write(2,0x10004000);table[4]=0x02000c02;
    assert(!arc_rpc_inspect_word(va,true,&value));vram=allocated_vram;
    cp_write(2,0x10004000);table[4]=0x14000c02;
    /* Bounded raw snapshots around successful and refused walks. */
    /* Overflow refuses the range before copying, even when both aliased
     * endpoint pages would be backed if a 32-bit addition wrapped. */
    table[0xfff]=0x14000c02;table[0]=0x14000c02;flush(model);
    char wrap[2]={'X','Y'};assert(arc_rpc_inspect_bytes(UINT32_MAX,true,wrap,2).status==ARC_RPC_INSPECT_REFUSED);
    assert(wrap[0]=='X'&&wrap[1]=='Y');
    /* ROM descriptors are permitted, absent VRAM descriptors are refused. */
    cp_write(2,0);rom[4]=0x14000c02;
    assert(arc_rpc_inspect_word(va,true,&value)&&value==0x87650d43);
    cp_write(2,0x10004000);table[4]=0x14000c02;
    ArcRpcScheduler inspection_clock={0};iomd.clkctrl=7;
    arc_rpc_mem_bind(&inspection_clock,model,20);arc_rpc_mem_control(cp_read(1));
    inspection_clock.now_ns=arc_rpc_mem_fast.cpu_due_ns;arc_rpc_mem_run(inspection_clock.now_ns);
    assert(arc_rpc_mem_ready(false));arc_rpc_mem_instruction_begin();assert(getpccache(va));
    assert(arc_rpc_mem_fast.pending); /* do not resolve the collected fetch */
    uint8_t cp_before[16384],cp_after[16384],timing_before[131072],timing_after[131072];
    size_t cn=arc_rpc_inspector_snapshot(cp_before,sizeof(cp_before));assert(cn<=sizeof(cp_before));
    size_t tn=arc_rpc_mem_observation_snapshot(timing_before,sizeof(timing_before));assert(tn<=sizeof(timing_before));
    ArcRpcScheduler scheduler=inspection_clock;uint8_t target0[64],target1[64];
    memcpy(target0,ram00,sizeof(target0));memcpy(target1,ram01,sizeof(target1));
    ARMState regs=arm;struct iomd device=iomd;uint32_t descriptors[4096];memcpy(descriptors,table,sizeof(descriptors));
    assert(arc_rpc_inspect_word(va,true,&value)&&value==0x87650d43); /* ABC prefix edits */
    assert(!arc_rpc_inspect_word(0x00600000,false,&value));
    assert(arc_rpc_inspector_snapshot(cp_after,sizeof(cp_after))==cn&&!memcmp(cp_before,cp_after,cn));
    assert(arc_rpc_mem_observation_snapshot(timing_after,sizeof(timing_after))==tn&&!memcmp(timing_before,timing_after,tn));
    assert(!memcmp(&regs,&arm,sizeof(arm))&&!memcmp(&device,&iomd,sizeof(iomd))&&!memcmp(descriptors,table,sizeof(descriptors)));
    assert(!memcmp(&scheduler,&inspection_clock,sizeof(scheduler)) &&
        !memcmp(target0,ram00,sizeof(target0)) && !memcmp(target1,ram01,sizeof(target1)));
    arc_rpc_mem_bind(NULL,0,0);
}
static void timed_step(ArcRpcScheduler *clock)
{
    unsigned steps=0;
    arc_rpc_mem_run(clock->now_ns);
    while(!arc_rpc_mem_ready(false)){
        assert(++steps<1000);uint64_t next=arc_rpc_mem_next(UINT64_MAX,false);assert(next>clock->now_ns);
        clock->now_ns=next;arc_rpc_mem_run(next);
    }
    arc_rpc_arm_exec_count(1);
}
static uint64_t ceiling(uint64_t ticks,unsigned denominator)
{ return ticks/denominator+(ticks%denominator!=0); }
static void timing_events(CPUModel model)
{
    fixture(model,0x10,0x100);arm_filter(~0u,0,0);
    rom[0x100/4]=0xef000007;rom[0x104/4]=0x01a00000;
    ArcRpcScheduler clock={0};unsigned rates[]={20,35,100,35},rate=rates[model];
    iomd.clkctrl=7;iomd.romcr0=iomd.romcr1=0x20;
    arc_rpc_mem_bind(&clock,model,rate);arc_rpc_mem_control(cp_read(1));
    unsigned d=28*rate;uint64_t b=model==2?1750u*rate:28000u,fetch=6125u*rate;
    expected_pc=0x104;expected_mode=0x10;expected_sp=arm.reg[13];action=1;
    uint64_t epoch=arc_rpc_observer_epoch();timed_step(&clock);assert(arc_rpc_mem_fast.pending);action=0;
    timed_step(&clock);timed_step(&clock);
    assert(seen_count==4 && seen[0].kind==ARC_RPC_OBS_SWI && seen[1].kind==ARC_RPC_OBS_MODE &&
        seen[2].kind==ARC_RPC_OBS_MODE && seen[3].kind==ARC_RPC_OBS_CONTINUATION);
    assert(seen[0].ns==ceiling(b,d) && seen[1].ns==seen[0].ns &&
        seen[2].ns==ceiling(2*b+fetch,d) && seen[3].ns==ceiling(3*b+2*fetch,d));
    for(unsigned i=0;i<4;++i){assert(seen[i].epoch==epoch);if(i)assert(seen[i].order>seen[i-1].order);}
    /* The no-mode/no-trace entry captures the same pre-fetch SWI clock and
     * original word backing even when a cold fetch starts memory service. */
    fixture(model,16,0x100);
    ArcRpcObserveFilter f={.modes=~0u,.kinds=ARC_RPC_OBS_SWI|ARC_RPC_OBS_CONTINUATION,.callback=observe};
    assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));assert(!arc_rpc_observer_context);
    rom[0x100/4]=0xef000007;rom[0x104/4]=0x01a00000;clock=(ArcRpcScheduler){0};
    iomd.clkctrl=7;iomd.romcr0=iomd.romcr1=0x20;arc_rpc_mem_bind(&clock,model,rate);arc_rpc_mem_control(cp_read(1));
    expected_pc=0x104;expected_mode=16;expected_sp=arm.reg[13];action=1;
    timed_step(&clock);action=0;timed_step(&clock);timed_step(&clock);
    assert(seen_count==2 && seen[0].ns==ceiling(b,d) && seen[1].ns==ceiling(3*b+2*fetch,d));
    assert(seen[0].pc==0x100 && seen[0].backing.kind==ARC_RPC_BACKING_ROM && seen[0].backing.offset==0x100);
    arc_rpc_mem_bind(NULL,0,0);
}
static void cheap_fault(CPUModel model)
{
    uint32_t saved[6];memcpy(saved,rom+0x100/4,sizeof(saved));
    fixture(model,19,0x100);
    ArcRpcObserveFilter f={.modes=~0u,.kinds=ARC_RPC_OBS_SWI,.callback=observe};
    assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));assert(!arc_rpc_observer_context);
    for(unsigned i=0;i<5;++i)rom[0x100/4+i]=0xe1a00000;
    rom[0x100/4+5]=0xe321f014; /* Authored invalid mode: existing fatal boundary. */
    ArcRpcScheduler clock={0};arc_rpc_mem_bind(&clock,model,100);
    uint64_t before=arc_rpc_observer_test_attempts(),epoch=arc_rpc_observer_epoch();
    if(!setjmp(boundary)){arc_rpc_arm_exec_count(8);assert(0);}
    /* The component boundary leaves retirement to its caller; the contained
     * session's fatal() retires before its jump, with the same live counter. */
    arc_rpc_observer_retire(false);
    assert(arc_rpc_observer_test_attempts()==before+6 && arc_rpc_observer_epoch()==epoch+1);
    assert(!arc_rpc_observer_active && !seen_count);
    arc_rpc_mem_bind(NULL,0,0);memcpy(rom+0x100/4,saved,sizeof(saved));
}
static uint64_t media_generation=5;
static unsigned resolutions,mutations,revalidation_action;
static bool valid(void *opaque,const ArcRpcHostWork *w) { (void)opaque;return w->media_generation==media_generation; }
static bool resolve_work(void *opaque,const ArcRpcHostWork *w)
{
    (void)opaque;(void)w;assert(arc_rpc_observer_in_host());arc_rpc_observer_drain();++resolutions;
    if(revalidation_action==1)++media_generation;
    if(revalidation_action==2)arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);
    if(revalidation_action==3)arc_rpc_observer_retire(false);
    return true;
}
static void mutate(void *opaque,const ArcRpcHostWork *w) { (void)opaque;assert(w->bytes[0]==42);++mutations; }
static ArcRpcHostWork work(ArcRpcObserver owner)
{
    ArcRpcHostWork w={.observer=owner,.generation=arc_rpc_observer_generation(owner),
        .epoch=arc_rpc_observer_epoch(),.media_generation=media_generation,.ns=17,.size=1};w.bytes[0]=42;return w;
}
static bool sink_fail(void *p,const ArcRpcObservationTrace *t) { (void)p;(void)t;return false; }
static void services(CPUModel model)
{
    for(unsigned change=0;change<4;++change){fixture(model,0x13,0x100);arm_filter(~0u,0,0);
        ArcRpcHostWorkHandler h={valid,resolve_work,mutate,NULL};arc_rpc_observer_host_handler(ARC_RPC_OBS_DISC,&h);
        resolutions=mutations=0;revalidation_action=change;ArcRpcHostWork w=work(ARC_RPC_OBS_DISC);
        ArcRpcHostWork foreign=w;--foreign.epoch;assert(!arc_rpc_observer_queue(&foreign));
        assert(arc_rpc_observer_queue(&w));w.bytes[0]=0;arc_rpc_observer_drain();assert(!arc_rpc_observer_in_host());
        assert(resolutions==1&&mutations==(change==0));
    }
    fixture(model,0x13,0x100);arm_filter(~0u,0,0);ArcRpcObserveFilter f={.modes=~0u,.kinds=7};
    assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_ERROR,&f));ArcRpcHostWorkHandler h={valid,resolve_work,mutate,NULL};
    arc_rpc_observer_host_handler(ARC_RPC_OBS_DISC,&h);arc_rpc_observer_host_handler(ARC_RPC_OBS_ERROR,&h);
    ArcRpcHostWork other=work(ARC_RPC_OBS_ERROR);assert(arc_rpc_observer_queue(&other));
    for(unsigned i=1;i<ARC_RPC_WORK_LIMIT;++i){ArcRpcHostWork w=work(ARC_RPC_OBS_DISC);assert(arc_rpc_observer_queue(&w));}
    ArcRpcHostWork stale=work(ARC_RPC_OBS_DISC);assert(!arc_rpc_observer_queue(&stale));
    assert(!arc_rpc_observer_queue(&stale));resolutions=mutations=revalidation_action=0;arc_rpc_observer_drain();assert(mutations==1);
    ArcRpcHostWork w=work(ARC_RPC_OBS_DISC);assert(arc_rpc_observer_queue(&w));arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);
    arc_rpc_observer_drain();assert(mutations==1);assert(!arc_rpc_observer_queue(&w));
    w=work(ARC_RPC_OBS_DISC);assert(arc_rpc_observer_queue(&w));++media_generation;unsigned before_resolution=resolutions;arc_rpc_observer_drain();assert(mutations==1 && resolutions==before_resolution);
    w=work(ARC_RPC_OBS_DISC);assert(arc_rpc_observer_queue(&w));
    uint64_t generation=arc_rpc_observer_generation(ARC_RPC_OBS_DISC);before_resolution=resolutions;
    arc_rpc_observer_host_handler(ARC_RPC_OBS_DISC,&h);
    assert(arc_rpc_observer_generation(ARC_RPC_OBS_DISC)==generation+1);
    arc_rpc_observer_drain();assert(mutations==1 && resolutions==before_resolution);
    assert(!arc_rpc_observer_queue(&w));
    arc_rpc_observer_test_seed_pending();uint64_t epoch=arc_rpc_observer_epoch();arc_rpc_observer_retire(false);
    assert(arc_rpc_observer_epoch()==epoch+1 && arc_rpc_observer_test_clean());
    fixture(model,0x13,0x100);arc_rpc_observer_trace(true,false);rom[0x100/4]=0xef123456;
    arc_rpc_arm_exec_count(1);ArcRpcObservationTrace records[4];size_t n=arc_rpc_observer_trace_read(records,4);assert(n==1);
    assert(records[0].event.register_mask==0x6000 && records[0].event.swi==0x123456);
    for(unsigned i=0;i<13;++i)assert(!records[0].event.registers[i]);
    assert(!records[0].path[0]);
    arm.reg[15]=0x108;rom[0x100/4]=0xef000007;arm.reg[0]=21;arm.reg[1]=0x10000000;((uint8_t *)ram00)[0]=1;
    arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].scalar_valid && records[0].scalar==1);
    arm.reg[15]=0x108;rom[0x100/4]=0xef000008;arm.reg[0]=5;arm.reg[1]=0x10000000;memcpy(ram00,"Authored.Path\r",14);
    arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);
    assert(n==1 && !records[0].path[0]); /* RAM is readable; only opt-in denies the copy. */
    arc_rpc_observer_trace(true,true);arm.reg[15]=0x108;rom[0x100/4]=0xef000008;
    arm.reg[0]=5;arm.reg[1]=0x10000000;memcpy(ram00,"Authored.Path\r",14);
    arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);
    assert(n==1 && !strcmp(records[0].path,"Authored.Path") && records[0].inspection==ARC_RPC_INSPECT_OK);
    arm.reg[15]=0x108;arm.reg[0]=99;arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);
    assert(n==1 && !records[0].path[0] && records[0].event.register_mask==0x6001);
    arm.reg[15]=0x108;rom[0x100/4]=0xef0400df;arm.reg[4]=0x44;arm.reg[5]=0x55;
    arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);
    assert(n==1 && !records[0].path[0] && records[0].event.registers[4]==0x44 && records[0].event.registers[5]==0x55);
    arm.reg[15]=0x108;arm.reg[0]=0x10000000;ram00[0]=0x1234;arc_rpc_arm_exec_count(1);
    n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].scalar_valid && records[0].scalar==0x1234);
    arm.reg[15]=0x108;arm.reg[0]=0x100;arc_rpc_arm_exec_count(1);
    n=arc_rpc_observer_trace_read(records,4);assert(n==1 && !records[0].scalar_valid && records[0].inspection==ARC_RPC_INSPECT_REFUSED);
    const struct {uint32_t swi,reason;uint16_t mask;} masks[]={
        {6,129,7},{7,21,3},{7,99,1},{8,5,0xb},{8,99,1},{0x400df,0,0x3f}};
    for(unsigned k=0;k<sizeof(masks)/sizeof(masks[0]);++k){
        arc_rpc_observer_trace(true,false);rom[0x100/4]=0xef000000|masks[k].swi;arm.reg[15]=0x108;
        for(unsigned r=0;r<13;++r)arm.reg[r]=0xa000+r;
        arm.reg[0]=masks[k].reason;arm.reg[1]=0x10000000;
        uint32_t before[13];memcpy(before,arm.reg,sizeof(before));arc_rpc_arm_exec_count(1);
        n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].event.register_mask==(0x6000|masks[k].mask));
        for(unsigned r=0;r<13;++r)assert(records[0].event.registers[r]==((masks[k].mask&(1u<<r))?before[r]:0));
    }
    /* LOW-R1: privileged File SWIs must never acquire trace watches. */
    fixture(model,19,0x100);arc_rpc_observer_trace(true,false);
    ArcRpcTraceSelection privileged={.classes=7,.capacity=16};assert(arc_rpc_observer_trace_select(&privileged));
    rom[0x100/4]=0xef000008;arm.reg[0]=5;arm.reg[1]=0x10000000;
    arc_rpc_arm_exec_count(3);n=arc_rpc_observer_trace_read(records,4);
    for(size_t i=0;i<n;++i)assert(records[i].event.kind!=ARC_RPC_OBS_CONTINUATION);
    /* LOW-R2: an UpCall path reads at most 12 bytes, not a full path. */
    for(unsigned length=10;length<=12;++length){
        fixture(model,19,0x100);arc_rpc_observer_trace(true,true);
        rom[0x100/4]=0xef020033;arm.reg[0]=2;arm.reg[1]=8;arm.reg[2]=0x10000000;arm.reg[3]=0;
        memset(ram00,'A',length);((char *)ram00)[length]=0;
        arc_rpc_arm_exec_count(1);n=arc_rpc_observer_trace_read(records,4);assert(n==1);
        assert(records[0].inspection==(length<12?ARC_RPC_INSPECT_OK:ARC_RPC_INSPECT_TRUNCATED));
        assert(strlen(records[0].path)==(length<12?length:11));
        assert(records[0].event.register_mask==0x600f);
    }
    arc_rpc_observer_trace(true,false);rom[0x100/4]=0xef000007;
    for(unsigned i=0;i<1000;++i){arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);}
    ArcRpcTraceStatus status;arc_rpc_observer_trace_status(&status);
    assert(!status.incomplete && !status.lost && status.count==1000);
    arc_rpc_observer_trace(true,false);
    ArcRpcTraceSelection selection={.classes=ARC_RPC_OBS_SWI,.capacity=4};
    assert(arc_rpc_observer_trace_select(&selection));
    for(unsigned i=0;i<7;++i){arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);}
    arc_rpc_observer_trace_status(&status);assert(status.incomplete && status.lost==3 && status.count==4);
    n=arc_rpc_observer_trace_read(records,4);assert(n==4);
    arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);
    n=arc_rpc_observer_trace_read(records,4);assert(n==2 && records[0].event.kind==ARC_RPC_OBS_GAP);
    assert(records[0].lost==3 && records[0].lost_end_ns>=records[0].event.ns);
    assert(records[1].event.kind==ARC_RPC_OBS_SWI && records[1].event.order>records[0].event.order);
    arc_rpc_observer_trace_status(&status);assert(status.incomplete && status.lost==3);
    arc_rpc_observer_trace(true,false);selection=(ArcRpcTraceSelection){.classes=ARC_RPC_OBS_SWI,.capacity=4};
    assert(arc_rpc_observer_trace_select(&selection));
    for(unsigned i=0;i<7;++i){arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);}
    arc_rpc_observer_retire(true);arc_rpc_observer_trace_status(&status);
    assert(status.lost==7 && status.incomplete);
    n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].event.kind==ARC_RPC_OBS_GAP && records[0].lost==7);
    arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);
    n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].event.kind==ARC_RPC_OBS_SWI);
    arc_rpc_observer_trace(true,false);selection=(ArcRpcTraceSelection){.classes=ARC_RPC_OBS_SWI,.capacity=4,.swi_count=1,.swis={0x20007}};
    assert(arc_rpc_observer_trace_select(&selection));
    arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);assert(!arc_rpc_observer_trace_read(records,4));
    rom[0x100/4]=0xef020007;arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);
    assert(arc_rpc_observer_trace_read(records,4)==1 && records[0].event.swi==0x20007);
    selection.swis[0]=0x1000000;assert(!arc_rpc_observer_trace_select(&selection));
    arc_rpc_observer_trace(true,false);selection=(ArcRpcTraceSelection){.classes=ARC_RPC_OBS_MODE,.capacity=4};
    assert(arc_rpc_observer_trace_select(&selection));
    updatemode(0x10);arm.reg[16]=0x10;arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);
    n=arc_rpc_observer_trace_read(records,4);assert(n==1 && records[0].event.kind==ARC_RPC_OBS_MODE);
    arc_rpc_observer_trace(true,false);arm.reg[15]=0x108;arc_rpc_arm_exec_count(1);arc_rpc_observer_trace_drain(sink_fail,NULL);
    arc_rpc_observer_trace_status(&status);assert(status.failed&&status.incomplete&&status.lost);
    assert(arc_rpc_observer_qualification(0,model,ARC_RPC_OBS_DISC,ARC_RPC_CAP_REPLACE).enabled==false);
}
static void trace_interrupt_burst(CPUModel model)
{
    ARMState off={0};
    for(unsigned enabled=0;enabled<2;++enabled){
        fixture(model,0x10,0x100);
        rom[0x100/4]=0xef000007;rom[8/4]=0xe1b0f00e;rom[0x18/4]=0xe25ef004;
        arc_rpc_observer_trace(enabled,false);
        for(unsigned i=0;i<1000;++i){
            arm.reg[15]=0x108;arm.event=0;arc_rpc_arm_exec_count(1);
            arm.reg[16]&=~0x80u;arm.event=1;arc_rpc_arm_exec_count(1);
            assert(arm.mode==0x12);arm.event=0;arc_rpc_arm_exec_count(1);
            assert(arm.mode==0x10);
        }
        if(!enabled)off=arm;else assert(!memcmp(&off,&arm,sizeof(arm)));
        ArcRpcTraceStatus status;arc_rpc_observer_trace_status(&status);
        assert(!status.incomplete && !status.lost);
        if(enabled)assert(status.count==1000);
    }
}

static unsigned continuation_count(void)
{ unsigned n=0;for(unsigned i=0;i<seen_count;++i)n+=seen[i].kind==ARC_RPC_OBS_CONTINUATION;return n; }
static void watch_boundaries(CPUModel model)
{
    /* Deadline is inclusive: return fetch is exactly two attempts after SWI. */
    fixture(model,16,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;
    expected_pc=0x104;expected_mode=16;expected_sp=arm.reg[13];action=13;
    arc_rpc_arm_exec_count(1);action=0;arc_rpc_arm_exec_count(2);
    assert(continuation_count()==1 && seen[seen_count-1].matched);
    /* Refuse mode/continuation callbacks and the host after a user completion. */
    fixture(model,16,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;action=1;
    expected_sp=arm.reg[13];arc_rpc_arm_exec_count(1);action=11;arc_rpc_arm_exec_count(2);
    assert(continuation_count()==1);
    fixture(model,16,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;action=8;
    arc_rpc_arm_exec_count(1);assert(watch && arm.mode==16);
    assert(!arc_rpc_observer_watch(ARC_RPC_OBS_DISC,0x104,16,arm.reg[13],100));
    action=0;arc_rpc_arm_exec_count(1);assert(continuation_count()==1 && seen[seen_count-1].matched);
    for(unsigned width=0;width<2;++width){
        fixture(model,width?16:0,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;
        expected_mode=width?16:0;expected_sp=arm.reg[13];expected_pc=0x104;action=1;
        rom[2]=0xe8fd8000; /* LDMIA sp!,{pc}^: real status-restoring return. */
        arc_rpc_arm_exec_count(1);action=0;
        arm.reg[13]=0x10001000;ram00[0x1000/4]=arm.reg[14];
        arc_rpc_arm_exec_count(2);assert(continuation_count()==1 && seen[seen_count-1].matched);
    }
    for(unsigned aborting=0;aborting<2;++aborting){
        fixture(model,16,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;
        expected_mode=16;expected_sp=arm.reg[13];expected_pc=0x104;action=1;
        arm.reg[0]=5;arm.reg[1]=0x10000000;arc_rpc_observer_trace(true,false);
        ArcRpcTraceSelection selection={.classes=7,.capacity=32};assert(arc_rpc_observer_trace_select(&selection));
        rom[2]=0xe321f010; /* MSR mode to User does not release the watch. */
        rom[3]=aborting?0xe8d28000:0xe8d20001; /* LDM^ PC abort / no-PC success. */
        arm.reg[2]=aborting?0x00400000:0x10000000;arm.reg[3]=0x104;
        if(aborting){
            uint32_t *table=ram00+0x4000/4;memset(table,0,16384);
            table[0]=0x00000c02;table[0x100]=0x10000c02;enable(model,0);
            rom[4]=0xe321f010;rom[5]=0xe1a0f003; /* Abort: MSR User; MOV pc,r3 (non-S). */
        }else rom[4]=0xe1a00000;
        arc_rpc_arm_exec_count(1);action=0;arc_rpc_arm_exec_count(aborting?5:3);
        assert(!continuation_count());
        ArcRpcObservationTrace rows[32];size_t n=arc_rpc_observer_trace_read(rows,32);
        for(size_t i=0;i<n;++i)assert(rows[i].event.kind!=ARC_RPC_OBS_CONTINUATION);
    }
}
static void identity_guest(CPUModel model,bool tracing)
{
    fixture(model,16,0x100);rom[0x100/4]=0xef000008;
    arm.reg[0]=5;arm.reg[1]=0x10000000;memcpy(ram00,"Identity.Path\r",14);
    arc_rpc_observer_trace(tracing,false);
    ArcRpcTraceSelection prelude={.classes=7,.capacity=2};assert(arc_rpc_observer_trace_select(&prelude));
    uint64_t order=arc_rpc_observer_test_events(),attempt=arc_rpc_observer_test_attempts();
    arc_rpc_arm_exec_count(3);
    assert(arc_rpc_observer_test_events()==order && arc_rpc_observer_test_attempts()==attempt);
    arm_filter(~0u,0,0);
    expected_pc=0x104;expected_mode=16;expected_sp=arm.reg[13];action=1;
    arc_rpc_observer_trace(tracing,false);
    ArcRpcTraceSelection selection={.classes=7,.capacity=2};assert(arc_rpc_observer_trace_select(&selection));
    for(unsigned i=0;i<2;++i){arm.reg[15]=0x108;arc_rpc_arm_exec_count(3);}
    assert(continuation_count()==2);
}
static void identity_equality(CPUModel model)
{
    ArcRpcObservation off[128];unsigned count=0;
    for(unsigned tracing=0;tracing<2;++tracing){
        /* Seed a canonical watch to measure the counter before tracing starts. */
        fixture(model,16,0x100);arm_filter(~0u,0,0);rom[0x100/4]=0xef000008;
        expected_pc=0x104;expected_mode=16;expected_sp=arm.reg[13];action=1;
        arc_rpc_arm_exec_count(3);uint64_t base_watch=watch,base_order=arc_rpc_observer_test_events(),base_attempt=arc_rpc_observer_test_attempts();
        seen_count=0;arc_rpc_observer_trace(tracing,false);
        ArcRpcTraceSelection selection={.classes=7,.capacity=2};assert(arc_rpc_observer_trace_select(&selection));
        arm.reg[0]=5;
        for(unsigned i=0;i<2;++i){arm.reg[15]=0x108;arc_rpc_arm_exec_count(3);}
        assert(continuation_count()==2);
        for(unsigned i=0;i<seen_count;++i){ArcRpcObservation *e=&seen[i];
            e->order-=base_order;e->instruction-=base_attempt;e->epoch=0;
            if(e->watch)e->watch-=base_watch;
        }
        if(!tracing){count=seen_count;memcpy(off,seen,count*sizeof(*off));}
        else {assert(count==seen_count);for(unsigned i=0;i<count;++i){
            assert(off[i].order==seen[i].order && off[i].watch==seen[i].watch && off[i].instruction==seen[i].instruction);
            assert(off[i].kind==seen[i].kind && off[i].pc==seen[i].pc && off[i].mode==seen[i].mode);
            assert(!memcmp(off[i].registers,seen[i].registers,sizeof(off[i].registers)));
        }}
    }
}


#ifndef ARC_RPC_OBSERVERS_TEST
static void status_transport(CPUModel model)
{
    for(unsigned narrow=0;narrow<2;++narrow)for(unsigned n=0;n<27;++n){
        fixture(model,16,0x100);arc_rpc_observer_trace(true,false);
        ArcRpcTraceSelection selection={.classes=ARC_RPC_OBS_SWI,.capacity=32,.authored_status=narrow!=0};
        assert(arc_rpc_observer_trace_select(&selection));
        uint32_t w[16]={0x53435241u,1,64,0,0,0,0,0,1,1,123,456,0,0,0,0};
        bool valid=n==0 || n==1 || n==25;
        if(n==1 || n==15 || n==16 || n==24){w[3]=1;w[5]=2;w[7]=3;w[8]=15;}
        switch(n){
        case 2:w[0]=0;break;case 3:w[1]=2;break;case 4:w[2]=60;break;
        case 5:w[3]=2;w[5]=2;w[7]=3;break;case 6:w[4]=1;break;case 7:w[6]=1;break;
        case 8:w[8]=0;break;case 9:w[8]=8;break;case 10:w[9]=0;break;
        case 11:case 12:case 13:case 14:w[n+1]=1;break;
        case 15:w[5]=0;break;case 16:w[7]=0;break;
        case 24:w[8]=16;break;case 25:w[8]=7;break;case 26:w[5]=1;break;
        }
        memcpy(ram00+0x100/4,w,sizeof(w));
        arm.reg[0]=0;arm.reg[1]=1;arm.reg[2]=0;arm.reg[3]=0x10000100;
        arm.reg[4]=0x53435241u;arm.reg[5]=64;arm.reg[6]=w[3];
        if(n==17)arm.reg[6]=1;if(n==18)arm.reg[4]=0;if(n==19)arm.reg[5]=60;
        if(n==20)arm.reg[0]=1;if(n==21)arm.reg[1]=0;if(n==22)arm.reg[2]=1;
        if(n==23){memcpy(rom+0x1000/4,w,sizeof(w));arm.reg[3]=0x1000;}
        assert(!arc_rpc_observe_swi_at(0xef020006,0x100,rom));
        ArcRpcObservationTrace row;
        if(narrow && n>=18 && n<=22){assert(arc_rpc_observer_trace_read(&row,1)==0);continue;}
        assert(arc_rpc_observer_trace_read(&row,1)==1);
        assert(row.status_valid==valid && !row.path[0]);
        for(unsigned reg=3;reg<13;++reg)assert(!row.event.registers[reg]);
        assert(row.status_token==(valid?(uint64_t)w[4]|((uint64_t)w[5]<<32):0));
        assert(row.status_epoch==(valid?(uint64_t)w[6]|((uint64_t)w[7]<<32):0));
        assert(row.status_role==(valid?w[3]:0) && row.status_event==(valid?w[8]:0));
        assert(row.status_sequence==(valid?w[9]:0));
        assert(row.status_detail[0]==(valid?w[10]:0) && row.status_detail[1]==(valid?w[11]:0));
    }
}

int main(int argc,char **argv)
{
    arm_init();protected=1;if(setjmp(boundary)){fputs("Unexpected observation component fault\n",stderr);return 1;}
    config.mem_size=64;config.vram_size=2;mem_init();
    if(argc==3 && !strcmp(argv[1],"--identity")){
        for(unsigned model=0;model<4;++model){identity_guest((CPUModel)model,atoi(argv[2])!=0);
            for(unsigned i=0;i<seen_count;++i)printf("%u %llu %llu %llu %llu %u\n",model,
                (unsigned long long)seen[i].order,(unsigned long long)seen[i].instruction,
                (unsigned long long)seen[i].epoch,(unsigned long long)seen[i].watch,seen[i].kind);
        }
        arc_rpc_observer_retire(false);arc_rpc_memory_close();return 0;
    }
    for(unsigned model=0;model<4;++model){interpreter_equivalence((CPUModel)model);cheap_events((CPUModel)model);watch_boundaries((CPUModel)model);identity_equality((CPUModel)model);identity_guest((CPUModel)model,true);events((CPUModel)model);timing_events((CPUModel)model);completion((CPUModel)model);inspector((CPUModel)model);services((CPUModel)model);trace_interrupt_burst((CPUModel)model);status_transport((CPUModel)model);}
    for(unsigned model=0;model<4;++model)cheap_fault((CPUModel)model);
    arc_rpc_observer_retire(false);arc_rpc_memory_close();puts("RPC observation: four CPUs, events, inspector values/purity, completion, generations, queue and trace");return 0;
}

#endif
