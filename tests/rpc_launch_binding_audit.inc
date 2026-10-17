/* Unit boundary supplement for the production-source orchestration harness.
 * Included only in temporary mutation builds, never a shipping translation
 * unit. The frontend publication test independently establishes real events.
 * GPL-2.0-only. */
#include <assert.h>
static uint8_t audit_media[32];
extern unsigned arc_predicate_input_calls(void);
static bool audit_identity(void *opaque,uint8_t out[32])
{(void)opaque;memcpy(out,audit_media,32);return true;}
static uint8_t *audit_retained_helper;
void arc_predicate_reset_before(ArcRpcPreparation *p)
{audit_retained_helper=p->generated_helper;}
static uint32_t audit_word(const uint8_t *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void arc_predicate_reset_after(ArcRpcPreparation *p)
{
 if(audit_retained_helper){
  assert(p->generated_helper==audit_retained_helper);
  const uint8_t *h=p->generated_helper+7*256;
  assert(!memcmp(h,"ARCRHLP1",8) && audit_word(h+16)==144);
  assert(audit_word(h+44)==136 && audit_word(h+48)==8 && audit_word(h+136)==1 && !audit_word(h+140));
  assert(audit_word(p->generated_helper+512+5+18)==144);
 }
}
void arc_predicate_binding_audit(ArcRpcPreparation *p)
{
    unsigned inputs=arc_predicate_input_calls();arc_engine_rpc_launch_input();assert(arc_predicate_input_calls()==inputs+1);
    ArcRpcPreparationState state=p->state;bool barrier=p->execution_barrier;
    bool override=p->request.helper_override;const ArcEngineMedia *helper=p->request.helper;
    ArcEngineMedia owned=p->helper;char path[1024];memcpy(path,p->helper_path,sizeof(path));
    p->execution_barrier=true;p->state=ARC_RPC_PREP_VERIFY;
    assert(!arc_riscpc_preparation_helper_write_policy(p,true));
    p->state=ARC_RPC_PREP_RUNNING;
    for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){
        p->request.helper_override=a!=0;p->request.helper=b?&p->helper:NULL;p->helper.writable=false;
        assert(arc_riscpc_preparation_helper_write_policy(p,true));assert(p->helper.writable==(a && b));
    }
    p->state=ARC_RPC_PREP_START;p->request.helper_override=true;
    char borrowed[]="/authored/helper";ArcEngineMedia view={.path=borrowed};
    assert(arc_riscpc_preparation_helper_override(p,&view));borrowed[1]='X';
    assert(!strcmp(p->helper.path,"/authored/helper"));
    p->state=state;p->execution_barrier=barrier;p->request.helper_override=override;p->request.helper=helper;
    p->helper=owned;memcpy(p->helper_path,path,sizeof(path));
    unsigned step=p->substep;bool suppressed=p->launch_suppressed,verified=p->product.verified;
    ArcRpcLaunchPlan launch=p->launch;const ArcRpcLaunchPlan *requested=p->request.launch;
    p->state=ARC_RPC_PREP_START;p->substep=2;p->launch_suppressed=false;p->product.verified=false;
    p->request.launch=&p->launch;p->launch.action=ARC_RPC_PLAN_PRODUCT;
    assert(cost_kind(p,false)==ARC_RPC_PREP_PRODUCT_KIND);
    assert(!next_namespace(p)); /* START has a completed store-open lease. */
    p->substep=1;assert(cost_kind(p,false)==ARC_RPC_PREP_START);p->substep=2;
    p->request.launch=NULL;assert(cost_kind(p,false)==ARC_RPC_PREP_START);p->request.launch=&p->launch;
    p->launch_suppressed=true;assert(cost_kind(p,false)==ARC_RPC_PREP_START);p->launch_suppressed=false;
    p->launch.action=ARC_RPC_PLAN_BOOT;assert(cost_kind(p,false)==ARC_RPC_PREP_START);p->launch.action=ARC_RPC_PLAN_PRODUCT;
    p->product.verified=true;assert(cost_kind(p,false)==ARC_RPC_PREP_START);
    p->state=ARC_RPC_PREP_RECORD;assert(cost_kind(p,false)==ARC_RPC_PREP_RECORD);
    ArcRpcLaunchRecordStore namespace_record=p->observations;ArcRpcHdStore *disc=p->disc;
    p->state=ARC_RPC_PREP_RECOVER;p->observations.ready=false;p->observations.phase=5;
    assert(next_namespace(p));p->observations.ready=true;
    char error[128];p->disc=arc_riscpc_hd_open_begin(p->save,p->request.key.content,error,sizeof(error));assert(p->disc);
    assert(next_namespace(p));arc_riscpc_hd_close(p->disc);p->disc=NULL;assert(!next_namespace(p));
    p->disc=disc;p->observations=namespace_record;
    /* A queued checkpoint has namespace effects after open has completed.
     * START reserves the larger ceiling for every waited maintenance tick. */
    uint8_t *writes=calloc(256,512);assert(writes);
    for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(p->disc,ARC_RPC_HD_SECTORS-256,256,writes));
    free(writes);arc_riscpc_hd_tick(p->disc);
    p->state=ARC_RPC_PREP_START;p->substep=2;p->launch_suppressed=false;
    bool checkpoint=false;
    for(unsigned i=0;;++i){
        assert(i<10000);ArcRpcHdStats stats;arc_riscpc_hd_stats(p->disc,&stats);
        if(stats.state==ARC_RPC_HD_IDLE)break;
        checkpoint=true;assert(next_namespace(p));arc_riscpc_hd_tick(p->disc);
    }
    assert(checkpoint && !next_namespace(p));
    p->state=state;p->substep=step;p->launch_suppressed=suppressed;p->product.verified=verified;
    p->launch=launch;p->request.launch=requested;
    ArcRpcBootRecord result;
    assert(!arc_riscpc_preparation_observation(NULL,&result));
    ArcRpcPreparationHost host=p->host;ArcRpcBootKey key=p->launch_key;
    ArcRpcLaunchRecordStore observations=p->observations;
    bool captured=p->launch_captured;uint8_t media[32];memcpy(media,p->launch_media_identity,32);
    memset(audit_media,1,32);p->host.media_identity=audit_identity;
    p->launch_captured=launch_bindings(p,&p->launch_key);assert(p->launch_captured);
    ArcRpcBootRecord old={.key=p->launch_key,.observation=ARC_RPC_OUTCOME_RETURNED};
    p->observations.old=old;p->observations.have_old=true;
    ArcRpcCmosStore cmos=p->record;bool receipted=p->receipted,population=p->needs_population;
    p->state=ARC_RPC_PREP_RECOVER;unit(p);assert(p->observations.have_old);
    p->state=state;p->record=cmos;p->receipted=receipted;p->needs_population=population;
    assert(arc_riscpc_preparation_observation(p,&result));
    assert(result.observation==old.observation && arc_riscpc_boot_record_matches(&result,&old.key));
    assert(!arc_riscpc_preparation_observation(p,NULL));assert(!p->observations.have_old);
    p->observations.old=old;
    assert(!arc_riscpc_preparation_observation(p,&result));
    p->observations.have_old=true;p->observations.old.key.configuration[0]^=1;
    assert(!arc_riscpc_preparation_observation(p,&result) && !p->observations.have_old);
    p->observations.old=old;p->observations.have_old=true;
    launch_revalidate(p);assert(p->observations.have_old);
    ++audit_media[0];launch_revalidate(p);
    assert(!p->launch_captured && !p->observations.have_old);
    assert(!arc_riscpc_preparation_observation(p,&result));
    --audit_media[0];p->launch_captured=true;p->observations.old=old;p->observations.have_old=true;
    p->seed[16]^=1;assert(!arc_riscpc_preparation_observation(p,&result));p->seed[16]^=1;
    assert(!p->launch_captured && !p->observations.have_old);
    p->launch_captured=true;p->observations.old=old;p->observations.have_old=true;p->observations.phase=10;
    arc_riscpc_preparation_maintenance(p);assert(p->observations.have_old);
    arc_riscpc_launch_record_close(&p->observations);assert(arc_io_remove(p->observations.temporary));
    p->launch_captured=true;p->observations.have_old=true;arc_engine_rpc_launch_media_changed();
    assert(!p->launch_captured && !p->observations.have_old);
    ArcRpcLaunchAttempt attempt=p->attempt;arc_engine_rpc_launch_interrupt(ARC_RPC_INTERRUPT_RESET);
    assert(p->attempt.delivery==ARC_RPC_DELIVERY_INTERRUPTED);p->attempt=attempt;
    ArcRpcLaunchBinding binding={&p->attempt,&p->controller,launch_revalidate,p,launch_media_changed,launch_aborted};
    assert(arc_engine_rpc_launch_bind(&binding));
    p->host=host;p->launch_key=key;p->observations=observations;p->launch_captured=captured;
    memcpy(p->launch_media_identity,media,32);
}
