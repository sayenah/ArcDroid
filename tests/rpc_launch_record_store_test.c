/* Fault cuts over actual publication callbacks. GPL-2.0-only. */
#include "rpc_launch_record_store.h"
#include "rpc_hd_vfs.h"
static const char *id="0123456789012345678901234567890123456789";
static void recover(ArcRpcLaunchRecordStore *s)
{bool done=false;for(unsigned i=0;!done;++i){assert(i<4096);assert(arc_riscpc_launch_record_recover(s,&done));}}
static ArcRpcBootRecord record(void)
{
    ArcRpcBootRecord r={.observation=ARC_RPC_OUTCOME_RETURNED};r.key.cpu=1;r.key.plan_revision=2;r.key.profile_revision=2;r.key.action=1;
    memset(r.key.firmware,1,32);memset(r.key.configuration,2,32);memset(r.key.content,3,32);memset(r.key.media,4,32);
    memset(r.key.store,5,32);memset(r.key.plan,6,32);memset(r.key.helper,7,32);return r;
}
int main(void)
{
    fresh_files();ArcRpcLaunchRecordStore s;ArcRpcBootRecord r=record();
    assert(!arc_riscpc_launch_record_begin(NULL,"/save",id));assert(!arc_riscpc_launch_record_begin(&s,"/save","bad"));
    assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(!s.have_old && s.ready);
    assert(!arc_riscpc_launch_record_publish_begin(&s,NULL));assert(arc_riscpc_launch_record_publish_begin(&s,&r));
    bool done=false;while(!done)assert(arc_riscpc_launch_record_publish_step(&s,true,&done));
    assert(s.published && find(s.path,false)>=0);test_vfs_no_open_handles();snapshot();
    /* Reopen steals the published candidate before any store recovery. */
    assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(s.have_old && find(s.path,false)<0);
    assert(arc_riscpc_boot_record_matches(&s.old,&r.key));arc_riscpc_launch_record_discard(&s);assert(!s.have_old);
    restore();int bad=find(s.path,false);assert(bad>=0);--files[bad].size;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(!s.have_old);
    restore();bad=find(s.path,false);files[bad].data=realloc(files[bad].data,289);assert(files[bad].data);files[bad].data[288]=0;files[bad].size=files[bad].capacity=289;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(!s.have_old);
    restore();bad=find(s.path,false);files[bad].data[0]^=1;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(!s.have_old);
    /* Every interrupted publication leaves no accepted record; a temporary
     * sibling alone is never a candidate. Every revalidation cut suppresses
     * rename, including after a complete flushed temporary file. */
    for(unsigned cut=0;cut<5;++cut){
        restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));
        for(unsigned i=0;i<cut;++i){done=false;assert(arc_riscpc_launch_record_publish_step(&s,true,&done));assert(!done);}
        arc_riscpc_launch_record_close(&s);
        assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);assert(!s.have_old && find(s.temporary,false)<0);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));
        for(unsigned i=0;i<cut;++i){done=false;assert(arc_riscpc_launch_record_publish_step(&s,true,&done));}
        done=false;assert(arc_riscpc_launch_record_publish_step(&s,false,&done));assert(done && !s.have_old && find(s.path,false)<0);
        test_vfs_no_open_handles();
    }
    for(unsigned cut=1;cut<=5;++cut){
        restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id));recover(&s);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));fail_at=steps+cut;
        bool ok=true;done=false;while(ok && !done)ok=arc_riscpc_launch_record_publish_step(&s,true,&done);
        assert(!ok && s.failed && !s.published && find(s.path,false)<0);reset_fault();
        arc_riscpc_launch_record_close(&s);test_vfs_no_open_handles();
    }
    /* A failed removal prevents the owner from opening/recovering its HD. */
    restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id));
    done=false;while(s.phase!=5)assert(arc_riscpc_launch_record_recover(&s,&done));
    fail_at=steps+1;assert(!arc_riscpc_launch_record_recover(&s,&done));assert(s.failed && !s.ready && find(s.path,false)>=0);
    reset_fault();fresh_files();puts("RPC records: steal-before-recovery, bounded publication, all interruption/revalidation cuts and failed invalidation passed");return 0;
}
