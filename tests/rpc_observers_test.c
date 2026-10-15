/* Authored instruction fixtures through the actual adapter/interpreter.
 * No firmware content. GPL-2.0-only. */
#define ARC_RPC_OBSERVERS_TEST
#include "rpc_observer_test.c"
#include "rpc_observers.h"
static uint64_t clock_ns,media[2]={1,1};
static bool identities_ok=true;
static char helper_name[11],hd_name[11];
uint64_t arc_rpc_time_ns(void) { return clock_ns; }
uint64_t arc_rpc_fdc_media_generation(unsigned d) { return media[d]; }
bool arc_rpc_observers_identities(char helper[11],char hd[11])
{ strcpy(helper,helper_name);strcpy(hd,hd_name);return identities_ok; }
static ArcErrorBox box(void) { ArcErrorBox result;arc_rpc_observers_error_snapshot(&result);return result; }
static void start(CPUModel model,unsigned cells)
{
    arc_rpc_observers_test_watch_budget(ARC_RPC_WATCH_ATTEMPT_LIMIT);fixture(model,16,0x100);memset(rom+64,0,512);arc_rpc_observers_bind(0,model);
    arc_rpc_observers_override(cells,0,0,UINT32_MAX);
    clock_ns=0;identities_ok=true;helper_name[0]=hd_name[0]=0;
    ram00[0x400/4]=0;
    rom[2]=0xe1b0f00e; /* default handler: MOVS pc,lr */
}
static void issue(uint32_t pc,uint32_t mode,uint32_t swi,uint32_t r0,uint32_t r1,uint32_t r2,uint32_t r3,uint32_t sp)
{
    updatemode(mode);arm.reg[16]=mode;arm.reg[15]=pc+8;
    if(mode<16)arm.reg[15]|=mode;
    arm.event=0;arm.reg[0]=r0;arm.reg[1]=r1;arm.reg[2]=r2;arm.reg[3]=r3;arm.reg[13]=sp;
    rom[pc/4]=0xef000000|swi;arc_rpc_arm_exec_count(1);
}
static void report(uint32_t flags) { issue(0x100,16,0x400df,0x10000400,flags,0,0,0x10008000); }
static void poll(uint32_t pc,uint32_t mode,uint32_t r1,uint32_t r2,uint32_t sp)
{ issue(pc,mode,6,129,r1,r2,0,sp); }
static void error_cases(CPUModel model)
{
    for(unsigned cells=0;cells<16;cells+=4){
        start(model,cells);assert(arc_rpc_observers_capability(ARC_RPC_CELL_ERROR)==((cells&4)!=0));report(3);
        assert(box().recognised_open==((cells&4)!=0));
        if(!(cells&4)){assert(box().modal_unknown);assert(!arc_rpc_observers_answer(ARC_ERROR_OK));continue;}
        assert(!box().answer_available);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
        poll(0x180,19,1,0,0x10001000);assert(box().stage==ARC_ERROR_POLL); /* unrelated earlier read */
        poll(0x184,18,0,0,0x10001000);assert(box().stage==ARC_ERROR_POLL); /* IRQ */
        poll(0x188,19,0,1,0x10001000);assert(box().stage==ARC_ERROR_POLL);
        poll(0x18c,19,0,0,0x10001000);
        assert(arm.reg[15]==16);
        assert(box().site==0x18c && box().answer_available==((cells&8)!=0));
        if(!(cells&8)){assert(!arc_rpc_observers_answer(ARC_ERROR_OK));continue;}
        ArcErrorBox local=box();uint8_t ignored=0;
        assert(arc_error_request(&local,ARC_ERROR_OK));
        assert(!arc_error_poll(&local,0,0,local.site,19,local.sp,false,&ignored) && local.pending==ARC_ERROR_OK);
        assert(arc_error_poll(&local,0,0,local.site,19,local.sp,true,&ignored) && ignored==13);
        assert(arc_rpc_observers_answer(ARC_ERROR_OK));
        poll(0x180,19,0,0,0x10001000);assert(box().pending==ARC_ERROR_OK && arm.mode==19);
        poll(0x18c,19,0,0,0x10001004);assert(box().pending==ARC_ERROR_OK);
        poll(0x18c,3,0,0,0x10001000);assert(box().pending==ARC_ERROR_OK);
        poll(0x18c,19,1,0,0x10001000);assert(box().pending==ARC_ERROR_OK);
        /* Same VA fetched from RAM must not consume. */
        updatemode(19);arm.reg[16]=19;arm.reg[15]=0x1000018c+8;arm.reg[0]=129;arm.reg[1]=arm.reg[2]=0;
        ram00[0x18c/4]=0xef000006;arc_rpc_arm_exec_count(1);assert(box().pending==ARC_ERROR_OK);
        poll(0x18c,19,0,0,0x10001000);
        assert(box().consumed==ARC_ERROR_OK && arm.reg[1]==13 && arm.reg[2]==0 && arm.mode==19);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
        /* Real user return ends the generation before fetching its body. */
        arm.reg[14]=0x104;arm.spsr[3]=16;arm.reg[15]=16;
        arc_rpc_arm_exec_count(2);assert(!box().recognised_open && !box().answer_available);
        assert(!arc_rpc_observers_answer(ARC_ERROR_CANCEL));
    }
    start(model,12);arc_rpc_observers_override(12,0,0,1u<<3);report(14);poll(0x18c,19,0,0,0x10001000);
    assert(!box().answer_available && !arc_rpc_observers_answer(ARC_ERROR_CANCEL));
    /* Qualified effective button/default masks can differ from raw flags;
     * no firmware error-number classification is needed or copied. */
    start(model,12);arc_rpc_observers_test_buttons(1u<<3,1u<<3,1u<<3);
    report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==27);
    start(model,12);arc_rpc_observers_test_buttons(0,1u<<3,1u<<3);
    report(3);poll(0x18c,19,0,0,0x10001000);
    assert(!arc_rpc_observers_answer(ARC_ERROR_OK) && arc_rpc_observers_answer(ARC_ERROR_CANCEL));
    poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==13);
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    arm.reg[14]=0x108;arm.spsr[3]=16;arm.reg[15]=16;arc_rpc_arm_exec_count(2);
    assert(!box().recognised_open && box().modal_unknown && !arc_rpc_observers_answer(ARC_ERROR_OK));
    for(unsigned flags=0;flags<32;++flags){
        start(model,12);report(flags);poll(0x18c,19,0,0,0x10001000);
        ArcErrorBox b=box();assert(b.recognised_open && b.answer_available);
        ArcErrorAnswer answer=b.cancel?ARC_ERROR_CANCEL:ARC_ERROR_OK;
        assert(arc_rpc_observers_answer(answer));poll(0x18c,19,0,0,0x10001000);
        assert(arm.reg[1]==(((answer==ARC_ERROR_CANCEL)==b.default_cancel)?13u:27u));
    }
    for(unsigned flags=0x20;flags<=0x100;flags+=0x20){
        start(model,12);report(flags);poll(0x18c,19,0,0,0x10001000);
        assert(box().recognised_open==(flags==0x100) && !box().answer_available);assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
    }
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    report(0x100);assert(!box().answer_available && box().pending==ARC_ERROR_NONE); /* no parent revival */
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    issue(0x190,19,0x400df,0,0x33,0,0,0x10001000);
    assert(box().modal_unknown && !box().recognised_open && box().pending==ARC_ERROR_NONE);
    poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==0 && !arc_rpc_observers_answer(ARC_ERROR_OK));
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);assert(box().modal_unknown && !box().recognised_open);
    start(model,12);arc_rpc_observers_test_watch_budget(0);report(3);
    assert(box().modal_unknown && !box().recognised_open && !arc_rpc_observers_answer(ARC_ERROR_OK));
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    assert(arc_rpc_observer_filter(ARC_RPC_OBS_ERROR,NULL));
    assert(box().modal_unknown && box().pending==ARC_ERROR_NONE && !arc_rpc_observers_answer(ARC_ERROR_OK));
    for(unsigned pending=0;pending<2;++pending){
        start(model,12);arc_rpc_observers_test_watch_budget(3);report(3);
        poll(0x18c,19,0,0,0x10001000);
        if(pending)assert(arc_rpc_observers_answer(ARC_ERROR_OK));
        /* Expiry before a return or before answering is synchronous. */
        poll(0x180,19,1,0,0x10001000);poll(0x184,19,1,0,0x10001000);poll(0x188,19,1,0,0x10001000);poll(0x188,19,1,0,0x10001000);
        assert(box().modal_unknown && !box().answer_available && box().pending==ARC_ERROR_NONE);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
    }
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);
    arc_rpc_observer_retire(false);assert(box().modal_unknown && !arc_rpc_observers_answer(ARC_ERROR_OK));
}
static unsigned resolve_count,observe_count,trigger_count;
static char resolved[128][256];
static unsigned resolve_disc(void *opaque,const char *path,ArcDiscOperation operation)
{ (void)opaque;assert(operation==ARC_DISC_CATALOGUE);strcpy(resolved[resolve_count++],path);return 1; }
static bool observe_disc(void *opaque,const char *path,unsigned candidate,unsigned observation,bool trigger)
{ (void)opaque;(void)path;assert(candidate==1 && observation);++observe_count;trigger_count+=trigger;return false; }
static void miss(const char *path,uint32_t pc)
{
    strcpy((char *)ram00,path);issue(pc,16,8,5,0x10000000,0,0,0x10008000);
    arm.reg[0]=0;arc_rpc_arm_exec_count(2);
}
static void disc_cases(CPUModel model)
{
    /* A second user call can replace a live watch without a status-restoring
     * return (for example after MSR). The old loss must not disarm it. */
    start(model,255);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    strcpy((char *)ram00,"ADFS::AuthDisc.$.Old");
    issue(0x100,16,8,5,0x10000000,0,0,0x10008000);
    miss("ADFS::AuthDisc.$.New",0x140);arc_rpc_observer_drain();
    assert(resolve_count==1 && !strcmp(resolved[0],"$.New"));
    start(model,255);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    strcpy((char *)ram00,"NextDisc");
    issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    ArcDiscRequest default_request;arc_rpc_observers_disc_snapshot(&default_request);assert(default_request.a_active);
    assert(arc_rpc_observers_test_disc_devices(0));arc_rpc_observers_disc_snapshot(&default_request);assert(!default_request.a_active);
    const uint32_t devices[]={0,1,2,3,4,UINT32_MAX,UINT32_MAX-1};
    for(unsigned forms=0;forms<=3;++forms){
        assert(arc_rpc_observers_test_disc_devices(forms));
        assert(!arc_rpc_observers_test_disc_devices(4)); /* Invalid does not change the mask. */
        for(unsigned i=0;i<sizeof(devices)/sizeof(devices[0]);++i){
            uint32_t device=devices[i];strcpy((char *)ram00,"NextDisc");
            issue(0x180,19,0x20033,2,8,0x10000000,device,0x10001000);
            ArcDiscRequest d;arc_rpc_observers_disc_snapshot(&d);
            bool admitted=(device==0 && (forms&ARC_DISC_DEVICE_0)) ||
                (device==UINT32_MAX && (forms&ARC_DISC_DEVICE_UNSPECIFIED));
            assert(d.a_active==admitted);arc_rpc_observers_disc_clear();
        }
    }
    assert(arc_rpc_observers_test_disc_devices(ARC_DISC_DEVICE_UNSPECIFIED));
    strcpy((char *)ram00,"ADFS::X.$");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    ArcDiscRequest invalid;arc_rpc_observers_disc_snapshot(&invalid);assert(!invalid.a_active);
    strcpy((char *)ram00,"12345678901");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    ArcDiscRequest d;arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active);
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x33,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active); /* non-X refused */
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    strcpy(helper_name,"NextDisc");arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    helper_name[0]=0;strcpy(hd_name,"NextDisc");arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active);
    hd_name[0]=0;identities_ok=false;arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));identities_ok=true;
    report(0x100);assert(!arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);assert(box().modal_unknown && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    start(model,255);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.First",0x100);miss("ADFS::AuthDisc.$.Second",0x140);
    arc_rpc_observer_drain();assert(resolve_count==2 && !strcmp(resolved[0],"$.First") && !strcmp(resolved[1],"$.Second"));
    assert(!trigger_count);miss("ADFS::AuthDisc.$.First",0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    unsigned before=resolve_count;miss("ADFS::AuthDisc.$.Stale",0x140);++media[0];arc_rpc_observer_drain();assert(resolve_count==before);
    miss("ADFS::AuthDisc.$.Cancel",0x140);arc_rpc_observers_disc_clear();arc_rpc_observer_drain();assert(resolve_count==before);
    miss("ADFS::1.$.Wrong",0x140);arc_rpc_observer_drain();assert(resolve_count==before);
    miss("ADFS::4.$.Wrong",0x140);arc_rpc_observer_drain();assert(resolve_count==before);
    for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Overflow",0x140);
    arc_rpc_observer_drain();assert(resolve_count==before);
    start(model,255);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.Before",0x100);++media[1];miss("ADFS::AuthDisc.$.After",0x140);
    arc_rpc_observer_drain();assert(resolve_count==1 && !strcmp(resolved[0],"$.After"));
    start(model,255);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.Before",0x100);
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observer_drain();assert(resolve_count==0);
    assert(arc_rpc_observers_test_disc_devices(ARC_DISC_DEVICE_0));
    arc_rpc_observers_rebase(model);strcpy((char *)ram00,"NextDisc");
    issue(0x180,19,0x20033,2,8,0x10000000,0,0x10001000);
    arc_rpc_observers_disc_snapshot(&d);assert(d.a_active);
    start(model,0);assert(!arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    arc_rpc_observers_bind(0,model);assert(!arc_rpc_observers_test_disc_devices(1));
}
static void miss_mode(const char *path,uint32_t pc,unsigned mode,bool found)
{
    strcpy((char *)ram00,path);issue(pc,mode,8,5,0x10000000,0,0,0x10008000);
    arm.reg[0]=found?1:0;arc_rpc_arm_exec_count(2);
}
/* PRM control forms do not make window lifetime equal call lifetime. */
static void report_return_status(unsigned mode,bool matched,uint32_t flags)
{
    unsigned svc=mode|3u;updatemode(svc);arm.reg[16]=svc;
    arm.reg[14]=(matched?0x104u:0x108u)|(mode<16?flags:0);arm.spsr[3]=mode|flags;
    arm.reg[15]=16|(svc<16?svc:0);arm.event=0;arc_rpc_arm_exec_count(2);
}
static void report_return(unsigned mode,bool matched)
{ report_return_status(mode,matched,0); }
static bool request_a(void)
{
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    return arc_rpc_observers_replace(ARC_DISC_SIGNAL_A);
}
static void control_report(unsigned mode,unsigned flags)
{ issue(0x100,mode,0x400df,0x10000400,flags,0,0,0x10008000); }
static void control_cases(CPUModel model)
{
    for(unsigned mode=0;mode<=16;mode+=16){
        const unsigned forms[]={0x23,0x41,0x63};
        for(unsigned n=0;n<3;++n){
            start(model,252);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
            assert(request_a());control_report(mode,forms[n]);
            assert(box().modal_unknown && !box().recognised_open && !box().answer_available);
            assert(!arc_rpc_observers_answer(ARC_ERROR_OK) && !arc_rpc_observers_answer(ARC_ERROR_CANCEL));
            assert(!request_a());report_return(mode,true);
            assert(box().modal_unknown==(n==0));assert(request_a()==(n!=0));
            if(n==0){
                for(unsigned idle=0;idle<2;++idle){
                    issue(0x1c0,mode,idle?0x600e1:0x400c7,0,0,0,0,0x10008000);
                    assert(box().modal_unknown && !request_a());
                }
                /* An unrelated/unknown report return cannot close it. */
                control_report(mode,0x80);report_return(mode,true);assert(box().modal_unknown && !request_a());
                control_report(mode,0x41);assert(box().modal_unknown);report_return(mode,true);
                assert(!box().modal_unknown && request_a());
            }
        }
        /* A mismatched close return remains unknown; a later recognised
         * synchronous sequence, including an observation-only form, recovers. */
        for(unsigned form=3;form<=0x100;form=0x100){
            start(model,252);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
            control_report(mode,0x23);report_return(mode,true);
            control_report(mode,0x41);report_return(mode,false);assert(box().modal_unknown && !request_a());
            issue(0x1c0,mode,0x400c7,0,0,0,0,0x10008000);assert(box().modal_unknown && !request_a());
            control_report(mode,0x41);report_return_status(mode,true,0x10000000u);
            assert(box().modal_unknown && !request_a());
            control_report(mode,form);assert(box().modal_unknown && !box().answer_available);
            poll(0x18c,mode|3u,0,0,0x10001000);assert(!box().answer_available && !arc_rpc_observers_answer(ARC_ERROR_OK));
            report_return(mode,true);assert(!box().modal_unknown && request_a());
            if(form==0x100)break;
        }
        for(unsigned close=0;close<2;++close){
            start(model,252);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
            arc_rpc_observers_test_watch_budget(2);control_report(mode,close?0x41:0x23);
            for(unsigned i=0;i<4;++i)issue(0x180,19,0x42,0,0,0,0,0x10001000);
            assert(!arc_rpc_observer_context && box().modal_unknown);
            issue(0x1c0,mode,0x400c7,0,0,0,0,0x10008000);assert(box().modal_unknown && !request_a());
        }
        start(model,252);control_report(mode,0x80);
        issue(0x1c0,mode,0x400c7,0,0,0,0,0x10008000);
        assert(box().modal_unknown); /* Non-control pending watch still gates poll recovery. */
        for(unsigned epoch=0;epoch<2;++epoch){
            start(model,252);control_report(mode,0x23);report_return(mode,true);assert(box().modal_unknown);
            if(epoch)arc_rpc_observer_retire(false);
            arc_rpc_observers_rebase(model);assert(!box().modal_unknown && !box().recognised_open);
            control_report(mode,3);assert(!box().modal_unknown);poll(0x18c,mode|3u,0,0,0x10001000);assert(box().answer_available);
        }
    }
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);
    assert(arc_rpc_observer_context);arc_rpc_observers_test_watch_budget(0);
    poll(0x18c,19,0,0,0x10001000);assert(box().modal_unknown && !arc_rpc_observer_context);
}
static unsigned retry_attempts,retry_insertions;
static bool busy_observation(void *opaque,const char *path,unsigned candidate,unsigned observation,bool trigger)
{
    (void)opaque;assert(candidate==1 && observation && !strcmp(path,"$.Later"));
    if(!trigger)return false;
    ArcDiscRequest copied;arc_rpc_observers_disc_snapshot(&copied);
    assert(copied.continuation==0x104 && copied.sp==0x10008000);
    ++retry_attempts;
    if(!arc_rpc_observers_replace(ARC_DISC_SIGNAL_B))return true;
    ++retry_insertions;return false;
}
static void adjudicated_cases(CPUModel model)
{
    control_cases(model);number_subscriptions(model);
    start(model,252);updatemode(19);arm.reg[16]=19;
    uint64_t captures=arc_rpc_observer_test_captures(),events=arc_rpc_observer_test_events(),checks=arc_rpc_observer_test_number_checks();
    issue(0x180,19,0x42,0,0,0,0,0x10001000);
    assert(arc_rpc_observer_test_captures()==captures && arc_rpc_observer_test_events()==events+1);
    ArcRpcObservation before=arc_rpc_observer_test_origin();
    assert(!arc_rpc_observe_swi_at(0xef000042,0x1c0,rom));
    ArcRpcObservation after=arc_rpc_observer_test_origin();
    assert(before.pc==after.pc && before.instruction==after.instruction && before.ns==after.ns);
    assert(arc_rpc_observer_test_captures()==captures && arc_rpc_observer_test_events()==events+2);
    assert(arc_rpc_observer_test_number_checks()==checks);
    start(model,252);arc_rpc_observers_test_watch_budget(3);report(3);
    for(unsigned i=0;i<5;++i)issue(0x180,19,0x42,0,0,0,0,0x10001000);
    assert(box().modal_unknown && !box().answer_available);

    for(unsigned mode=0;mode<=16;mode+=16){
        start(model,252);
        issue(0x100,mode,0x400df,0x10000400,3,0,0,0x10008000);
        poll(0x18c,19,0,0,0x10001000);assert(box().answer_available);
        assert(arc_rpc_observers_answer(ARC_ERROR_CANCEL));poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==27);
        start(model,252);resolve_count=observe_count=trigger_count=0;
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
        miss_mode("ADFS::AuthDisc.$.Both",0x100,mode,false);
        miss_mode("ADFS::AuthDisc.$.Both",0x100,mode,false);
        arc_rpc_observer_drain();assert(resolve_count==2 && trigger_count==1);
    }
    for(unsigned flags=0x501;flags<=0x903;flags+=0x402){
        start(model,252);report(flags);poll(0x18c,19,0,0,0x10001000);
        assert(box().recognised_open && !box().modal_unknown && !box().answer_available);
    }
    for(unsigned scenario=0;scenario<3;++scenario){
        start(model,252);resolve_count=observe_count=trigger_count=0;
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
        ArcDiscRequest shared={0};unsigned shared_triggers=0;
        for(unsigned iteration=0;iteration<2;++iteration){
            ArcDiscSwi input={.swi=8,.r0=5,.mode=0,.address=0x100,.user_sp=0x10008000,
                .name_readable=true,.name="ADFS::AuthDisc.$.Level2"};
            arc_disc_request_swi(&shared,&input,"AuthDisc");
            ArcDiscResult result=arc_disc_request_continue(&shared,0x104,0,0x10008000,0,false,false,0,0);
            shared_triggers+=result.trigger;
            miss("ADFS::AuthDisc.$.Level2",0x100);
            if(scenario!=1){
                input.address=0x140;input.name=scenario==0?"ADFS::AuthDisc.$.Music":"ADFS::AuthDisc.$.Level2";
                arc_disc_request_swi(&shared,&input,"AuthDisc");
                (void)arc_disc_request_continue(&shared,0x144,0,0x10008000,1,false,false,0,0);
                miss_mode(input.name,0x140,16,true);
            }
            if(scenario==0 || iteration==1){arc_rpc_observer_drain();assert(trigger_count==shared_triggers);if(scenario==2)assert(resolve_count==0);}
            if(scenario==1 && iteration==1){
                /* Shared Arculator counts these two before the lifecycle SWI. */
                assert(result.trigger);
            }
        }
        if(scenario==1){
            /* Repeat with the lifecycle in the very same guest frame. */
            start(model,252);resolve_count=observe_count=trigger_count=0;
            arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
            miss("ADFS::AuthDisc.$.Level2",0x100);miss("ADFS::AuthDisc.$.Level2",0x100);
            issue(0x180,16,0x400c7,0,0,0,0,0x10008000);arc_rpc_observer_drain();
            assert(resolve_count==2 && trigger_count==1);
            ArcDiscRequest cleared;arc_rpc_observers_disc_snapshot(&cleared);assert(!cleared.identities[0].occupied);
        }
    }
    /* A disqualifying return is not a found-object observation. It must not
     * cancel an earlier copied miss for that canonical object. */
    start(model,252);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.Valid",0x100);
    strcpy((char *)ram00,"ADFS::AuthDisc.$.Valid");issue(0x140,16,8,5,0x10000000,0,0,0x10008000);
    arm.reg[0]=1;arm.reg[14]=0x148;arc_rpc_arm_exec_count(2);arc_rpc_observer_drain();
    assert(resolve_count==1 && !strcmp(resolved[0],"$.Valid"));
    miss("ADFS::AuthDisc.$.Valid",0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    start(model,252);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.Valid",0x100);
    strcpy((char *)ram00,"ADFS::AuthDisc.$.Valid");issue(0x140,16,8,5,0x10000000,0,0,0x10008000);
    strcpy((char *)ram00,"RAM::Other.$.Valid");issue(0x140,16,8,5,0x10000000,0,0,0x10008000);
    arm.reg[0]=1;arc_rpc_arm_exec_count(2);arc_rpc_observer_drain();
    assert(resolve_count==1 && !strcmp(resolved[0],"$.Valid"));
    /* Lifecycle retirement also belongs to A-only qualification profiles. */
    start(model,52);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_notified();issue(0x184,19,0x20033,4,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observer_drain();ArcDiscRequest retired_a;arc_rpc_observers_disc_snapshot(&retired_a);
    assert(!retired_a.a_active && !retired_a.a_notified && !retired_a.a_name[0]);
    issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_snapshot(&retired_a);assert(retired_a.a_active && !retired_a.a_notified);
    start(model,52);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_notified();issue(0x184,19,0x20033,4,8,0x10000000,UINT32_MAX,0x10001000);
    issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_snapshot(&retired_a);assert(retired_a.a_active && !retired_a.a_notified);
    start(model,252);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();
    arc_rpc_observers_override(52,0,0,UINT32_MAX);
    issue(0x184,16,0x400c7,0,0,0,0,0x10008000);arc_rpc_observer_drain();
    arc_rpc_observers_override(252,0,0,UINT32_MAX);
    miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();assert(!trigger_count);
    for(unsigned triggered=0;triggered<2;++triggered){
        start(model,252);resolve_count=observe_count=trigger_count=0;
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
        miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();
        if(triggered){miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();}
        for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Overflow",0x140);
        arc_rpc_observer_drain();ArcDiscRequest kept;arc_rpc_observers_disc_snapshot(&kept);
        assert(kept.identities[0].occupied && !strcmp(kept.identities[0].object,"$.Before") && kept.b_triggered==(triggered!=0));
        miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    }
    start(model,252);retry_attempts=retry_insertions=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,busy_observation,NULL);
    miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);report(3);
    arc_rpc_observer_drain();assert(retry_attempts==1 && !retry_insertions);
    arc_rpc_observers_frame(1);assert(retry_attempts==2 && !retry_insertions);
    arm.reg[14]=0x104;arm.spsr[3]=16;arm.reg[15]=16;arc_rpc_arm_exec_count(2);
    assert(!box().recognised_open && !box().modal_unknown);
    arc_rpc_observers_frame(2);assert(retry_insertions==1);arc_rpc_observers_frame(3);assert(retry_insertions==1);
    for(unsigned drive=0;drive<2;++drive){
        start(model,252);retry_attempts=retry_insertions=0;
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,busy_observation,NULL);
        miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);report(3);
        arc_rpc_observer_drain();assert(retry_attempts==1 && !retry_insertions);
        ++media[drive];arc_rpc_observers_frame(1);assert(retry_attempts==1);
    }
    start(model,252);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.BeforePoll",0x100);
    issue(0x180,16,0x400c7,0,0,0,0,0x10008000);
    strcpy((char *)ram00,"ADFS::AuthDisc.$.AfterPoll");issue(0x140,16,8,5,0x10000000,0,0,0x10008000);
    arc_rpc_observer_drain();assert(resolve_count==1);
    arm.reg[0]=0;arc_rpc_arm_exec_count(2);arc_rpc_observer_drain();
    assert(resolve_count==2 && !strcmp(resolved[1],"$.AfterPoll"));
    start(model,252);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,7,0x10000000,UINT32_MAX,0x10001000);
    ArcDiscRequest d;arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active);
    updatemode(19);arm.reg[16]=19;arm.reg[15]=0x10000188;arm.reg[0]=2;arm.reg[1]=8;
    arm.reg[2]=0x10000000;arm.reg[3]=UINT32_MAX;ram00[0x180/4]=0xef020033;
    arc_rpc_arm_exec_count(1);arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active);
    issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_snapshot(&d);assert(d.a_active);
    issue(0x184,19,0x20033,4,8,0x10000000,UINT32_MAX,0x10001000);
    arc_rpc_observers_disc_snapshot(&d);assert(!d.a_active);
}

static void sustained_error_cases(CPUModel model)
{
    start(model,252);arc_rpc_observers_test_watch_budget(8);report(3);
    poll(0x18c,19,0,0,0x10001000);uint64_t generation=box().generation;
    /* Same instruction-deadline semantics with a shortened test budget:
     * thousands of budgets, no pad intent, then an actual answer/return. */
    for(unsigned i=0;i<10000;++i){
        poll(0x18c,19,0,0,0x10001000);
        assert(box().answer_available && box().generation==generation && !box().modal_unknown);
    }
    assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==13);
    arm.reg[14]=0x104;arm.spsr[3]=16;arm.reg[15]=16;arc_rpc_arm_exec_count(2);
    assert(!box().recognised_open && !box().modal_unknown && !arc_rpc_observer_context);
    for(unsigned wrong=0;wrong<6;++wrong){
        start(model,252);arc_rpc_observers_test_watch_budget(8);report(3);poll(0x18c,19,0,0,0x10001000);
        if(wrong==5){
            ram00[0x4000/4]=0x10000c02;cp_write(2,0x10004000);cp_write(3,3);
            cp_write(1,cp_read(1)|1u);flush(model);ram00[0x18c/4]=0xef000006;
        }
        for(unsigned i=0;i<9;++i){
            if(wrong==5){ /* Identical poll fetched from RAM. */
                updatemode(19);arm.reg[16]=19;arm.reg[15]=0x194;
                arm.reg[0]=129;arm.reg[1]=arm.reg[2]=0;arm.reg[13]=0x10001000;
                arc_rpc_arm_exec_count(1);
            }else poll(wrong==0?0x180:0x18c,wrong==1?3:19,
                wrong==2?1:0,wrong==3?1:0,wrong==4?0x10001004:0x10001000);
        }
        assert(box().modal_unknown && !box().recognised_open && !box().answer_available);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
    }
    for(unsigned which=0;which<2;++which){
        uint32_t swi=which?0x600e1:0x400c7;
        start(model,252);arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
        report(3);arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);
        strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
        assert(box().modal_unknown && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
        issue(0x190,19,swi,0,0,0,0,0x10001000);
        assert(box().modal_unknown); /* Privileged Wimp polls are no evidence. */
        issue(0x190,which?0:16,swi,0,0,0,0,0x10008000);
        assert(!box().modal_unknown && !box().recognised_open && !arc_rpc_observer_context);
        strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
        assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
        arc_rpc_observers_disc_clear();
        miss("ADFS::AuthDisc.$.After",0x100);arc_rpc_observer_drain();
        miss("ADFS::AuthDisc.$.After",0x100);arc_rpc_observer_drain();
        assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_B));
        report(3);issue(0x190,16,swi,0,0,0,0,0x10008000);
        assert(box().recognised_open && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_B));
        arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);
        assert(box().modal_unknown && !arc_rpc_observers_replace(ARC_DISC_SIGNAL_B));
        report(0x20);assert(box().modal_unknown && !box().recognised_open);
        issue(0x190,16,swi,0,0,0,0,0x10008000);
        assert(box().modal_unknown); /* An unrecognised pending report is still modal. */
    }
}
static void number_cases(CPUModel model)
{
    const uint32_t numbers[]={0,1,0x123,0x7fffffff,0x80000000,0x80000123,UINT32_MAX};
    for(unsigned ordinary=0;ordinary<2;++ordinary)for(unsigned i=0;i<sizeof(numbers)/sizeof(numbers[0]);++i){
        start(model,12);assert(arc_rpc_observers_test_error_numbers((ArcErrorNumberClass)ordinary));
        assert(!arc_rpc_observers_test_error_numbers((ArcErrorNumberClass)2));
        ram00[0x400/4]=numbers[i];report(3);poll(0x18c,19,0,0,0x10001000);
        bool allowed=!(numbers[i]&0x80000000u) && (ordinary || !numbers[i]);
        assert(box().recognised_open && box().answer_available==allowed);
        assert(arc_rpc_observers_answer(ARC_ERROR_OK)==allowed);
        poll(0x18c,19,0,0,0x10001000);assert(arm.reg[1]==(allowed?13u:0u));
        arc_rpc_observers_rebase(model);assert(box().profile->numbers==(ArcErrorNumberClass)ordinary);
        arc_rpc_observers_bind(0,model);assert(!arc_rpc_observers_test_error_numbers(ARC_ERROR_NUMBER_ORDINARY));
        assert(box().modal_unknown);
    }
    start(model,12);assert(arc_rpc_observers_test_error_numbers(ARC_ERROR_NUMBER_ORDINARY));
    issue(0x100,16,0x400df,0x03000000,3,0,0,0x10008000);poll(0x18c,19,0,0,0x10001000);
    assert(box().recognised_open && !box().answer_available);
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);assert(arc_rpc_observers_answer(ARC_ERROR_OK));
    assert(arc_rpc_observers_test_error_numbers(ARC_ERROR_NUMBER_ORDINARY));
    assert(box().modal_unknown && !box().answer_available && box().pending==ARC_ERROR_NONE);
}
static void phase_cases(CPUModel model)
{
    start(model,3);issue(0x100,16,0xe,0x1000,0,0,0,0x10008000);
    arc_rpc_observers_frame(3000000);assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_DISABLED);
    arc_rpc_observers_boot_arm();issue(0x100,19,0xe,0x1000,0,0,0,0x10001000);
    arc_rpc_observers_frame(3000000);assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_ARMED);
    updatemode(16);arm.reg[16]=16;arm.reg[15]=0x10000100+8;arm.reg[0]=0x1000;
    ram00[0x100/4]=0xef00000e;arc_rpc_arm_exec_count(1);arc_rpc_observers_frame(3000000);
    assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_ARMED);
    issue(0x100,16,0xe,0x8000,0,0,0,0x10008000);arc_rpc_observers_frame(3000000);
    assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_ARMED);
    issue(0x100,16,0xe,0x1000,0,0,0,0x10008000);arc_rpc_observers_frame(2000000);
    assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_PROMPT);
    assert(arc_rpc_observers_launch_arm("Run Authored"));strcpy((char *)ram00,"Run Other");
    issue(0x100,16,0x400de,0x10000000,0,0,0,0x10008000);
    ArcLaunchOutcome out;arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_WAIT);
    strcpy((char *)ram00,"Run Authored");issue(0x100,16,0x400de,0x10000000,0,0,0,0x10008000);
    /* Privileged launch traffic cannot match or cancel the user phase. */
    issue(0x104,19,0x400de,0x10000000,0,0,0,0x10001000);
    arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_EXIT);
    /* Each terminal step requires fetched ROM provenance independently. */
    const uint32_t terminal[]={0x11,0x400dd,0x400c7};
    const unsigned expected[]={ARC_OUTCOME_EXIT,ARC_OUTCOME_CLOSE,ARC_OUTCOME_END};
    for(unsigned step=0;step<3;++step){
        updatemode(16);arm.reg[16]=16;arm.reg[15]=0x10000200+8;
        arm.reg[0]=0;arm.reg[1]=5;arm.reg[2]=0;arm.reg[13]=0x10008000;
        ram00[0x200/4]=0xef000000|terminal[step];arc_rpc_arm_exec_count(1);
        arc_rpc_observers_launch_snapshot(&out);assert(out.state==expected[step]);
        issue(0x104+4*step,16,terminal[step],0,5,0,0,0x10008000);
    }
    assert(arc_rpc_observers_launch_arm("Run Authored"));
    issue(0x100,16,0x400de,0x10000000,0,0,0,0x10008000);
    arc_rpc_observers_refused(0);arc_rpc_observers_refused(4);
    issue(0x104,16,0x11,0,0x58454241,9,0,0x10008000);
    issue(0x108,16,0x400dd,0,5,0,0,0x10008000);issue(0x10c,16,0x400c7,0,0,0,0,0x10008000);
    arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_POLL && out.last_rc==9 && out.refused_hd==1 && out.refused_floppy==1);
    assert(arc_rpc_observers_launch_arm("Run Authored"));issue(0x100,16,0x400de,0x10000000,0,0,0,0x10008000);
    issue(0x104,16,0x400de,0x10000000,0,0,0,0x10008000);arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_CANCEL);
    assert(arc_rpc_observers_launch_arm("Run Authored"));arc_rpc_observers_frame(599999999);
    arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_WAIT);
    arc_rpc_observers_frame(600000000);arc_rpc_observers_launch_snapshot(&out);assert(out.state==ARC_OUTCOME_CANCEL);
    arc_rpc_observers_boot_arm();arc_rpc_observers_frame(39999999);assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_ARMED);
    arc_rpc_observers_frame(40000000);assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_EXPIRED);
}
static bool production_pair(unsigned catalogue,unsigned cpu)
{ return (catalogue==1 && cpu==0) || (catalogue==2 && (cpu==1 || cpu==3)) ||
    (catalogue==3 && cpu==2) || (catalogue==4 && (cpu==1 || cpu==2 || cpu==3)); }
static void production_start(unsigned catalogue,CPUModel model)
{
    fixture(model,16,0x100);arc_rpc_observers_bind(catalogue,model);
    clock_ns=0;identities_ok=true;helper_name[0]=hd_name[0]=0;ram00[0x400/4]=0;
}
static void production_cases(unsigned catalogue,CPUModel model)
{
    for(unsigned mode=3;mode<=19;mode+=16)for(unsigned flags=0;flags<32;++flags){
        production_start(catalogue,model);
        for(unsigned bit=1;bit<=128;bit*=2)
            assert(arc_rpc_observers_capability(bit)==((252u&bit)!=0));
        assert(arc_rpc_observer_active && !arc_rpc_observer_context);
        arc_rpc_observers_boot_arm();assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_DISABLED);
        assert(!arc_rpc_observers_launch_arm("Run Authored"));
        report(flags);assert(box().recognised_open && !box().answer_available);
        poll(0x180,mode,1,0,0x10001000);poll(0x180,18,0,0,0x10001000);
        assert(!box().site && !box().answer_available);
        /* The same poll from authored RAM may not teach a ROM site. */
        updatemode(19);arm.reg[16]=19;arm.reg[15]=0x10000048;
        arm.reg[0]=129;arm.reg[1]=arm.reg[2]=0;ram00[16]=0xef000006;
        arc_rpc_arm_exec_count(1);assert(!box().site);
        poll(0x18c,mode,0,0,0x10001000);
        ArcErrorBox b=box();bool answerable=flags==3 || flags==14;
        assert(b.answer_available==answerable && b.ok==(flags==3) && b.cancel==answerable && b.default_cancel==(flags==14));
        if(answerable){
            assert(arc_rpc_observers_answer(ARC_ERROR_OK)==(flags==3));
            if(flags==14)assert(arc_rpc_observers_answer(ARC_ERROR_CANCEL));
            poll(0x18c,mode,0,0,0x10001000);assert(arm.reg[1]==13);
            if(flags==3){
                production_start(catalogue,model);report(3);poll(0x18c,mode,0,0,0x10001000);
                assert(arc_rpc_observers_answer(ARC_ERROR_CANCEL));
                poll(0x18c,mode,0,0,0x10001000);assert(arm.reg[1]==27);
            }
        }else assert(!arc_rpc_observers_answer(ARC_ERROR_OK) && !arc_rpc_observers_answer(ARC_ERROR_CANCEL));
    }
    production_start(catalogue,model);arc_rpc_observers_test_cells(252u & ~ARC_RPC_CELL_ANSWER);
    report(3);poll(0x18c,19,0,0,0x10001000);
    assert(box().recognised_open && !box().answer_available && !arc_rpc_observers_answer(ARC_ERROR_OK));
    for(unsigned pending=0;pending<2;++pending){
        production_start(catalogue,model);
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
        strcpy((char *)ram00,"NextDisc");issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
        report(3);poll(0x18c,19,0,0,0x10001000);assert(box().answer_available);
        if(pending)assert(arc_rpc_observers_answer(ARC_ERROR_OK));
        arc_rpc_observer_retire(false);arc_rpc_observers_rebase(model);
        ArcDiscRequest retired;arc_rpc_observers_disc_snapshot(&retired);
        assert(!box().recognised_open && !box().modal_unknown && !box().answer_available && box().pending==ARC_ERROR_NONE && !retired.a_active);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK) && !arc_rpc_observer_context);
        unsigned cat,cpu;arc_rpc_observers_test_binding(&cat,&cpu);assert(cat==catalogue && cpu==(unsigned)model);
    }
    /* Serious/unreadable headers cannot claim calibrated buttons. */
    for(unsigned invalid=0;invalid<2;++invalid)for(unsigned flags=3;flags<=14;flags+=11){
        production_start(catalogue,model);ram00[0]=0x80000123;
        issue(0x100,16,0x400df,invalid?0x03000000:0x10000000,flags,0,0,0x10008000);
        poll(0x18c,19,0,0,0x10001000);
        assert(box().recognised_open && !box().answer_available && !box().ok && !box().cancel);
    }
    production_start(catalogue,model);report(0x100);poll(0x18c,19,0,0,0x10001000);
    assert(box().recognised_open && !box().answer_available);
    production_start(catalogue,model);ram00[0]=0x123;
    issue(0x100,16,0x400df,0x10000000,3,0,0,0x10008000);poll(0x18c,19,0,0,0x10001000);
    assert(box().answer_available);
    production_start(catalogue,model);
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    const uint32_t devices[]={0,1,4,UINT32_MAX};
    for(unsigned i=0;i<4;++i){
        arc_rpc_observers_disc_clear();strcpy((char *)ram00,"NextDisc");
        issue(0x180,19,0x20033,2,8,0x10000000,devices[i],0x10001000);
        ArcDiscRequest d;arc_rpc_observers_disc_snapshot(&d);assert(d.a_active==(devices[i]==UINT32_MAX));
    }
    assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    production_start(catalogue,model);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
    miss("ADFS::AuthDisc.$.First",0x100);arc_rpc_observer_drain();assert(resolve_count==1 && !trigger_count);
    miss("ADFS::AuthDisc.$.First",0x100);arc_rpc_observer_drain();assert(resolve_count==2 && trigger_count==1);
    assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_B));
    arc_rpc_observers_rebase(model);assert(arc_rpc_observers_capability(252));
    assert(!box().recognised_open && !arc_rpc_observer_context);
}
static void qualification_dump(void)
{
    size_t count=0;const ArcRpcObserverQualification *table=arc_rpc_observer_test_qualifications(&count);
    for(size_t i=0;i<count;++i){const ArcRpcObserverQualification *q=table+i;
        printf("table\t%u\t%u\t%u\t%u\t%u\t%u\t%s\n",q->catalogue,q->cpu,q->observer,q->capability,q->profile,q->enabled,q->reason);
    }
    for(unsigned catalogue=0;catalogue<=5;++catalogue)for(unsigned cpu=0;cpu<=4;++cpu)
        for(unsigned owner=0;owner<4;++owner)for(unsigned cap=0;cap<=ARC_RPC_CAP_REPLACE_B;++cap){
            ArcRpcObserverQualification q=arc_rpc_observer_qualification(catalogue,cpu,(ArcRpcObserver)owner,(ArcRpcObserverCapability)cap);
            printf("cell\t%u\t%u\t%u\t%u\t%u\t%u\t%s\n",q.catalogue,q.cpu,q.observer,q.capability,q.profile,q.enabled,q.reason);
        }
    for(unsigned catalogue=1;catalogue<=4;++catalogue)for(unsigned cpu=0;cpu<4;++cpu)if(production_pair(catalogue,cpu)){
        production_start(catalogue,(CPUModel)cpu);ArcErrorBox b=box();const ArcErrorProfile *e=b.profile;assert(e);
        printf("error\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",catalogue,cpu,e->contextual,e->poll_r1,e->poll_r2,e->poll_modes,e->answer_forms,e->ok_forms,e->cancel_forms,e->default_cancel_forms,e->numbers);
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);strcpy((char *)ram00,"NextDisc");
        issue(0x180,19,0x20033,2,8,0x10000000,UINT32_MAX,0x10001000);
        ArcDiscRequest d;arc_rpc_observers_disc_snapshot(&d);const ArcDiscProfile *p=d.profile;assert(p);
        printf("disc\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",catalogue,cpu,p->user_modes,p->svc_modes,p->device_forms,p->require_x,p->strict_bare);
    }
}
int main(int argc,char **argv)
{
    arm_init();protected=1;if(setjmp(boundary))return 1;
    config.mem_size=64;config.vram_size=2;mem_init();
    if(argc==2 && !strcmp(argv[1],"--qualification")){qualification_dump();arc_rpc_observer_retire(false);arc_rpc_memory_close();return 0;}
    for(unsigned catalogue=0;catalogue<=5;++catalogue)for(unsigned cpu=0;cpu<4;++cpu)
        for(unsigned owner=0;owner<4;++owner)for(unsigned capability=0;capability<=ARC_RPC_CAP_REPLACE_B;++capability){
            ArcRpcObserverQualification q=arc_rpc_observer_qualification(catalogue,cpu,(ArcRpcObserver)owner,(ArcRpcObserverCapability)capability);
            bool enabled=production_pair(catalogue,cpu) && ((owner==ARC_RPC_OBS_ERROR && capability<=ARC_RPC_CAP_COMPLETE) ||
                (owner==ARC_RPC_OBS_DISC && capability!=ARC_RPC_CAP_COMPLETE));
            assert(q.enabled==enabled && q.profile==ARC_RPC_PROFILE_REVISION && q.reason && q.reason[0]);
        }
    for(unsigned catalogue=0;catalogue<=5;++catalogue)for(unsigned cpu=0;cpu<4;++cpu){
        if(production_pair(catalogue,cpu))production_cases(catalogue,(CPUModel)cpu);
        else {production_start(catalogue,(CPUModel)cpu);assert(!arc_rpc_observer_active && !arc_rpc_observers_capability(252));report(3);assert(box().modal_unknown && !box().recognised_open);}
    }
    for(unsigned model=0;model<4;++model){error_cases((CPUModel)model);disc_cases((CPUModel)model);adjudicated_cases((CPUModel)model);
        resolve_count=observe_count=trigger_count=0;sustained_error_cases((CPUModel)model);number_cases((CPUModel)model);phase_cases((CPUModel)model);}
    arc_rpc_observer_retire(false);arc_rpc_memory_close();puts("RPC observers: authored four-CPU error/disc/phase matrix");return 0;
}
