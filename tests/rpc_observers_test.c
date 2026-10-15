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
    rom[2]=0xe1b0f00e; /* default handler: MOVS pc,lr */
}
static void issue(uint32_t pc,uint32_t mode,uint32_t swi,uint32_t r0,uint32_t r1,uint32_t r2,uint32_t r3,uint32_t sp)
{
    updatemode(mode);arm.reg[16]=mode;arm.reg[15]=pc+8;
    if(mode<16)arm.reg[15]|=mode;
    arm.event=0;arm.reg[0]=r0;arm.reg[1]=r1;arm.reg[2]=r2;arm.reg[3]=r3;arm.reg[13]=sp;
    rom[pc/4]=0xef000000|swi;arc_rpc_arm_exec_count(1);
}
static void report(uint32_t flags) { issue(0x100,16,0x400df,0,flags,0,0,0x10008000); }
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
        poll(0x180,19,1,0,0x10001000);poll(0x184,19,1,0,0x10001000);poll(0x188,19,1,0,0x10001000);
        assert(box().modal_unknown && !box().answer_available && box().pending==ARC_ERROR_NONE);
        assert(!arc_rpc_observers_answer(ARC_ERROR_OK));
    }
    start(model,12);report(3);poll(0x18c,19,0,0,0x10001000);
    arc_rpc_observer_retire(false);assert(box().modal_unknown && !arc_rpc_observers_answer(ARC_ERROR_OK));
}
static unsigned resolve_count,observe_count,trigger_count;
static char resolved[16][208];
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
static ArcRpcLaunchAttempt phase_attempt;
static uint64_t phase_session=123456788;
static ArcRpcLaunchController phase_controller;
static uint32_t phase_tag[3];
static void phase_valid(void *opaque) {(void)opaque;}
static void phase_bind(CPUModel model,unsigned cells)
{
    start(model,cells);
    phase_tag[0]=0x53435241;phase_tag[1]=64;phase_tag[2]=1;
    ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
    ArcRpcAttemptToken token;assert(arc_riscpc_attempt_init(&phase_attempt,++phase_session,1));
    assert(arc_riscpc_attempt_prepare(&phase_attempt,&plan,0,&token));
    assert(arc_riscpc_controller_build(&plan,&token,&phase_controller));
    memcpy((uint8_t *)ram00+0x20000,phase_controller.image,phase_controller.size);
    ArcRpcLaunchBinding binding={&phase_attempt,&phase_controller,phase_valid,NULL};
    assert(arc_rpc_observers_attempt(&binding));assert(!arc_rpc_observers_attempt(&binding));
}
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
static void phase_dispatch(void)
{
    phase_marker(1,1,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_CONSUMED);
    phase_marker(2,2,0x4321);phase_marker(3,3,0);assert(phase_attempt.delivery==ARC_RPC_DELIVERY_DISPATCH);
    phase_swi(0x10020000+phase_controller.start,0x600de,0x10020000+phase_controller.command,0,0,0);
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
        if(!(cells&2))assert(phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN);
        before=phase_attempt;phase_marker(6,7,99);assert(!memcmp(&before,&phase_attempt,sizeof(before)));
    }
    const uint32_t bad_read[][4]={{0x10022000,0,32,126},{0x10022000,256,32,126},
        {0x10022000,255,0,126},{0x10022000,255,32,255},{0x50022000,255,32,126},{0,255,32,126}};
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
    arc_rpc_observers_attempt_input();assert(phase_attempt.outcome==ARC_RPC_OUTCOME_WAITING);
    arc_rpc_observers_frame(2000000);assert(!arc_rpc_observer_context && phase_attempt.outcome==ARC_RPC_OUTCOME_PENDING && arc_rpc_observers_boot_state()!=ARC_BOOT_WATCH_PROMPT); /* budget ends */
    arc_rpc_observers_frame(120000000);assert(phase_attempt.outcome==ARC_RPC_OUTCOME_UNKNOWN);
    assert(!arc_rpc_observer_context);ArcRpcLaunchAttempt stale=phase_attempt;
    phase_marker(5,4,0);assert(!memcmp(&stale,&phase_attempt,sizeof(stale)));
    phase_bind(model,3);phase_dispatch();phase_marker(4,4,0);phase_marker(5,5,0);
    assert(phase_attempt.outcome==ARC_RPC_OUTCOME_RETURNED);
    phase_marker(8,6,0);arc_rpc_observers_frame(1000);
    phase_bind(model,3);phase_marker(1,1,0);arc_rpc_observers_attempt_interrupt(ARC_RPC_INTERRUPT_RESET);
    stale=phase_attempt;phase_marker(2,2,1);assert(!memcmp(&stale,&phase_attempt,sizeof(stale)));
}
static bool production_pair(unsigned catalogue,unsigned cpu)
{ return (catalogue==1 && cpu==0) || (catalogue==2 && (cpu==1 || cpu==3)) ||
    (catalogue==3 && cpu==2) || (catalogue==4 && (cpu==1 || cpu==2 || cpu==3)); }
static void production_start(unsigned catalogue,CPUModel model)
{
    fixture(model,16,0x100);arc_rpc_observers_bind(catalogue,model);
    clock_ns=0;identities_ok=true;helper_name[0]=hd_name[0]=0;
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
    /* Zero is the record's only exercised error number. Nonzero/unreadable
     * headers cannot claim calibrated buttons, even with qualified flags. */
    for(unsigned invalid=0;invalid<2;++invalid)for(unsigned flags=3;flags<=14;flags+=11){
        production_start(catalogue,model);ram00[0]=1;
        issue(0x100,16,0x400df,invalid?0x03000000:0x10000000,flags,0,0,0x10008000);
        poll(0x18c,19,0,0,0x10001000);
        assert(box().recognised_open && !box().answer_available && !box().ok && !box().cancel);
    }
    production_start(catalogue,model);report(0x100);poll(0x18c,19,0,0,0x10001000);
    assert(box().recognised_open && !box().answer_available);
    production_start(catalogue,model);ram00[0]=0;
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
        printf("error\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",catalogue,cpu,e->contextual,e->poll_r1,e->poll_r2,e->poll_modes,e->answer_forms,e->ok_forms,e->cancel_forms,e->default_cancel_forms);
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
    for(unsigned model=0;model<4;++model){error_cases((CPUModel)model);disc_cases((CPUModel)model);phase_cases((CPUModel)model);}
    arc_rpc_observer_retire(false);arc_rpc_memory_close();puts("RPC observers: authored four-CPU error/disc/phase matrix");return 0;
}
