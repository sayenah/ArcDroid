/* Fault cuts over actual publication callbacks. GPL-2.0-only. */
#include "rpc_store_owner.h"
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
    assert(!arc_riscpc_launch_record_begin(&s,NULL,id,rpc_store_test_owner()));
    assert(!arc_riscpc_launch_record_begin(&s,"/save",NULL,rpc_store_test_owner()));
    assert(!arc_riscpc_launch_record_begin(&s,"content://unsupported",id,rpc_store_test_owner()));
    char content[41];memset(content,'a',40);content[40]=0;
    assert(arc_riscpc_launch_record_begin(&s,"/save",content,rpc_store_test_owner()));
    const char invalid[]={'/','G','g',':','`','z'};
    for(unsigned at=0;at<40;++at)for(unsigned i=0;i<sizeof(invalid);++i){
        content[at]=invalid[i];assert(!arc_riscpc_launch_record_begin(&s,"/save",content,rpc_store_test_owner()));content[at]='a';
    }
    arc_riscpc_launch_record_close(NULL);memset(&s,0,sizeof(s));arc_riscpc_launch_record_close(&s);
    assert(!arc_riscpc_launch_record_namespace(NULL));
    for(unsigned phase=0;phase<=17;++phase){s.phase=phase;assert(arc_riscpc_launch_record_namespace(&s)==(phase==5||phase==7||phase==15||phase==16));}
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));fail_at=steps+1;
    bool missing_done=false;assert(arc_riscpc_launch_record_recover(&s,&missing_done));
    assert(missing_done && s.ready && s.phase==6 && !s.have_old);reset_fault();
    assert(!arc_riscpc_launch_record_begin(NULL,"/save",id,rpc_store_test_owner()));assert(!arc_riscpc_launch_record_begin(&s,"/save","bad",rpc_store_test_owner()));
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(!s.have_old && s.ready);
    assert(!arc_riscpc_launch_record_publish_begin(&s,NULL));assert(arc_riscpc_launch_record_publish_begin(&s,&r));
    bool done=false;while(!done)assert(arc_riscpc_launch_record_publish_step(&s,true,&done));
    assert(s.published && s.phase==6 && find(s.path,false)>=0);test_vfs_no_open_handles();snapshot();
    /* Reopen steals the published candidate before any store recovery. */
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(s.have_old && find(s.path,false)<0);
    assert(arc_riscpc_boot_record_matches(&s.old,&r.key));arc_riscpc_launch_record_discard(&s);assert(!s.have_old);
    restore();int bad=find(s.path,false);assert(bad>=0);--files[bad].size;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(!s.have_old);
    restore();bad=find(s.path,false);files[bad].data=realloc(files[bad].data,289);assert(files[bad].data);files[bad].data[288]=0;files[bad].size=files[bad].capacity=289;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(!s.have_old);
    restore();bad=find(s.path,false);files[bad].data[0]^=1;
    assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(!s.have_old);
    /* Every interrupted publication leaves no accepted record; a temporary
     * sibling alone is never a candidate. Every revalidation cut suppresses
     * rename, including after a complete flushed temporary file. */
    for(unsigned cut=0;cut<5;++cut){
        restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));
        for(unsigned i=0;i<cut;++i){done=false;assert(arc_riscpc_launch_record_publish_step(&s,true,&done));assert(!done);}
        arc_riscpc_launch_record_close(&s);
        assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);assert(!s.have_old && find(s.temporary,false)<0);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));
        for(unsigned i=0;i<cut;++i){done=false;assert(arc_riscpc_launch_record_publish_step(&s,true,&done));}
        done=false;assert(arc_riscpc_launch_record_publish_step(&s,false,&done));assert(done && s.phase==6 && !s.have_old && find(s.path,false)<0);
        test_vfs_no_open_handles();
    }
    for(unsigned cut=1;cut<=5;++cut){
        restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));recover(&s);
        assert(arc_riscpc_launch_record_publish_begin(&s,&r));fail_at=steps+cut;
        bool ok=true;done=false;while(ok && !done){ok=arc_riscpc_launch_record_publish_step(&s,true,&done);if(s.failed)assert(!ok);}
        assert(!ok && s.failed && !s.published && find(s.path,false)<0);reset_fault();
        arc_riscpc_launch_record_close(&s);test_vfs_no_open_handles();
    }
    /* A failed removal prevents the owner from opening/recovering its HD. */
    restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));
    done=false;while(s.phase!=5)assert(arc_riscpc_launch_record_recover(&s,&done));
    fail_at=steps+1;assert(!arc_riscpc_launch_record_recover(&s,&done));assert(s.failed && !s.ready && find(s.path,false)>=0);
    reset_fault();
    for(unsigned temporary=0;temporary<2;++temporary)for(unsigned cut=1;cut<40;++cut){
        restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));
        if(temporary)assert(find(s.temporary,true)>=0);
        assert(test_mkdir(s.directory)==0);fail_at=steps+cut;done=false;
        for(unsigned tries=0;!done && !s.failed;++tries){assert(tries<100);
            bool ok=arc_riscpc_launch_record_recover(&s,&done);if(s.failed)assert(!ok);
        }
        reset_fault();arc_riscpc_launch_record_close(&s);test_vfs_no_open_handles();
    }
    restore();assert(arc_riscpc_launch_record_begin(&s,"/save",id,rpc_store_test_owner()));assert(test_mkdir(s.directory)==0);
    fail_at=steps+1;done=false;assert(!arc_riscpc_launch_record_recover(&s,&done) && s.failed && !done);
    reset_fault();arc_riscpc_launch_record_close(&s);
    memset(&s,0,sizeof(s));s.phase=14;done=false;
    assert(!arc_riscpc_launch_record_publish_step(&s,true,&done) && s.failed && !done);
    fresh_files();puts("RPC records: steal-before-recovery, bounded publication, all interruption/revalidation cuts and failed invalidation passed");return 0;
}
