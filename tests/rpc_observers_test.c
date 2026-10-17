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
/* Run J: authored QA probes, with the shared Arculator observer as oracle. */
static void lifecycle(unsigned which)
{
    static const uint32_t swis[]={0x400c7,0x400e1,0x400c0,0x400dd,0x400de,0x11,0x40,0x33};
    issue(0x180,16,swis[which],which==7?4:0,0,0,0,0x10008000);
}
static void disc_fixture(CPUModel model)
{
    start(model,252);resolve_count=observe_count=trigger_count=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);
}
static ArcDiscResult shared_miss(ArcDiscRequest *shared,const char *path,bool found)
{
    ArcDiscSwi input={.swi=8,.r0=5,.mode=0,.address=0x100,.user_sp=0x10008000,
        .name_readable=true,.name=path,.candidate=1};
    arc_disc_request_swi(shared,&input,"AuthDisc");
    return arc_disc_request_continue(shared,0x104,0,0x10008000,found?1:0,false,false,0,0);
}
static void queue_loss_cases(CPUModel model)
{
    const char *x="ADFS::AuthDisc.$.X",*y="ADFS::AuthDisc.$.Y";
    for(unsigned n=0;n<=40;++n){
        disc_fixture(model);ArcDiscRequest shared={0};
        miss(x,0x100);arc_rpc_observer_drain();(void)shared_miss(&shared,x,false);
        for(unsigned i=0;i<n;++i){
            lifecycle(0);ArcDiscSwi input={.swi=0x400c7};arc_disc_request_swi(&shared,&input,"AuthDisc");
        }
        miss(x,0x100);arc_rpc_observer_drain();ArcDiscResult result=shared_miss(&shared,x,false);
        assert(trigger_count==result.trigger && trigger_count==(n==0));
    }
    for(unsigned k=8;k<=13;++k){
        disc_fixture(model);ArcDiscRequest shared={0};unsigned shared_triggers=0;
        for(unsigned frame=0;frame<20;++frame){
            resolve_count=0;
            for(unsigned i=0;i<k;++i){
                lifecycle(0);ArcDiscSwi input={.swi=0x400c7};arc_disc_request_swi(&shared,&input,"AuthDisc");
                miss(x,0x100);shared_triggers+=shared_miss(&shared,x,false).trigger;
            }
            arc_rpc_observers_frame(frame);arc_rpc_observer_drain();
            assert(trigger_count==shared_triggers && !trigger_count);
        }
    }
    /* Every lifecycle form, in every slot around overflow, amid FOUND and
     * miss items. No retired X history may count towards the next X miss. */
    for(unsigned which=0;which<8;++which)for(unsigned slot=0;slot<=17;++slot){
        disc_fixture(model);miss(x,0x100);arc_rpc_observer_drain();
        for(unsigned i=0;i<18;++i){
            if(i==slot)lifecycle(which);
            else miss_mode((i%3)==0?y:"ADFS::AuthDisc.$.Noise",0x140,16,(i%3)==0);
        }
        arc_rpc_observer_drain();resolve_count=0;unsigned before_trigger=trigger_count;
        miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==before_trigger);
        lifecycle(0);arc_rpc_observer_drain();miss(x,0x100);arc_rpc_observer_drain();
        ArcDiscRequest got;arc_rpc_observers_disc_snapshot(&got);assert(got.observations==1);
    }
    /* Every overflow clears X and Y history, whether FOUND was queued or
     * was the overflowing item. A lost FOUND conservatively keeps a latch. */
    for(unsigned slot=0;slot<=16;++slot){
        disc_fixture(model);miss(x,0x100);miss(y,0x140);arc_rpc_observer_drain();
        for(unsigned i=0;i<17;++i){
            if(i==slot)miss_mode(x,0x100,16,true);
            else miss("ADFS::AuthDisc.$.Noise",0x1c0);
        }
        arc_rpc_observer_drain();miss(x,0x100);arc_rpc_observer_drain();assert(!trigger_count);
        miss(y,0x140);arc_rpc_observer_drain();assert(!trigger_count);
        miss(y,0x140);arc_rpc_observer_drain();assert(!trigger_count);
        lifecycle(0);arc_rpc_observer_drain();miss(y,0x140);miss(y,0x140);arc_rpc_observer_drain();assert(trigger_count==1);
    }
    for(unsigned slot=0;slot<=16;slot+=16){
        disc_fixture(model);miss(x,0x100);miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
        for(unsigned i=0;i<17;++i){
            if(i==slot)miss_mode(x,0x100,16,true);else miss(y,0x140);
        }
        arc_rpc_observer_drain();ArcDiscRequest got;arc_rpc_observers_disc_snapshot(&got);assert(got.b_triggered);
        miss(x,0x100);miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
        miss_mode(x,0x100,16,true);arc_rpc_observer_drain();
        miss(x,0x100);miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
        lifecycle(0);arc_rpc_observer_drain();
        miss(x,0x100);miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==2);
    }
    for(unsigned triggered=0;triggered<2;++triggered){
        disc_fixture(model);miss(x,0x100);arc_rpc_observer_drain();
        if(triggered){miss(x,0x100);arc_rpc_observer_drain();}
        for(unsigned i=0;i<17;++i){if(i==8)lifecycle(0);else miss(y,0x140);}
        arc_rpc_observer_drain();ArcDiscRequest got;arc_rpc_observers_disc_snapshot(&got);
        assert(!got.b_triggered);
        miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==triggered);
        /* Every overflow clears new history, while lifecycle retirement
         * above still prevents a pre-clear trigger latch surviving. */
        for(unsigned i=0;i<17;++i)miss(y,0x140);
        arc_rpc_observer_drain();miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==triggered);
        miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==triggered);
        lifecycle(0);arc_rpc_observer_drain();miss(x,0x100);miss(x,0x100);arc_rpc_observer_drain();assert(trigger_count==triggered+1);
    }
}
static void busy_fixture(CPUModel model)
{
    disc_fixture(model);clock_ns=UINT64_C(1000000000);retry_attempts=retry_insertions=0;
    arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,busy_observation,NULL);
    miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);report(3);
    arc_rpc_observer_drain();assert(retry_attempts==1 && !retry_insertions && arc_rpc_observers_test_retry(NULL));
    report_return(16,true);assert(!box().recognised_open && !box().modal_unknown);
}
static void retry_guard_cases(CPUModel model)
{
    /* Run L: FOUND retires the matching BUSY retry before frame, while its
     * history/latch effects still wait for the drain. Both sites and modes;
     * unrelated FOUND and no-FOUND controls retain the successful retry. */
    for(unsigned mode=0;mode<=16;mode+=16)for(unsigned scenario=0;scenario<4;++scenario){
        busy_fixture(model);
        if(scenario<3)miss_mode(scenario==2?"ADFS::AuthDisc.$.Other":"ADFS::AuthDisc.$.Later",
            scenario==1?0x140:0x100,mode,true);
        bool cancelled=scenario<2;
        assert(arc_rpc_observers_test_retry(NULL)==!cancelled);
        ArcDiscRequest before;arc_rpc_observers_disc_snapshot(&before);assert(before.b_triggered);
        arc_rpc_observers_frame(clock_ns/1000);arc_rpc_observer_drain();
        assert(retry_attempts==(cancelled?1u:2u) && retry_insertions==!cancelled);
        arc_rpc_observers_frame(clock_ns/1000+1);assert(retry_insertions==!cancelled);
    }
    /* Presentation calls frame BEFORE drain. Entry must retire the BUSY
     * retry immediately, even though its history clear is still queued. */
    for(unsigned which=0;which<8;++which){
        busy_fixture(model);lifecycle(which);
        arc_rpc_observers_frame(clock_ns/1000);assert(retry_attempts==1 && !retry_insertions);
        assert(!arc_rpc_observers_test_retry(NULL));arc_rpc_observer_drain();
    }
    /* Exercise the drain's clear independently from entry's cancellation,
     * and FOUND's own retirement. Inspect pending state, since the separate
     * b_triggered/replacement gates also prevent a stale callback. */
    for(unsigned kind=3;kind<=4;++kind){
        busy_fixture(model);ArcRpcHostWork work;assert(arc_rpc_observers_test_retry(&work));
        work.kind=kind;assert(arc_rpc_observer_queue(&work));arc_rpc_observer_drain();
        assert(!arc_rpc_observers_test_retry(NULL));arc_rpc_observers_frame(clock_ns/1000);
        assert(retry_attempts==1 && !retry_insertions);
    }
    /* Same payload, distinct kinds: dropping a miss must retain FOUND and
     * lifecycle barriers, including their order in the real queue. */
    busy_fixture(model);ArcRpcHostWork work;assert(arc_rpc_observers_test_retry(&work));
    work.kind=2;assert(arc_rpc_observer_queue(&work));work.kind=3;assert(arc_rpc_observer_queue(&work));
    work.kind=4;assert(arc_rpc_observer_queue(&work));
    arc_rpc_observer_drop_queued(ARC_RPC_OBS_DISC,2,work.bytes,work.size);
    assert(!arc_rpc_observer_queued(ARC_RPC_OBS_DISC,2));
    assert(arc_rpc_observer_queued(ARC_RPC_OBS_DISC,3));
    assert(arc_rpc_observer_queued(ARC_RPC_OBS_DISC,4));
    arc_rpc_observer_drain();assert(!arc_rpc_observers_test_retry(NULL));
    for(unsigned scenario=0;scenario<9;++scenario){
        busy_fixture(model);ArcDiscRequest disc;arc_rpc_observers_disc_snapshot(&disc);
        uint64_t time=disc.triggered_us;
        switch(scenario){
            case 0:break; /* valid control */
            case 1:arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);break;
            case 2:disc.b_triggered=false;break;
            case 3:disc.triggered_continuation+=4;break;
            case 4:++disc.triggered_sp;break;
            case 5:strcpy(disc.triggered_object,"$.Other");break;
            case 6:time+=UINT64_C(120000000);break; /* inclusive limit */
            case 7:time+=UINT64_C(120000001);break;
            case 8:disc.triggered_us=time+1;break;
        }
        arc_rpc_observers_test_disc_state(&disc);assert(arc_rpc_observers_test_retry(NULL));
        arc_rpc_observers_frame(time);
        bool valid=scenario==0 || scenario==6;
        assert(retry_attempts==(valid?2u:1u) && retry_insertions==(valid?1u:0u));
        assert(!arc_rpc_observers_test_retry(NULL));arc_rpc_observers_frame(time+1);
        assert(retry_attempts==(valid?2u:1u));
    }
    start(model,252);control_report(16,0x23);report_return(16,true);assert(box().modal_unknown);
    arc_rpc_observers_override(252,0,0,UINT32_MAX);assert(!box().modal_unknown);
    report(3);poll(0x18c,19,0,0,0x10001000);assert(!box().modal_unknown && box().answer_available);
}
/* Run K: event-by-event oracle, including callbacks that do not insert. */
typedef struct {
    bool miss,seen,trigger;unsigned observation;uint32_t pc,sp;char object[256];
} DiscExpected;
static DiscExpected disc_expected[64];
static bool disc_exact;
static bool disc_boundary;
static const ArcDiscRequest *disc_oracle;
static unsigned diff_model,diff_seed,diff_frame;
static unsigned differential_triggers,differential_events,differential_overflows;
static unsigned differential_busy,differential_retries;
static unsigned differential_resolve(void *opaque,const char *path,ArcDiscOperation operation)
{ (void)opaque;(void)path;assert(operation==ARC_DISC_CATALOGUE || operation==ARC_DISC_OPEN_DIR);return 1; }
static bool differential_observe(void *opaque,const char *path,unsigned candidate,unsigned observation,bool trigger)
{
    (void)opaque;assert(candidate==1);ArcDiscRequest copy;arc_rpc_observers_disc_snapshot(&copy);
    if(disc_boundary){
        /* A BUSY retry is not a new miss/trigger. The shared observer has
         * already seen this frame's guest events, including FOUND/clears. */
        assert(trigger && observation==2 && disc_oracle->b_triggered);
        assert(!strcmp(path,disc_oracle->triggered_object));
        assert(copy.continuation==disc_oracle->triggered_continuation && copy.sp==disc_oracle->triggered_sp);
        ++differential_retries;return diff_frame%3!=0;
    }
    const ArcDiscIdentity *identity=NULL;
    for(unsigned i=0;i<ARC_DISC_IDENTITY_CAPACITY;++i){
        const ArcDiscIdentity *item=&copy.identities[i];
        if(item->occupied && item->continuation==copy.continuation && item->sp==copy.sp && !strcmp(item->object,path))identity=item;
    }
    assert(identity);unsigned event=(unsigned)(identity->observed_us%64);
    assert(event && event<64);DiscExpected *expected=&disc_expected[event];
    assert(expected->miss && !expected->seen && !strcmp(expected->object,path));
    assert(expected->pc+4==copy.continuation && expected->sp==copy.sp);expected->seen=true;
    if(disc_exact)assert(trigger==expected->trigger && observation==expected->observation);
    else if(trigger && !expected->trigger){
        fprintf(stderr,"disc subset mismatch: CPU=%u seed=%u frame=%u event=%u object=%s pc=%x sp=%x oracle-observation=%u RPC-observation=%u\n",
            diff_model,diff_seed,diff_frame,event,path,expected->pc,expected->sp,expected->observation,observation);
        assert(!trigger || expected->trigger);
    }
    differential_triggers+=trigger;
    bool busy=trigger && ((event+diff_seed)&1);differential_busy+=busy;return busy;
}
static uint32_t disc_random(uint32_t *state)
{ uint32_t value=*state;value^=value<<13;value^=value>>17;value^=value<<5;return *state=value; }
static void differential_cases(CPUModel model,unsigned seeds,unsigned frames)
{
    static const uint32_t initial[]={0x51c07,0x8a91,0x235719,0x643201};
    for(unsigned leg=0;leg<3;++leg)for(unsigned seed=0;seed<seeds;++seed){
        bool overflow=leg==1;
        disc_fixture(model);arc_rpc_observers_disc_configure(true,"AuthDisc",differential_resolve,differential_observe,NULL);
        ArcDiscRequest shared={0};ArcRpcScheduler stamp={0};uint32_t random=initial[seed];uint64_t bias=0;
        disc_oracle=&shared;
        /* Timestamp-only fixture clock: memory timing remains disabled.
         * Copied identities carry the original event tag through the drain. */
        assert(!arc_rpc_mem_fast.enabled);arc_rpc_mem_fast.clock=&stamp;
        if(leg==2){
            /* Prove liveness after a global clear, including arbitrary FOUND,
             * lifecycle and miss sequences after the suppressed prefix. */
            for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Lost",0x100);
            arc_rpc_observer_drain();lifecycle(seed%8);arc_rpc_observer_drain();
        }
        disc_exact=!overflow;diff_model=model;diff_seed=initial[seed];
        for(unsigned frame=0;frame<frames;++frame){
            if(frame%17==16)bias+=UINT64_C(120000064); /* >120 s, preserves event tags. */
            diff_frame=frame;
            memset(disc_expected,0,sizeof(disc_expected));
            unsigned length=1+disc_random(&random)%(overflow?48:16);
            differential_overflows+=length>16;
            for(unsigned event=1;event<=length;++event){
                uint64_t time=bias+(uint64_t)frame*64+event;stamp.now_ns=clock_ns=time*1000;
                unsigned kind=disc_random(&random)%32,mode=(disc_random(&random)&1)?16:0;
                uint32_t pc=0x100+0x40*(disc_random(&random)%3),sp=0x10008000+16*(disc_random(&random)&1);
                if(kind>=30){
                    static const uint32_t swis[]={0x400c7,0x400e1,0x400c0,0x400dd,0x400de,0x11,0x40,0x33};
                    unsigned which=disc_random(&random)%8;
                    ArcDiscSwi input={.swi=swis[which],.r0=which==7?4:0};
                    arc_disc_request_swi(&shared,&input,"AuthDisc");
                    issue(0x1c0,mode,input.swi,input.r0,0,0,0,sp);
                }else{
                    unsigned bound=(disc_random(&random)&3)?3:12;
                    unsigned object=disc_random(&random)%bound;
                    char path[64];snprintf(path,sizeof(path),"ADFS::AuthDisc.$.O%u",object);
                    bool found=kind>=24,find=(disc_random(&random)&1)!=0;
                    ArcDiscSwi input={.swi=find?13:8,.r0=find?0x40:5,.mode=0,.address=pc,.user_sp=sp,
                        .name_readable=true,.name=path,.candidate=1};
                    arc_disc_request_swi(&shared,&input,"AuthDisc");
                    ArcDiscResult result=arc_disc_request_continue(&shared,pc+4,0,sp,found?1:0,false,false,0,time);
                    DiscExpected *expected=&disc_expected[event];expected->miss=!found;
                    expected->trigger=result.trigger;expected->observation=result.observation;expected->pc=pc;expected->sp=sp;
                    assert(arc_disc_request_name(path,"AuthDisc",expected->object));
                    strcpy((char *)ram00,path);issue(pc,mode,input.swi,input.r0,0x10000000,0,0,sp);
                    arm.reg[0]=found?1:0;arc_rpc_arm_exec_count(2);
                }
                ++differential_events;
            }
            disc_boundary=true;arc_rpc_observers_frame(bias+(uint64_t)frame*64+length);disc_boundary=false;
            arc_rpc_observer_drain();
            if(disc_exact)for(unsigned event=1;event<=length;++event)assert(disc_expected[event].seen==disc_expected[event].miss);
        }
        arc_rpc_mem_fast.clock=NULL;disc_oracle=NULL;
    }
}
static void ordered_found_cases(CPUModel model)
{
    disc_fixture(model);ArcDiscRequest shared={0};unsigned expected=0;
    const char *a="ADFS::AuthDisc.$.A",*b="ADFS::AuthDisc.$.B";
    miss(b,0x100);expected+=shared_miss(&shared,b,false).trigger;
    miss(a,0x100);expected+=shared_miss(&shared,a,false).trigger;
    miss(a,0x100);expected+=shared_miss(&shared,a,false).trigger;
    arc_rpc_observer_drain();assert(trigger_count==expected && expected==1);
    miss(b,0x100);expected+=shared_miss(&shared,b,false).trigger;
    miss_mode(a,0x100,16,true);expected+=shared_miss(&shared,a,true).trigger;
    arc_rpc_observer_drain();assert(trigger_count==expected && expected==1); /* cumulative, no new frame-1 trigger */
    /* Earlier same-object misses must also produce their trigger before FOUND. */
    disc_fixture(model);miss(a,0x100);miss(a,0x100);miss_mode(a,0x100,16,true);
    arc_rpc_observer_drain();assert(trigger_count==1);ArcDiscRequest copy;arc_rpc_observers_disc_snapshot(&copy);assert(!copy.b_triggered);
    /* Retirement of an arm before its matching return must not manufacture
     * a FOUND marker that clears previously drained history. */
    disc_fixture(model);miss(a,0x100);arc_rpc_observer_drain();
    strcpy((char *)ram00,a);issue(0x100,16,8,5,0x10000000,0,0,0x10008000);
    arc_rpc_observers_disc_snapshot(&copy);assert(copy.armed);copy.armed=false;
    arc_rpc_observers_test_disc_state(&copy);arm.reg[0]=1;arc_rpc_arm_exec_count(2);
    arc_rpc_observer_drain();miss(a,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
}
static void overflow_eviction_cases(CPUModel model)
{
    const char *y="ADFS::AuthDisc.$.Y";
    for(unsigned n=8;n<=40;++n){
        disc_fixture(model);ArcDiscRequest shared={0};unsigned expected=0;
        miss(y,0x100);expected+=shared_miss(&shared,y,false).trigger;arc_rpc_observer_drain();
        for(unsigned i=0;i<n;++i){char path[64];snprintf(path,sizeof(path),"ADFS::AuthDisc.$.N%u",i);
            miss(path,0x100);expected+=shared_miss(&shared,path,false).trigger;}
        arc_rpc_observer_drain();miss(y,0x100);expected+=shared_miss(&shared,y,false).trigger;arc_rpc_observer_drain();
        assert(!expected && !trigger_count);
    }
    /* The overflowing lifecycle item itself positively retires a prior
     * completed trigger, even though it cannot enter the full queue. */
    for(unsigned which=0;which<8;++which){
        disc_fixture(model);miss(y,0x100);miss(y,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
        for(unsigned i=0;i<16;++i)miss("ADFS::AuthDisc.$.Noise",0x140);
        lifecycle(which);arc_rpc_observer_drain();ArcDiscRequest copy;arc_rpc_observers_disc_snapshot(&copy);assert(!copy.b_triggered);
        miss(y,0x100);miss(y,0x100);arc_rpc_observer_drain();assert(trigger_count==2);
    }
    /* A hint/failed commit leaves a latch. Overflow must not create a new
     * notice each frame, even after rebuilding two observations. */
    disc_fixture(model);miss(y,0x100);miss(y,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    for(unsigned frame=0;frame<20;++frame){
        resolve_count=0;for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Noise",0x140);
        arc_rpc_observer_drain();miss(y,0x100);miss(y,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    }
    for(unsigned overflow=0;overflow<2;++overflow){
        busy_fixture(model);
        if(overflow){for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Noise",0x140);
            assert(!arc_rpc_observers_test_retry(NULL));ArcDiscRequest copy;arc_rpc_observers_disc_snapshot(&copy);assert(!copy.b_triggered);}
        arc_rpc_observer_drain();arc_rpc_observers_frame(1);
        if(overflow){assert(!retry_insertions);miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);arc_rpc_observer_drain();
            assert(!retry_insertions);lifecycle(0);arc_rpc_observer_drain();
            miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);arc_rpc_observer_drain();}
        assert(retry_insertions==1);arc_rpc_observers_frame(2);assert(retry_insertions==1);
    }
}
static void suppression_recovery_cases(CPUModel model)
{
    static const uint32_t swis[]={0x400c7,0x400e1,0x400c0,0x400dd,0x400de,0x11,0x40,0x33};
    const char *a="ADFS::AuthDisc.$.A",*b="ADFS::AuthDisc.$.B";
    for(unsigned recovery=0;recovery<13;++recovery){
        disc_fixture(model);ArcDiscRequest oracle={0};ArcRpcScheduler stamp={0};
        arc_rpc_mem_fast.clock=&stamp;
        for(unsigned i=0;i<17;++i){char path[64];
            if(i<2)strcpy(path,a);else snprintf(path,sizeof(path),"ADFS::AuthDisc.$.N%u",i);
            miss(path,0x100);(void)shared_miss(&oracle,path,false);
        }
        arc_rpc_observer_drain();assert(!trigger_count && !observe_count);
        miss(b,0x100);miss(b,0x100);arc_rpc_observer_drain();
        assert(!trigger_count && !observe_count); /* No new B hint either. */
        (void)shared_miss(&oracle,b,false);(void)shared_miss(&oracle,b,false);
        if(recovery<8){
            ArcDiscSwi input={.swi=swis[recovery],.r0=recovery==7?4:0};
            arc_disc_request_swi(&oracle,&input,"AuthDisc");lifecycle(recovery);arc_rpc_observer_drain();
        }else if(recovery==8){arc_disc_request_clear(&oracle);arc_rpc_observers_disc_clear();}
        else if(recovery==9){++media[0];arc_disc_request_clear(&oracle);
            arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,observe_disc,NULL);}
        else if(recovery==10){arc_disc_request_clear(&oracle);arc_rpc_observer_retire(false);arc_rpc_observers_rebase(model);}
        else if(recovery==11){arc_disc_request_clear(&oracle);arc_rpc_observers_rebase(model);}
        else stamp.now_ns=UINT64_C(120000001000); /* Quiet since time zero. */
        /* After recovery these events have identical history in both policies:
         * an explicit global clear, or every old identity is beyond its window. */
        for(unsigned event=0;event<8;++event){
            unsigned before=trigger_count;const char *path=event<4?b:a;
            ArcDiscSwi input={.swi=8,.r0=5,.mode=0,.address=0x100,.user_sp=0x10008000,
                .name_readable=true,.name=path,.candidate=1};
            arc_disc_request_swi(&oracle,&input,"AuthDisc");
            ArcDiscResult result=arc_disc_request_continue(&oracle,0x104,0,0x10008000,0,false,false,0,stamp.now_ns/1000);
            miss(path,0x100);arc_rpc_observer_drain();assert(trigger_count-before==result.trigger);
        }
        assert(trigger_count==1 && observe_count==8);arc_rpc_mem_fast.clock=NULL;
    }
    /* Suppression also gates a retained completed latch; signal A keeps its
     * existing independent admission and supersession behavior. */
    disc_fixture(model);miss(a,0x100);miss(a,0x100);arc_rpc_observer_drain();assert(trigger_count==1);
    for(unsigned i=0;i<17;++i)miss("ADFS::AuthDisc.$.Noise",0x140);
    assert(!arc_rpc_observers_replace(ARC_DISC_SIGNAL_B));
    assert(request_a());
}
static unsigned quiet_resolve(void *opaque,const char *path,ArcDiscOperation operation)
{ (void)opaque;(void)operation;return !strcmp(path,"$.Unmapped")?UINT_MAX:1; }
static bool quiet_event(ArcDiscRequest *oracle,ArcRpcScheduler *stamp,const char *path,uint64_t time,bool found,bool exact)
{
    stamp->now_ns=time*1000;clock_ns=stamp->now_ns;
    ArcDiscSwi input={.swi=8,.r0=5,.mode=0,.address=0x100,.user_sp=0x10008000,
        .name_readable=true,.name=path,.candidate=strstr(path,".Unmapped")?UINT_MAX:1};
    arc_disc_request_swi(oracle,&input,"AuthDisc");
    ArcDiscResult result=arc_disc_request_continue(oracle,0x104,0,0x10008000,found?1:0,false,false,0,time);
    unsigned before=trigger_count;
    miss_mode(path,0x100,16,found);arc_rpc_observer_drain();bool actual=trigger_count!=before;
    assert(!actual || result.trigger);if(exact)assert(actual==result.trigger);return actual;
}
static void quiet_recovery_cases(CPUModel model)
{
    const char *a="ADFS::AuthDisc.$.A",*b="ADFS::AuthDisc.$.B";
    /* Different lost histories, including a retained B observation beside the
     * lost A latch. FOUND of A or C cannot realign either history. */
    for(unsigned scenario=0;scenario<3;++scenario){
        disc_fixture(model);arc_rpc_observers_disc_configure(true,"AuthDisc",quiet_resolve,observe_disc,NULL);
        ArcDiscRequest oracle={0};ArcRpcScheduler stamp={0};arc_rpc_mem_fast.clock=&stamp;
        for(unsigned i=0;i<17;++i){char path[64];
            if(i<2)strcpy(path,a);else if(scenario==2 && i==16)strcpy(path,b);else snprintf(path,sizeof(path),"ADFS::AuthDisc.$.N%u",i);
            (void)shared_miss(&oracle,path,false);miss(path,0x100);
        }
        arc_rpc_observer_drain();assert(!observe_count);
        const char *found=scenario==2?a:"ADFS::AuthDisc.$.C";
        assert(!quiet_event(&oracle,&stamp,found,1,true,false));
        assert(!quiet_event(&oracle,&stamp,b,2,false,false));
        assert(!quiet_event(&oracle,&stamp,b,3,false,false));assert(!observe_count);
        /* A miss of any object, even unresolved, postpones expiry. */
        assert(!quiet_event(&oracle,&stamp,a,100000000,false,false));
        assert(!quiet_event(&oracle,&stamp,b,120000001,false,false));assert(!observe_count);
        assert(!quiet_event(&oracle,&stamp,"ADFS::AuthDisc.$.Unmapped",180000000,false,false));
        assert(!quiet_event(&oracle,&stamp,b,240000002,false,false));assert(!observe_count);
        /* Inclusive shared window is still suppressed and itself resets quiet. */
        assert(!quiet_event(&oracle,&stamp,b,360000002,false,false));assert(!observe_count);
        uint64_t time=480000003;
        for(unsigned i=0;i<32;++i){char path[64];snprintf(path,sizeof(path),"ADFS::AuthDisc.$.N%u",i%12);
            (void)quiet_event(&oracle,&stamp,i<2?b:path,time+i,false,true);
        }
        assert(trigger_count==1 && observe_count==32);arc_rpc_mem_fast.clock=NULL;
    }
    /* Two misses after quiet in one frame must recover at the first item's
     * timestamp, despite the second already having updated the latest arm. */
    disc_fixture(model);ArcRpcScheduler stamp={0};arc_rpc_mem_fast.clock=&stamp;
    for(unsigned i=0;i<17;++i)miss(a,0x100);arc_rpc_observer_drain();
    stamp.now_ns=UINT64_C(120000001000);miss(b,0x100);++stamp.now_ns;miss(b,0x100);
    arc_rpc_observer_drain();assert(trigger_count==1 && observe_count==2);arc_rpc_mem_fast.clock=NULL;
    disc_fixture(model);stamp.now_ns=UINT64_C(100000000000);arc_rpc_mem_fast.clock=&stamp;
    for(unsigned i=0;i<17;++i)miss(a,0x100);arc_rpc_observer_drain();
    stamp.now_ns=0;miss(b,0x100);miss(b,0x100);arc_rpc_observer_drain();
    assert(!observe_count && !trigger_count);arc_rpc_mem_fast.clock=NULL;
}
static void quiet_overflow_found_case(CPUModel model)
{
    disc_fixture(model);ArcDiscRequest oracle={0};ArcRpcScheduler stamp={0};arc_rpc_mem_fast.clock=&stamp;
    for(unsigned i=0;i<16;++i){
        const char *path="ADFS::AuthDisc.$.Old";miss(path,0x100);(void)shared_miss(&oracle,path,false);
    }
    stamp.now_ns=UINT64_C(100000000000);
    miss_mode("ADFS::AuthDisc.$.Found",0x140,16,true);
    (void)shared_miss(&oracle,"ADFS::AuthDisc.$.Found",true);
    arc_rpc_observer_drain();assert(!observe_count && !trigger_count);
    /* Last miss was at zero, not the overflowing FOUND at 100 seconds. */
    assert(!quiet_event(&oracle,&stamp,"ADFS::AuthDisc.$.Fresh",120000001,false,true));
    assert(observe_count==1);
    assert(quiet_event(&oracle,&stamp,"ADFS::AuthDisc.$.Fresh",120000002,false,true));
    assert(observe_count==2 && trigger_count==1);arc_rpc_mem_fast.clock=NULL;
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
            if(scenario==0 || iteration==1){arc_rpc_observer_drain();assert(trigger_count==shared_triggers);if(scenario==2)assert(resolve_count==2);}
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
        for(unsigned i=0;i<ARC_DISC_IDENTITY_CAPACITY;++i)assert(!kept.identities[i].occupied);
        assert(!kept.next_order && !kept.observations && kept.b_triggered==(triggered!=0));
        miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();assert(trigger_count==triggered);
        miss("ADFS::AuthDisc.$.Before",0x100);arc_rpc_observer_drain();assert(trigger_count==triggered);
        lifecycle(0);arc_rpc_observer_drain();miss("ADFS::AuthDisc.$.Before",0x100);miss("ADFS::AuthDisc.$.Before",0x100);
        arc_rpc_observer_drain();assert(trigger_count==triggered+1);
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
static ArcRpcLaunchAttempt phase_attempt;
static uint64_t phase_session=123456788;
static ArcRpcLaunchController phase_controller;
static uint32_t phase_tag[3];
static unsigned phase_revalidations,phase_changes,phase_aborts;
static unsigned phase_consumptions,phase_boundaries;
static bool phase_retirement_callbacks,phase_boundary_missing,phase_boundary_result;
static void phase_consumed(void *opaque) {(void)opaque;++phase_consumptions;}
static bool phase_boundary(void *opaque) {(void)opaque;++phase_boundaries;return phase_boundary_result;}
static void phase_valid(void *opaque) {(void)opaque;++phase_revalidations;}
static void phase_changed(void *opaque) {(void)opaque;++phase_changes;}
static void phase_aborted(void *opaque,ArcRpcAttemptInterrupt reason)
{(void)opaque;(void)reason;++phase_aborts;}
static void phase_prepare(void)
{
    phase_tag[0]=0x53435241;phase_tag[1]=64;phase_tag[2]=1;
    ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
    ArcRpcAttemptToken token;assert(arc_riscpc_attempt_init(&phase_attempt,++phase_session,1));
    assert(arc_riscpc_attempt_prepare(&phase_attempt,&plan,0,&token));
    assert(arc_riscpc_controller_build(&plan,&token,&phase_controller));
    memcpy((uint8_t *)ram00+0x20000,phase_controller.image,phase_controller.size);
    ArcRpcLaunchBinding binding={&phase_attempt,&phase_controller,phase_valid,NULL,phase_changed,phase_aborted};
    if(phase_retirement_callbacks){binding.consumed=phase_consumed;
        if(!phase_boundary_missing)binding.boundary=phase_boundary;}
    assert(arc_rpc_observers_attempt(&binding));assert(!arc_rpc_observers_attempt(&binding));
}
static void phase_bind(CPUModel model,unsigned cells)
{ start(model,cells);phase_prepare(); }
static void phase_swi(uint32_t pc,uint32_t swi,uint32_t r0,uint32_t r1,uint32_t r2,uint32_t r3)
{
    updatemode(16);arm.reg[16]=16;arm.reg[15]=pc+8;
    arm.reg[0]=r0;arm.reg[1]=r1;arm.reg[2]=r2;arm.reg[3]=r3;arm.reg[13]=0x10008000;
    assert(!arc_rpc_observe_swi_at(0xef000000|swi,pc,ram00+((pc-0x10000000)&~0x3ffu)/4));
}
static void phase_marker(unsigned event,unsigned sequence,unsigned detail)
{
    uint32_t *w=ram00+(0x20000+phase_controller.status)/4;w[8]=event;w[9]=sequence;w[10]=detail;
    arm.reg[4]=phase_tag[0];arm.reg[5]=phase_tag[1];arm.reg[6]=phase_tag[2];
    phase_swi(0x10020000+phase_controller.query,0x20006,0,1,0,0x10020000+phase_controller.status);
}
static void phase_retirement(CPUModel model)
{
    /* An unowned or refused consume creates no retirement. An applied one
     * calls its owner exactly once, independently of cells, and propagates
     * the boundary's success/failure while clearing the obligation. */
    phase_bind(model,0);phase_marker(1,1,0);
    assert(phase_attempt.delivery==ARC_RPC_DELIVERY_CONSUMED && !arc_rpc_observer_boundary_pending);
    phase_retirement_callbacks=true;
    for(unsigned kind=0;kind<3;++kind){
        phase_boundary_missing=kind==2;phase_boundary_result=kind==0;
        phase_bind(model,kind==1?3:0);phase_consumptions=phase_boundaries=0;
        assert(arc_rpc_observers_boundary() && !phase_boundaries);
        phase_marker(1,1,1); /* Refused detail, still PREPARED. */
        assert(!phase_consumptions && !arc_rpc_observer_boundary_pending);
        phase_marker(1,1,0);
        assert(phase_attempt.delivery==ARC_RPC_DELIVERY_CONSUMED && phase_consumptions==1);
        assert(arc_rpc_observer_boundary_pending);
        assert(arc_rpc_observers_boundary()==(kind==0));
        assert(!arc_rpc_observer_boundary_pending && phase_boundaries==(kind==2?0u:1u));
        assert(arc_rpc_observers_boundary() && phase_boundaries==(kind==2?0u:1u));
        phase_marker(1,1,0);assert(phase_consumptions==1 && !arc_rpc_observer_boundary_pending);
    }
    phase_retirement_callbacks=phase_boundary_missing=false;
}
static void phase_dispatch(void)
{
    unsigned previous=phase_revalidations;
    phase_marker(1,1,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_CONSUMED);
    assert(phase_revalidations==previous+1);
    phase_marker(2,2,0x4321);phase_marker(3,3,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_DISPATCH);
    phase_swi(0x10020000+phase_controller.start,0x600de,0x10020000+phase_controller.command,0,0,0);
}
static void phase_retry_cases(CPUModel model)
{
    for(unsigned expired=0;expired<2;++expired){
        phase_bind(model,255);retry_attempts=retry_insertions=0;
        if(expired)arc_rpc_observers_frame(120000000);
        ArcRpcScheduler stamp={.now_ns=expired?UINT64_C(120000000000):0};
        arc_rpc_mem_fast.clock=&stamp;
        arc_rpc_observers_disc_configure(true,"AuthDisc",resolve_disc,busy_observation,NULL);
        miss("ADFS::AuthDisc.$.Later",0x100);miss("ADFS::AuthDisc.$.Later",0x100);report(3);
        arc_rpc_observer_drain();assert(retry_attempts==1 && !retry_insertions);
        uint64_t time=expired?120000001:1;
        arc_rpc_observers_frame(time);assert(retry_attempts==2 && !retry_insertions);
        report_return(16,true);assert(!box().recognised_open && !box().modal_unknown);
        arc_rpc_observers_frame(time+1);assert(retry_insertions==1);
        arc_rpc_observers_frame(time+2);assert(retry_insertions==1);
        arc_rpc_mem_fast.clock=NULL;
    }
}
static void phase_cases(CPUModel model)
{
    phase_bind(model,0);arc_rpc_observers_frame(120000000);
    assert(phase_attempt.delivery==ARC_RPC_DELIVERY_PREPARED && phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN && !arc_rpc_observer_context);
    assert(arc_rpc_observer_active);phase_marker(1,1,0);
    assert(phase_attempt.delivery==ARC_RPC_DELIVERY_UNKNOWN && phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN && !arc_rpc_observer_active);
    start(model,3);arc_rpc_observers_boot_arm();assert(arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_DISABLED);
    assert(!arc_rpc_observers_launch_arm("Run Authored"));
    for(unsigned cells=0;cells<4;++cells){
        phase_bind(model,cells);
        assert(!arc_rpc_observers_launch_arm(NULL) && !arc_rpc_observers_launch_arm("unrelated"));
        assert(arc_rpc_observers_launch_arm(phase_controller.text)==((cells&2)!=0));
        arc_rpc_observers_boot_arm();assert(arc_rpc_observers_boot_state()!=(ARC_BOOT_WATCH_ARMED));
        phase_dispatch();arc_rpc_observers_boot_arm();
        assert(arc_rpc_observers_boot_state()==((cells&1)?ARC_BOOT_WATCH_ARMED:ARC_BOOT_WATCH_DISABLED));
        unsigned changes=phase_changes;arc_rpc_observers_attempt_media();assert(phase_changes==changes+1);
        unsigned aborts=phase_aborts;arc_rpc_observers_attempt_interrupt(ARC_RPC_INTERRUPT_RESET);
        assert(phase_aborts==aborts+1 && phase_attempt.delivery==ARC_RPC_DELIVERY_INTERRUPTED);arc_rpc_observers_attempt_interrupt(ARC_RPC_INTERRUPT_RESET);
        assert(phase_aborts==aborts+1);
        phase_bind(model,cells);ArcRpcLaunchAttempt before=phase_attempt;
        for(unsigned tag=0;tag<3;++tag){uint32_t value=phase_tag[tag];phase_tag[tag]=0;
            phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));phase_tag[tag]=value;}
        const unsigned fields[]={0,1,2,3,4,5,6,7,11,12,13,14,15};
        uint32_t *status=ram00+(0x20000+phase_controller.status)/4;
        for(unsigned field=0;field<sizeof(fields)/sizeof(*fields);++field){unsigned at=fields[field];uint32_t value=status[at];status[at]^=0x10;
            phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));status[at]=value;}
        ((uint8_t *)ram00)[0x20000+phase_controller.command]^=1;
        phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));
        ((uint8_t *)ram00)[0x20000+phase_controller.command]^=1;
        phase_marker(3,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before))); /* dispatch before consume */
        phase_marker(1,2,0);assert(!memcmp(&before,&phase_attempt,sizeof(before))); /* gap */
        uint32_t *w=ram00+(0x20000+phase_controller.status)/4;
        w[4]^=1;phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));w[4]^=1;
        w[6]^=1;phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));w[6]^=1;
        w[12]=1;phase_marker(1,1,0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));w[12]=0;
        phase_dispatch();before=phase_attempt;
        phase_marker(1,1,0);phase_marker(2,2,999);assert(!memcmp(&before,&phase_attempt,sizeof(before)));
        /* Unrelated task starts/exits cannot establish an outcome. */
        issue(0x104,16,0x400de,0,0,0,0,0x10008000);issue(0x108,16,0x11,0,0,0,0,0x10008000);
        assert(!memcmp(&before,&phase_attempt,sizeof(before)));
        phase_marker(4,4,99);assert(phase_attempt.start_returned && phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
        phase_marker(5,5,99);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING); /* live child != returned */
        phase_marker(6,5,99);
        assert(arc_riscpc_attempt_established(&phase_attempt)==((cells&2)!=0));
        if(cells&2)assert(phase_attempt.outcome==ARC_RPC_OUTCOME_DESKTOP);
        phase_marker(8,6,0);arc_rpc_observers_frame(1000);
        assert(!arc_rpc_observer_active);
        if(!(cells&2))assert(phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN);
        before=phase_attempt;phase_marker(6,7,99);assert(!memcmp(&before,&phase_attempt,sizeof(before)));
    }
    const uint32_t bad_read[][4]={{0x10022000,0,32,126},{0x10022000,256,32,126},
        {0x10022000,255,0,126},{0x10022000,255,32,255},{0x50022000,255,32,126},{0,255,32,126},
        {0x17fffff0,255,32,126}};
    for(unsigned i=0;i<sizeof(bad_read)/sizeof(*bad_read);++i){
        phase_bind(model,3);phase_dispatch();
        phase_swi(0x10021000,0x2000e,bad_read[i][0],bad_read[i][1],bad_read[i][2],bad_read[i][3]);
        arc_rpc_observers_frame(300000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    }
    for(unsigned hint=0;hint<2;++hint)for(unsigned matched=0;matched<2;++matched){
        phase_bind(model,3);phase_dispatch();rom[0x104/4]=rom[0x108/4]=0xe1a00000;
        issue(0x100,16,0x2000e,0x10022000,255,32,126,0x10008000);
        arc_rpc_observers_frame(hint?300000:0);assert(phase_attempt.outcome==(hint?ARC_RPC_OUTCOME_WAITING:ARC_RPC_OUTCOME_PENDING) && arc_rpc_observers_boot_state()==(hint?ARC_BOOT_WATCH_PROMPT:ARC_BOOT_WATCH_ARMED));
        if(!matched)arm.reg[14]+=4;
        arc_rpc_arm_exec_count(2);
        assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING && arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_CANCELLED);
        assert(phase_attempt.last_event==(hint?(matched?ARC_RPC_EVENT_INPUT_RESUMED:ARC_RPC_EVENT_WAIT_UNOBSERVED):ARC_RPC_EVENT_DISPATCH));
    }
    phase_bind(model,3);phase_dispatch();
    phase_swi(0x10021000,0x2000e,0x10022000,255,0,255);arc_rpc_observers_frame(300000);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);arc_rpc_observers_frame(300000);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_WAITING);
    arc_rpc_observers_attempt_input();assert(phase_attempt.outcome==ARC_RPC_OUTCOME_WAITING && arc_rpc_observers_boot_state()==ARC_BOOT_WATCH_CANCELLED);
    arc_rpc_observers_frame(2000000);assert(!arc_rpc_observer_context && phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING && arc_rpc_observers_boot_state()!=ARC_BOOT_WATCH_PROMPT); /* budget ends */
    arc_rpc_observers_frame(120000000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN);
    assert(!arc_rpc_observer_context);ArcRpcLaunchAttempt stale=phase_attempt;
    phase_marker(5,4,0);assert(!memcmp(&stale,&phase_attempt,sizeof(stale)));
    phase_bind(model,3);phase_dispatch();phase_marker(4,4,0);phase_marker(5,5,0);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_RETURNED);
    phase_marker(8,6,0);arc_rpc_observers_frame(1000);
    assert(!arc_rpc_observer_active);
    for(unsigned dispatched=0;dispatched<2;++dispatched){
        phase_bind(model,3);phase_marker(1,1,0);phase_marker(2,2,0x4321);
        if(dispatched)phase_marker(3,3,0);
        phase_marker(7,dispatched?4:3,0);
        assert(phase_attempt.outcome==(dispatched?ARC_RPC_OUTCOME_UNKNOWN:ARC_RPC_OUTCOME_REJECTED));
        phase_marker(8,dispatched?5:4,0);arc_rpc_observers_frame(1000);
        assert(!arc_rpc_observer_active);
    }
    phase_bind(model,3);
    ((uint8_t *)ram00)[0x20000+phase_controller.plan]^=1;
    phase_marker(7,1,0);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_REJECTED);
    phase_bind(model,3);phase_marker(1,1,0);arc_rpc_observers_attempt_interrupt(ARC_RPC_INTERRUPT_RESET);
    stale=phase_attempt;phase_marker(2,2,1);assert(!memcmp(&stale,&phase_attempt,sizeof(stale)));
    phase_bind(model,3);arc_rpc_observers_retire();assert(phase_attempt.delivery==ARC_RPC_DELIVERY_INTERRUPTED);
    phase_bind(model,3);phase_dispatch();phase_swi(0x10021000,0x600c0,0,0,0,0);
    phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);arc_rpc_observers_frame(300000);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
}
/* L-2: isolate each refusal; other guards remain valid in every subject. */
static void phase_refusals(CPUModel model)
{
    for(unsigned bad=0;bad<5;++bad){
        phase_bind(model,3);
        uint32_t base=0x10020000;
        ArcRpcObservation e={.kind=ARC_RPC_OBS_SWI,.mode=16,.swi=0x20006};
        uint32_t *w=ram00+(0x20000+phase_controller.status)/4;
        w[8]=1;w[9]=1;w[10]=0;
        e.pc=base+phase_controller.query;
        e.registers[1]=1;e.registers[3]=base+phase_controller.status;
        e.registers[4]=0x53435241;e.registers[5]=64;e.registers[6]=1;
        if(bad==0)e.mode=19;
        if(bad==1)e.registers[0]=1;
        if(bad==2){memcpy((uint8_t *)ram00+0x40000,w,64);e.registers[3]=0x10040000;}
        if(bad==3){memcpy((uint8_t *)rom+0x20000,(uint8_t *)ram00+0x20000,phase_controller.size);
            e.pc=0x20000+phase_controller.query;e.registers[3]=0x20000+phase_controller.status;}
        if(bad==4){
            phase_marker(1,1,0);
            memcpy((uint8_t *)ram00+0x40000,(uint8_t *)ram00+0x20000,phase_controller.size);
            w=ram00+(0x40000+phase_controller.status)/4;w[8]=2;w[9]=2;w[10]=42;
            e.pc=0x10040000+phase_controller.query;e.registers[3]=0x10040000+phase_controller.status;
        }
        ArcRpcLaunchAttempt before=phase_attempt;arc_rpc_observers_test_status(&e);
        assert(!memcmp(&before,&phase_attempt,sizeof(before)));
        if(bad==4){
            /* REGISTERED has only recogniser-local effects. A following
             * dispatch at the relocated base must still be refused. */
            w[8]=3;w[9]=3;w[10]=0;arc_rpc_observers_test_status(&e);
            assert(!memcmp(&before,&phase_attempt,sizeof(before)));
            phase_marker(2,2,42);phase_marker(3,3,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_DISPATCH);}
        else {phase_marker(1,1,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_CONSUMED);}
    }
    phase_bind(model,3);phase_dispatch();
    phase_marker(8,4,0); /* CLOSED before an outcome must not advance sequence. */
    phase_marker(4,4,0);assert(phase_attempt.start_returned);
    phase_marker(5,5,0);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_RETURNED);
    for(unsigned desktop=0;desktop<2;++desktop){
        phase_bind(model,3);phase_dispatch();phase_marker(4,4,desktop?99:42);
        ArcRpcLaunchAttempt before=phase_attempt;
        phase_marker(desktop?6:5,5,desktop?98:0);assert(!memcmp(&before,&phase_attempt,sizeof(before)));
        phase_marker(desktop?6:6,5,desktop?99:42);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_DESKTOP);
    }
    for(unsigned bad=0;bad<3;++bad){
        phase_bind(model,3);phase_marker(1,1,0);phase_marker(2,2,42);phase_marker(3,3,0);
        uint32_t command=0x10020000+phase_controller.command;
        if(bad==1){strcpy((char *)ram00+0x40000,phase_controller.text);command=0x10040000;}
        if(bad==2)((char *)ram00)[0x20000+phase_controller.command]^=1;
        phase_swi(0x10020000+phase_controller.start+(bad==0?4:0),0x600de,command,0,0,0);
        phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);arc_rpc_observers_frame(250000);
        assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING && !arc_rpc_observer_context);
    }
    /* A competing task cancels a live recognised prompt. */
    phase_bind(model,3);phase_dispatch();phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);
    arc_rpc_observers_frame(250000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_WAITING);
    phase_swi(0x10021000,0x600de,0x10020000+phase_controller.command,0,0,0);
    assert(!arc_rpc_observer_context && phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);arc_rpc_observers_frame(300000);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    phase_bind(model,3);phase_dispatch();phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);
    phase_marker(4,4,0);assert(!arc_rpc_observer_context);
    arc_rpc_observers_frame(250000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    phase_bind(model,3);phase_dispatch();phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);
    phase_swi(0x10021004,0x2000e,0x10023000,255,32,126);
    arc_rpc_observers_frame(250000);assert(!arc_rpc_observer_context && phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    phase_bind(model,3);phase_dispatch();phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);
    /* Advance the adapter's instruction clock without executing 64M NOPs.
     * The first instruction at the exact ceiling is still admitted. */
    arc_rpc_observe_advance(63999999u);
    issue(0x300,19,6,129,0,0,0,0x10001000);assert(arc_rpc_observer_context);
    issue(0x300,19,6,129,0,0,0,0x10001000);assert(!arc_rpc_observer_context);
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
    /* Production reset/epoch retirement must work independently of the
     * test override, which now also clears the control-form latch. */
    for(unsigned epoch=0;epoch<2;++epoch){
        production_start(catalogue,model);rom[2]=0xe1b0f00e;
        control_report(16,0x23);report_return(16,true);assert(box().modal_unknown);
        if(epoch)arc_rpc_observer_retire(false);
        arc_rpc_observers_rebase(model);assert(!box().modal_unknown);
        report(3);poll(0x18c,19,0,0,0x10001000);assert(box().answer_available);
    }
    for(unsigned mode=3;mode<=19;mode+=16)for(unsigned flags=0;flags<32;++flags){
        production_start(catalogue,model);
        for(unsigned bit=1;bit<=128;bit*=2)
            assert(arc_rpc_observers_capability(bit));
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
/* D-203 promotion runs through production binding with no cell override. */
static void production_phase_cases(unsigned catalogue,CPUModel model)
{
    for(unsigned desktop=0;desktop<2;++desktop){
        production_start(catalogue,model);phase_prepare();
        assert(arc_rpc_observers_launch_arm(phase_controller.text));phase_dispatch();
        phase_marker(ARC_RPC_STATUS_START_RETURNED,4,desktop?99:0);
        assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
        phase_marker(desktop?ARC_RPC_STATUS_DESKTOP:ARC_RPC_STATUS_RETURNED,5,desktop?99:0);
        assert(phase_attempt.outcome==(desktop?ARC_RPC_OUTCOME_DESKTOP:ARC_RPC_OUTCOME_RETURNED));
        assert(arc_riscpc_attempt_established(&phase_attempt));
        phase_marker(ARC_RPC_STATUS_CLOSED,6,0);
    }
    production_start(catalogue,model);phase_prepare();phase_dispatch();
    phase_swi(0x10021000,0x2000e,0x10022000,255,32,126);
    arc_rpc_observers_frame(249999);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING);
    arc_rpc_observers_frame(250000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_WAITING);
    arc_rpc_observers_frame(2000000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING && !arc_rpc_observer_context);
    arc_rpc_observers_frame(120000000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN);
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
        ArcLaunchOutcome outcome;arc_rpc_observers_launch_snapshot(&outcome);const ArcOutcomeProfile *l=outcome.profile;assert(l);
        printf("launch\t%u\t%u\t%llu\t%u\t%u\t%u\t%u\n",catalogue,cpu,(unsigned long long)l->horizon_us,l->exit_rom,l->close_rom,l->poll_rom,l->contextual);
        ArcBootWatch watch;arc_rpc_observers_test_boot_snapshot(&watch);const ArcBootProfile *w=watch.profile;assert(w);
        printf("boot\t%u\t%u\t%llu\t%llu\t%u\t%u\t%u\t%u\n",catalogue,cpu,(unsigned long long)w->horizon_us,(unsigned long long)w->wait_us,w->buffer_limit,w->user_only,w->rom,w->contextual);
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
    if(argc==2 && !strcmp(argv[1],"--recovery-positive")){
        for(unsigned model=0;model<4;++model){suppression_recovery_cases((CPUModel)model);quiet_recovery_cases((CPUModel)model);quiet_overflow_found_case((CPUModel)model);}
        arc_rpc_observer_retire(false);arc_rpc_memory_close();puts("RPC suppression: global-clear/quiet-expiry liveness on four CPUs; A unaffected");return 0;
    }
    if(argc>=2 && !strcmp(argv[1],"--differential")){
        bool sanitizer=argc==3 && !strcmp(argv[2],"sanitizer");
        for(unsigned model=0;model<4;++model)differential_cases((CPUModel)model,sanitizer?2:4,sanitizer?64:256);
        assert(differential_busy && differential_retries);
        printf("RPC disc differential: %u events, %u triggers, %u overflow frames, %u BUSY deferrals, %u retries; four CPUs\n",
            differential_events,differential_triggers,differential_overflows,differential_busy,differential_retries);
        arc_rpc_observer_retire(false);arc_rpc_memory_close();return 0;
    }
    for(unsigned catalogue=0;catalogue<=5;++catalogue)for(unsigned cpu=0;cpu<4;++cpu)
        for(unsigned owner=0;owner<4;++owner)for(unsigned capability=0;capability<=ARC_RPC_CAP_REPLACE_B;++capability){
            ArcRpcObserverQualification q=arc_rpc_observer_qualification(catalogue,cpu,(ArcRpcObserver)owner,(ArcRpcObserverCapability)capability);
            bool enabled=production_pair(catalogue,cpu) && ((owner==ARC_RPC_OBS_ERROR && capability<=ARC_RPC_CAP_COMPLETE) ||
                (owner==ARC_RPC_OBS_DISC && capability!=ARC_RPC_CAP_COMPLETE) ||
                ((owner==ARC_RPC_OBS_BOOT || owner==ARC_RPC_OBS_LAUNCH) && capability==ARC_RPC_CAP_OBSERVE));
            unsigned revision=enabled && (owner==ARC_RPC_OBS_BOOT || owner==ARC_RPC_OBS_LAUNCH)?
                ARC_RPC_LAUNCH_PROFILE_REVISION:ARC_RPC_PROFILE_REVISION;
            assert(q.enabled==enabled && q.profile==revision && q.reason && q.reason[0]);
        }
    for(unsigned catalogue=0;catalogue<=5;++catalogue)for(unsigned cpu=0;cpu<4;++cpu){
        if(production_pair(catalogue,cpu)){production_cases(catalogue,(CPUModel)cpu);production_phase_cases(catalogue,(CPUModel)cpu);}
        else {production_start(catalogue,(CPUModel)cpu);assert(!arc_rpc_observer_active && !arc_rpc_observers_capability(252));report(3);assert(box().modal_unknown && !box().recognised_open);}
    }
    for(unsigned model=0;model<4;++model){error_cases((CPUModel)model);disc_cases((CPUModel)model);adjudicated_cases((CPUModel)model);
        queue_loss_cases((CPUModel)model);retry_guard_cases((CPUModel)model);
        ordered_found_cases((CPUModel)model);overflow_eviction_cases((CPUModel)model);
        suppression_recovery_cases((CPUModel)model);quiet_recovery_cases((CPUModel)model);quiet_overflow_found_case((CPUModel)model);
        differential_cases((CPUModel)model,1,8);
        resolve_count=observe_count=trigger_count=0;sustained_error_cases((CPUModel)model);number_cases((CPUModel)model);phase_cases((CPUModel)model);phase_refusals((CPUModel)model);phase_retry_cases((CPUModel)model);phase_retirement((CPUModel)model);}
    arc_rpc_observer_retire(false);arc_rpc_memory_close();puts("RPC observers: authored four-CPU error/disc/phase matrix");return 0;
}
