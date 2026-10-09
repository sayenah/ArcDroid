/* RPC records never use Arculator's envelope or normalisation. GPL-2.0-only. */
#include "rpc_cmos_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(char *error, size_t n, const char *message)
{ if(error && n)snprintf(error,n,"RPC CMOS: %s",message);return false; }
static void put(uint8_t *p, uint64_t v, unsigned n)
{ for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint64_t get(const uint8_t *p,unsigned n)
{ uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(8*i);return v; }
unsigned arc_riscpc_cmos_layout(const char *s)
{ const char *names[]={"", "ro350", "ro360", "ro370"};for(unsigned i=1;s && i<4;++i)if(!strcmp(s,names[i]))return i;return 0; }
bool arc_riscpc_cmos_hex(const char *s,size_t n,char *out)
{
    if(!s || strlen(s)!=n)return false;
    for(size_t i=0;i<n;++i){char c=s[i];if(c>='A'&&c<='F')c+=(char)('a'-'A');if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;out[i]=c;}out[n]=0;return true;
}
static unsigned nibble(char c){return c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10);}
static void digest_bytes(const char *s,uint8_t *p)
{ for(unsigned i=0;i<32;++i)p[i]=(uint8_t)(nibble(s[2*i])*16+nibble(s[2*i+1])); }
bool arc_riscpc_cmos_checksum(const uint8_t p[240])
{ unsigned sum=1;for(unsigned i=0;i<240;++i)if(i!=47)sum+=p[i];return (uint8_t)sum==p[47]; }
/* Physical address masks, deliberately data rather than firmware recognisers. */
static const uint8_t required[3][32]={
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,243,255,255,255,255,255,255,255},
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}};
void arc_riscpc_cmos_required(unsigned layout,uint8_t mask[32])
{ if(layout>=1 && layout<=3)memcpy(mask,required[layout-1],32);else memset(mask,0,32); }
bool arc_riscpc_cmos_oracle(unsigned layout,const uint8_t p[240],const ArcRpcCmosEvidence *e)
{
    if(layout<1 || layout>3 || !e || !e->writes || e->last_write>ARC_RPC_CMOS_CAPTURE_NS-ARC_RPC_CMOS_STABILITY_NS || !arc_riscpc_cmos_checksum(p))return false;
    for(unsigned i=0;i<32;++i)if((e->mask[i]&required[layout-1][i])!=required[layout-1][i])return false;
    return true;
}
static void header(const ArcRpcCmosKey *k,uint8_t p[512],uint64_t gen)
{
    memset(p,0,512);memcpy(p,k->defaults?"ARCRPCD1":"ARCRPCM1",8);
    put(p+8,1,4);put(p+12,512,4);put(p+16,k->model+1,4);put(p+20,0x10000u|k->layout,4);put(p+24,gen,8);
    if(!k->defaults)memcpy(p+32,k->content,40);
    digest_bytes(k->firmware,p+72);
}
static void encode(ArcRpcCmosStore *s,const uint8_t payload[240],const ArcRpcCmosEvidence *e,uint8_t p[512])
{
    header(&s->key,p,s->generation+1);memcpy(p+104,payload,240);
    if(s->key.defaults){
        put(p+344,s->key.procedure,4);put(p+348,s->key.timing,4);put(p+352,e->rate,4);put(p+356,1,4);
        put(p+360,e->epoch,8);put(p+368,0,8);put(p+376,ARC_RPC_CMOS_DELETE_NS,8);
        put(p+384,ARC_RPC_CMOS_CAPTURE_NS,8);put(p+392,ARC_RPC_CMOS_STABILITY_NS,8);
        put(p+400,e->writes,8);put(p+408,e->last_write,8);memcpy(p+416,e->mask,32);
        char digest[65];arc_sha256_hex(payload,240,digest);digest_bytes(digest,p+448);
    }
    put(p+508,arc_crc32(p,508),4);
}
static bool decode(ArcRpcCmosStore *s,const uint8_t *p,size_t size,ArcRpcCmosEvidence *e)
{
    uint8_t expected[512];header(&s->key,expected,1);
    if(size!=512 || memcmp(p,expected,24) || memcmp(p+32,expected+32,72) || !get(p+24,8) || get(p+508,4)!=arc_crc32(p,508))return false;
    memset(e,0,sizeof(*e));
    if(s->key.defaults){
        if(get(p+344,4)!=s->key.procedure || get(p+348,4)!=s->key.timing || get(p+356,4)!=1 ||
           get(p+368,8)!=0 || get(p+376,8)!=ARC_RPC_CMOS_DELETE_NS || get(p+384,8)!=ARC_RPC_CMOS_CAPTURE_NS || get(p+392,8)!=ARC_RPC_CMOS_STABILITY_NS)return false;
        static const unsigned rates[]={20,35,100,35};
        e->rate=(unsigned)get(p+352,4);e->epoch=get(p+360,8);e->writes=get(p+400,8);e->last_write=get(p+408,8);memcpy(e->mask,p+416,32);
        /* Payload-only evidence cannot claim more distinct written addresses
         * than writes, or include RTC registers. Validate cached provenance. */
        unsigned covered=0;
        for(unsigned i=2;i<32;++i)for(unsigned bit=0;bit<8;++bit)covered+=(e->mask[i]>>bit)&1u;
        if(e->mask[0] || e->mask[1] || e->writes<covered)return false;
        char digest[65];uint8_t bytes[32];arc_sha256_hex(p+104,240,digest);digest_bytes(digest,bytes);
        if(e->rate!=rates[s->key.model] || e->epoch>UINT64_C(13569465599) || memcmp(p+448,bytes,32) || !arc_riscpc_cmos_oracle(s->key.layout,p+104,e))return false;
        for(unsigned i=480;i<508;++i)if(p[i])return false;
    }else for(unsigned i=344;i<508;++i)if(p[i])return false;
    return true;
}
static bool path(const ArcRpcCmosStore *s,const char *suffix,char out[1040])
{ int n=snprintf(out,1040,"%s%s",s->base,suffix);return n>=0 && n<1040; }
static bool exists(const char *directory,const char *leaf,bool *found)
{ return arc_io_directory_contains(directory,leaf,found); }
/* Only published slots participate in recovery/refusal. A candidate is not
 * a recovery root, even when complete; publication always overwrites it. */
static void inspect_slots(ArcRpcCmosStore *s,char *error,size_t n)
{
    char directory[1040];strcpy(directory,s->base);*strrchr(directory,'/')=0;
    for(unsigned slot=0;slot<2;++slot){
        char filename[1040];path(s,slot?".1":".0",filename);bool found=false;
        if(!exists(directory,strrchr(filename,'/')+1,&found)){s->refused=true;fail(error,n,"unable to inspect record slot");continue;}
        if(!found)continue;
        void *bytes=NULL;size_t size=0;ArcRpcCmosEvidence e;
        bool valid=arc_io_read_all_limit(filename,&bytes,&size,512,NULL) && decode(s,bytes,size,&e);
        if(valid && slot<2 && (get((uint8_t *)bytes+24,8)&1)!=slot)valid=false;
        if(!valid){s->refused=true;fail(error,n,"refused corrupt, foreign or wrong-key slot; preserved on disk");}
        else if( get((uint8_t *)bytes+24,8)>s->generation){s->generation=get((uint8_t *)bytes+24,8);s->present=true;memcpy(s->payload,(uint8_t *)bytes+104,240);s->evidence=e;}
        free(bytes);
    }
}
ArcRpcCmosResult arc_riscpc_cmos_store_load(ArcRpcCmosStore *s,const char *save,const ArcRpcCmosKey *k,char *error,size_t n)
{
    memset(s,0,sizeof(*s));s->key=*k;if(error&&n)*error=0;
    char content[41],firmware[65];
    if(!save || !*save || k->model>=4 || k->layout<1 || k->layout>3 || !arc_riscpc_cmos_hex(k->firmware,64,firmware) || (!k->defaults && !arc_riscpc_cmos_hex(k->content,40,content))){fail(error,n,"invalid key or save directory");return ARC_RPC_CMOS_REFUSED;}
    memcpy(s->key.firmware,firmware,65);if(!k->defaults)memcpy(s->key.content,content,41);
    char directory[1024];int length=snprintf(directory,sizeof(directory),"%s/arcdroid/cmos-riscpc%s",save,k->defaults?"/defaults":"");
    if(length<0 || length>=(int)sizeof(directory) || !arc_io_make_path(directory)){fail(error,n,"save directory unavailable");return ARC_RPC_CMOS_REFUSED;}
    length=k->defaults?snprintf(s->base,sizeof(s->base),"%s/%s-m%u-p%u-t%u",directory,firmware,k->model+1,k->procedure,k->timing):
        snprintf(s->base,sizeof(s->base),"%s/%s-m%u-%s",directory,content,k->model+1,firmware);
    if(length<0 || length>=(int)sizeof(s->base)){fail(error,n,"save path too long");return ARC_RPC_CMOS_REFUSED;}
    inspect_slots(s,error,n);
    return s->present?ARC_RPC_CMOS_FOUND:s->refused?ARC_RPC_CMOS_REFUSED:ARC_RPC_CMOS_MISSING;
}
bool arc_riscpc_cmos_store_put(ArcRpcCmosStore *s,const uint8_t payload[240],const ArcRpcCmosEvidence *e,char *error,size_t n)
{
    /* A failure after rename may have published a verified newer generation
     * without updating this live handle. Reconcile before the no-change check
     * or retry, preserving every invalid published slot as at ordinary load. */
    if(s->publication_pending && !s->refused){
        s->generation=0;s->present=false;
        inspect_slots(s,error,n);
        if(!s->refused)s->publication_pending=false;
    }
    if(s->present && !memcmp(s->payload,payload,240))return true;
    if(s->refused || s->generation==UINT64_MAX)return fail(error,n,"publication refused; existing refused slots are preserved");
    if(s->key.defaults && (!e || !arc_riscpc_cmos_oracle(s->key.layout,payload,e)))return fail(error,n,"default completion oracle failed");
    uint8_t bytes[512];encode(s,payload,e,bytes);char tmp[1040],final[1040];
    path(s,".tmp",tmp);path(s,(s->generation+1)&1?".1":".0",final);
    /* Revalidate the destination before retirement; an externally replaced
     * foreign/corrupt slot must never be silently replaced while live. */
    char directory[1040];strcpy(directory,final);*strrchr(directory,'/')=0;
    bool found=false;
    if(!exists(directory,strrchr(final,'/')+1,&found))return fail(error,n,"destination inspection failed");
    if(found){
        void *old=NULL;size_t old_size=0;ArcRpcCmosEvidence old_e;
        bool old_valid=arc_io_read_all_limit(final,&old,&old_size,512,NULL) && decode(s,old,old_size,&old_e) && get((uint8_t *)old+24,8)<s->generation && (((uint8_t *)old)[24]&1u)==((s->generation+1)&1u);
        free(old);if(!old_valid){s->refused=true;return fail(error,n,"destination changed or refused; preserved on disk");}
    }
    /* Empty, torn, foreign and wrong-key .tmp files are abandoned attempts;
     * they never latch refusal and the truncating write below replaces them. */
    s->publication_pending=true;
    const char *why=NULL;
    if(!arc_io_write_all_diagnostic(tmp,bytes,512,&why)){fail(error,n,why?why:"candidate write failed");return false;}
    void *read=NULL;size_t size=0;
    bool valid=arc_io_read_all_limit(tmp,&read,&size,512,NULL) && size==512 && !memcmp(read,bytes,512);free(read);
    if(!valid)return fail(error,n,"candidate readback verification failed");
    /* Only the older verified slot is removed. The newest remains recoverable
     * across remove/rename and on platforms whose rename cannot replace. */
    if(found && !arc_io_remove(final))return fail(error,n,"older-slot retirement failed");
    if(!arc_io_rename(tmp,final))return fail(error,n,"candidate rename failed");
    read=NULL;size=0;valid=arc_io_read_all_limit(final,&read,&size,512,NULL) && size==512 && !memcmp(read,bytes,512);free(read);
    if(!valid)return fail(error,n,"published-slot verification failed");
    ++s->generation;s->present=true;s->publication_pending=false;memcpy(s->payload,payload,240);if(e)s->evidence=*e;return true;
}
