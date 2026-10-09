/* ArcDroid-owned floppy mechanics/presentation. GPL-2.0-only.
 * 300 RPM and JFD descriptor positions use the accepted D-139 model. Raw
 * layouts retain validated geometry/prefix bounds. No host latency is charged. */
#include "rpc_floppy.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void arc_rpc_floppy_init(ArcRpcFloppy *f,uint64_t session)
{
    memset(f,0,sizeof(*f)); f->session_generation=session;
    for(unsigned i=0;i<2;++i){f->drives[i].changed=true;f->drives[i].mount_generation=1;}
}
void arc_rpc_floppy_cancel(ArcRpcFloppy *f) { ++f->command_generation; }
void arc_rpc_floppy_eject(ArcRpcFloppy *f,unsigned drive)
{
    if(drive>=2)return;
    ArcRpcFloppyDrive *d=&f->drives[drive];
    ++d->mount_generation; /* Before any release. */
    if(d->file)arc_io_close_file(d->file);
    free(d->raw); d->file=NULL;d->raw=NULL;d->jfd=NULL;d->present=false;
    d->writable=false;d->changed=true;d->loaded=false;
}
void arc_rpc_floppy_close(ArcRpcFloppy *f)
{ ++f->session_generation;arc_rpc_floppy_eject(f,0);arc_rpc_floppy_eject(f,1); }
bool arc_rpc_floppy_valid(const ArcRpcFloppy *f,unsigned drive,ArcRpcFloppyGeneration g)
{
    return drive<2 && g.session==f->session_generation &&
        g.mount==f->drives[drive].mount_generation && g.command==f->command_generation;
}
static bool geometry(ArcRpcFloppyDrive *d,ArcMediaGeometry g)
{
    d->sides=2;d->density=2;
    switch(g){
    case ARC_GEOMETRY_320K:d->sides=1;d->sectors=16;d->bytes=256;break;
    case ARC_GEOMETRY_640K_L:d->sectors=16;d->bytes=256;break;
    case ARC_GEOMETRY_800K_D:case ARC_GEOMETRY_800K_E:d->sectors=5;d->bytes=1024;break;
    case ARC_GEOMETRY_1600K_F:d->sectors=10;d->bytes=1024;d->density=4;break;
    default:return false;
    }return true;
}
bool arc_rpc_floppy_mount(ArcRpcFloppy *f,unsigned drive,const ArcEngineMedia *m,char *error,size_t size)
{
    if(drive>=2)return false;
    ArcRpcFloppyDrive candidate={0}; bool ok=m!=NULL;
    if(ok && m->kind==ARC_MEDIA_JFD){
        ok=drive==0 && m->jfd && m->jfd->decoded && m->jfd->track_count;
        candidate.jfd=m->jfd;
        candidate.writable=ok && m->writable && m->jfd->overlay && m->jfd->overlay->writable;
    }else if(ok){
        ok=(m->kind==ARC_MEDIA_ADF_320K || m->kind==ARC_MEDIA_ADF_800K ||
            m->kind==ARC_MEDIA_ADF_1600K || m->kind==ARC_MEDIA_ADL_640K) && geometry(&candidate,m->geometry);
        size_t capacity=(size_t)candidate.sides*candidate.sectors*candidate.bytes*80u;
        if(ok && m->data){
            ok=m->size && m->size<=capacity;
            if(ok){candidate.raw=malloc(m->size);ok=candidate.raw!=NULL;}
            if(ok){memcpy(candidate.raw,m->data,m->size);candidate.extent=m->size;}
        }else if(ok){
            candidate.file=m->path?arc_io_open_file(m->path,m->writable):NULL;
            candidate.writable=candidate.file && m->writable;
            if(!candidate.file && m->path)candidate.file=arc_io_open_file(m->path,false);
            int64_t length=candidate.file?arc_io_file_length(candidate.file):-1;
            ok=length>0 && (uint64_t)length<=capacity;
            if(ok)candidate.extent=(size_t)length;
        }
        if(ok && m->effective_extent){
            ok=m->effective_extent<=candidate.extent;
            candidate.extent=m->effective_extent;
        }
        /* Memory media is read-only (the shipping helper contract). */
    }
    /* Preparation precedes retirement. A failed insertion explicitly empties
     * the drive, rather than retaining an unannounced old image. */
    ArcRpcFloppyDrive *old=&f->drives[drive];
    unsigned cylinder=old->cylinder,head=old->head;bool motor=old->motor;
    uint64_t epoch=old->motor_epoch,phase=old->stopped_phase;
    arc_rpc_floppy_eject(f,drive);uint64_t generation=old->mount_generation;
    if(!ok){if(candidate.file)arc_io_close_file(candidate.file);free(candidate.raw);
        if(error&&size)snprintf(error,size,"Invalid or unreadable RPC floppy");
        return false;}
    candidate.present=true;candidate.changed=true;candidate.cylinder=cylinder;candidate.head=head;
    candidate.motor=motor;candidate.motor_epoch=epoch;candidate.stopped_phase=phase;
    candidate.mount_generation=generation;*old=candidate;
    if(error&&size)error[0]=0;
    return true;
}
uint64_t arc_rpc_floppy_phase(const ArcRpcFloppy *f,unsigned drive,uint64_t now)
{
    if(drive>=2)return 0;
    const ArcRpcFloppyDrive *d=&f->drives[drive];
    return d->motor?(now-d->motor_epoch+d->stopped_phase)%ARC_RPC_FLOPPY_REV_NS:d->stopped_phase;
}
void arc_rpc_floppy_motor(ArcRpcFloppy *f,unsigned drive,bool on,uint64_t now)
{
    if(drive>=2)return;
    ArcRpcFloppyDrive *d=&f->drives[drive];
    if(d->motor==on)return;
    if(on)d->motor_epoch=now;else d->stopped_phase=arc_rpc_floppy_phase(f,drive,now);
    d->motor=on;
}
void arc_rpc_floppy_step(ArcRpcFloppy *f,unsigned drive,int delta)
{
    if(drive>=2 || !delta)return;
    ArcRpcFloppyDrive *d=&f->drives[drive];
    int64_t target=(int64_t)d->cylinder+delta;
    d->cylinder=target<0?0u:(unsigned)target;
    if(d->present)d->changed=false; /* An actual STEP, including at TRK0. */
    d->loaded=false;
}
ArcRpcFloppySignals arc_rpc_floppy_signals(const ArcRpcFloppy *f,unsigned drive,uint64_t now)
{
    if(drive>=2)return (ArcRpcFloppySignals){false,true,false,false};
    const ArcRpcFloppyDrive *d=&f->drives[drive];
    return (ArcRpcFloppySignals){d->cylinder==0,d->changed||!d->present,
        d->present&&d->motor&&arc_rpc_floppy_phase(f,drive,now)<UINT64_C(2000000),
        d->present&&!d->writable};
}
static void refused(const ArcRpcFloppyDrive *d,const ArcRpcFloppyRequest *r,const char *why)
{
    const ArcJfdOverlay *o=d->jfd?d->jfd->overlay:NULL;
    if(o&&o->log_refusal)o->log_refusal(o->image_number,r->cylinder,r->head,r->r,why);
}
static unsigned density(const ArcRpcFloppyRequest *r)
{ return r->encoding==ARC_RPC_FLOPPY_FM?1:r->rate_kbps==500?4:r->rate_kbps==1000?8:2; }
static uint8_t size_code(unsigned bytes)
{ unsigned n=0;while(n<7 && (128u<<n)<bytes)++n;return (uint8_t)n; }
ArcRpcFloppyResult arc_rpc_floppy_select(ArcRpcFloppy *f,const ArcRpcFloppyRequest *r,
    uint64_t now,ArcRpcFloppySector *out)
{
    memset(out,0,sizeof(*out));out->request=*r;
    if(r->drive>=2)return ARC_RPC_FLOPPY_NO_MEDIA;
    ArcRpcFloppyDrive *d=&f->drives[r->drive];
    out->generation=(ArcRpcFloppyGeneration){f->session_generation,d->mount_generation,f->command_generation};
    if(!d->present)return ARC_RPC_FLOPPY_NO_MEDIA;
    bool write=r->operation==ARC_RPC_FLOPPY_WRITE,format=r->operation==ARC_RPC_FLOPPY_FORMAT;
    if((write||format)&&(!d->writable || (d->jfd&&(format||r->deleted)))){
        refused(d,r,format?"format":r->deleted?"deleted-data":"write-protected");
        return ARC_RPC_FLOPPY_WRITE_PROTECT;
    }
    if(!d->motor)return ARC_RPC_FLOPPY_NOT_FOUND;
    if(r->head>1 || r->cylinder!=d->cylinder)return ARC_RPC_FLOPPY_NOT_FOUND;
    d->head=r->head;unsigned den=density(r);
    uint64_t phase=arc_rpc_floppy_phase(f,r->drive,now),best=UINT64_MAX;
    bool any=r->operation==ARC_RPC_FLOPPY_READ_ID || r->operation==ARC_RPC_FLOPPY_READ_TRACK;
    if(d->jfd){
        unsigned t=r->cylinder*2+r->head;
        if(t>=d->jfd->track_count)return ARC_RPC_FLOPPY_NOT_FOUND;
        const ArcJfdTrack *track=&d->jfd->tracks[t];
        for(unsigned i=0;i<track->sector_count+ARC_JFD_MAX_STANDARD_IDS;++i){
            ArcJfdView v;
            if(!arc_jfd_effective_sector(d->jfd,t,i,write,&v) || v.sector.density!=den ||
                (!any&&v.sector.id!=r->r))continue;
            if(write&&v.created&&!v.data && d->jfd->overlay && d->jfd->overlay->map_valid &&
                (d->jfd->overlay->probe_mask[t]&(1u<<v.sector.id)))continue;
            uint64_t pos=(uint64_t)v.sector.time_us*1000;
            uint64_t delta=(pos+ARC_RPC_FLOPPY_REV_NS-phase)%ARC_RPC_FLOPPY_REV_NS;
            if(delta<best){best=delta;out->view=v;}
        }
        if(best==UINT64_MAX)return ARC_RPC_FLOPPY_NOT_FOUND;
        out->jfd=true;out->bytes=out->view.sector.size;out->c=(uint8_t)r->cylinder;
        out->h=(uint8_t)r->head;out->r=out->view.sector.id;out->n=size_code(out->bytes);
        out->delay_ns=best;
        if((out->view.sector.flags&ARC_JFD_ID_CRC_BAD) && r->operation!=ARC_RPC_FLOPPY_READ_TRACK)return ARC_RPC_FLOPPY_ID_CRC;
        if(!any && r->c!=out->c)return ARC_RPC_FLOPPY_WRONG_CYLINDER;
        if(!any && r->h!=out->h)return ARC_RPC_FLOPPY_NOT_FOUND;
        if(!any && r->n!=out->n){refused(d,r,"size-mismatch");return write?ARC_RPC_FLOPPY_WRITE_PROTECT:ARC_RPC_FLOPPY_NOT_FOUND;}
        if(write && ((!out->view.created && !(d->jfd->overlay->eligible?
             d->jfd->overlay->eligible[out->view.key]:arc_jfd_sector_write_eligible(d->jfd,out->view.key))) ||
             (out->view.created&&!out->view.write_eligible) ||
             (r->n==0 && r->dtl && r->dtl<out->bytes))){
            refused(d,r,"sector-ineligible");return ARC_RPC_FLOPPY_WRITE_PROTECT;
        }
        if(!write && r->operation!=ARC_RPC_FLOPPY_READ_ID && !out->view.data)return ARC_RPC_FLOPPY_NOT_FOUND;
        if(out->bytes>sizeof(out->data))return ARC_RPC_FLOPPY_NOT_FOUND;
        if(out->view.data)memcpy(out->data,out->view.data,out->bytes);
    }else{
        if(r->head>=d->sides || den!=d->density)return ARC_RPC_FLOPPY_NOT_FOUND;
        if(format){
            uint64_t format_ns=(146u+(uint64_t)r->sc*(62u+d->bytes+r->gpl))*UINT64_C(8000000)/r->rate_kbps;
            if(r->n!=size_code(d->bytes)||r->sc!=d->sectors || format_ns>ARC_RPC_FLOPPY_REV_NS)return ARC_RPC_FLOPPY_WRITE_PROTECT;
            out->bytes=d->bytes;out->delay_ns=(ARC_RPC_FLOPPY_REV_NS-phase)%ARC_RPC_FLOPPY_REV_NS;return ARC_RPC_FLOPPY_OK;}
        unsigned id=r->r;
        if(any){
            for(unsigned i=0;i<d->sectors;++i){
                size_t offset=((size_t)r->cylinder*d->sides+r->head)*d->sectors*d->bytes+(size_t)i*d->bytes;
                if(offset>d->extent || d->bytes>d->extent-offset)continue;
                uint64_t pos=(uint64_t)i*ARC_RPC_FLOPPY_REV_NS/d->sectors;
                uint64_t delta=(pos+ARC_RPC_FLOPPY_REV_NS-phase)%ARC_RPC_FLOPPY_REV_NS;
                if(delta<best){best=delta;id=i;}
            }
        }
        if(id>=d->sectors || (any&&best==UINT64_MAX))return ARC_RPC_FLOPPY_NOT_FOUND;
        out->raw_offset=((size_t)r->cylinder*d->sides+r->head)*d->sectors*d->bytes+(size_t)id*d->bytes;
        if(out->raw_offset>d->extent || d->bytes>d->extent-out->raw_offset)return ARC_RPC_FLOPPY_NOT_FOUND;
        out->c=(uint8_t)r->cylinder;out->h=(uint8_t)r->head;out->r=(uint8_t)id;out->bytes=d->bytes;out->n=size_code(d->bytes);
        if(!any)best=((uint64_t)id*ARC_RPC_FLOPPY_REV_NS/d->sectors+ARC_RPC_FLOPPY_REV_NS-phase)%ARC_RPC_FLOPPY_REV_NS;
        out->delay_ns=best;
        if(!any && r->c!=out->c)return ARC_RPC_FLOPPY_WRONG_CYLINDER;
        if(!any && r->h!=out->h)return ARC_RPC_FLOPPY_NOT_FOUND;
        if(!any && r->n!=out->n)return ARC_RPC_FLOPPY_NOT_FOUND;
        if(write&&r->deleted)return ARC_RPC_FLOPPY_WRITE_PROTECT; /* Raw has no DAM storage. */
        if(d->raw)memcpy(out->data,d->raw+out->raw_offset,d->bytes);
        else if(arc_io_seek_file(d->file,(int64_t)out->raw_offset,RETRO_VFS_SEEK_POSITION_START)<0 ||
            arc_io_read_file(d->file,out->data,d->bytes)!=d->bytes)return ARC_RPC_FLOPPY_NOT_FOUND;
    }
    out->delay_ns=best;return out->jfd&&(out->view.sector.flags&ARC_JFD_ID_CRC_BAD)?ARC_RPC_FLOPPY_ID_CRC:ARC_RPC_FLOPPY_OK;
}
ArcRpcFloppyResult arc_rpc_floppy_commit(ArcRpcFloppy *f,ArcRpcFloppySector *s,
    const uint8_t *bytes,unsigned consumed,bool tc)
{
    unsigned drive=s->request.drive;
    if(!arc_rpc_floppy_valid(f,drive,s->generation))return ARC_RPC_FLOPPY_CANCELLED;
    ArcRpcFloppyDrive *d=&f->drives[drive];
    if(s->committed || consumed>s->bytes)return ARC_RPC_FLOPPY_CANCELLED;
    /* D-139: TC at the final byte is a complete commit. Incomplete JFD never
     * reaches the store; raw TC writes a complete zero-padded sector (p.65). */
    if(s->jfd && consumed!=s->bytes)return ARC_RPC_FLOPPY_OK;
    if(consumed!=s->bytes && !tc)return ARC_RPC_FLOPPY_CANCELLED;
    if(!d->writable)return ARC_RPC_FLOPPY_WRITE_PROTECT;
    uint8_t pending[ARC_RPC_FLOPPY_MAX_SECTOR]={0};memcpy(pending,bytes,consumed);
    if(s->jfd){
        ArcJfdOverlay *o=d->jfd->overlay;
        const char *veto=s->view.created?NULL:arc_jfd_write_veto_reason(d->jfd,s->view.key,pending);
        if(veto){refused(d,&s->request,veto);return ARC_RPC_FLOPPY_DATA_CRC;}
        bool ok=s->view.created?o->commit_created&&o->commit_created((ArcJfdImage *)(uintptr_t)d->jfd,
            s->request.cylinder*2+s->request.head,(unsigned)s->view.key,pending):
            o->commit&&o->commit((ArcJfdImage *)(uintptr_t)d->jfd,s->view.key,pending);
        if(!ok){d->writable=false;refused(d,&s->request,"store-failure");return ARC_RPC_FLOPPY_WRITE_PROTECT;}
    }else if(!d->file || arc_io_seek_file(d->file,(int64_t)s->raw_offset,RETRO_VFS_SEEK_POSITION_START)<0 ||
        arc_io_write_file(d->file,pending,s->bytes)!=s->bytes || !arc_io_flush_file(d->file)){
        d->writable=false;return ARC_RPC_FLOPPY_WRITE_PROTECT;
    }
    s->committed=true;return ARC_RPC_FLOPPY_OK;
}
ArcRpcFloppyResult arc_rpc_floppy_format(ArcRpcFloppy *f,ArcRpcFloppySector *s,
    const uint8_t *ids,unsigned count,uint8_t fill)
{
    unsigned drive=s->request.drive;
    if(!arc_rpc_floppy_valid(f,drive,s->generation))return ARC_RPC_FLOPPY_CANCELLED;
    ArcRpcFloppyDrive *d=&f->drives[drive];
    if(d->jfd||!d->writable||count!=d->sectors)return ARC_RPC_FLOPPY_WRITE_PROTECT;
    uint8_t n=size_code(d->bytes);
    /* Raw records have fixed rotational IDs. Never normalize supplied tuples. */
    for(unsigned i=0;i<count;++i)
        if(ids[i*4]!=(uint8_t)d->cylinder || ids[i*4+1]!=s->request.head || ids[i*4+2]!=i || ids[i*4+3]!=n)
            return ARC_RPC_FLOPPY_WRITE_PROTECT;
    size_t offset=((size_t)d->cylinder*d->sides+s->request.head)*d->sectors*d->bytes;
    size_t length=(size_t)d->sectors*d->bytes;
    if(offset>d->extent || length>d->extent-offset)return ARC_RPC_FLOPPY_NOT_FOUND;
    uint8_t buffer[20*1024];memset(buffer,fill,length);
    if(!d->file || arc_io_seek_file(d->file,(int64_t)offset,RETRO_VFS_SEEK_POSITION_START)<0 ||
        arc_io_write_file(d->file,buffer,length)!=length || !arc_io_flush_file(d->file)){
        d->writable=false;return ARC_RPC_FLOPPY_WRITE_PROTECT;}
    return ARC_RPC_FLOPPY_OK;
}
void arc_rpc_floppy_missing(ArcRpcFloppy *f,const ArcRpcFloppyRequest *r)
{
    if(r->drive>=2)return;
    ArcRpcFloppyDrive *d=&f->drives[r->drive];
    ArcJfdOverlay *o=d->jfd?d->jfd->overlay:NULL;
    unsigned t=r->cylinder*2+r->head;
    if(!o || !o->map_enabled || !o->map_valid || r->operation==ARC_RPC_FLOPPY_WRITE ||
       r->c!=d->cylinder || r->h!=r->head || r->encoding!=ARC_RPC_FLOPPY_MFM ||
       r->rate_kbps!=250 || t>=ARC_JFD_PDD5_TRACKS || r->r>=ARC_JFD_PDD5_IDS ||
       !(o->creation_mask[t]&(1u<<r->r)) || o->created[t][r->r])return;
    ArcJfdView v;
    if(!arc_jfd_effective_sector(d->jfd,t,d->jfd->tracks[t].sector_count+r->r,false,&v)||!v.data)
        o->probe_mask[t]|=(uint8_t)(1u<<r->r);
}

ArcRpcFloppyEvent arc_rpc_floppy_event(const ArcRpcFloppy *f,
    const ArcRpcFloppySector *s,ArcRpcFloppyEventKind kind,
    unsigned position,ArcRpcFloppyResult result)
{
    ArcRpcFloppyEvent e={s->generation,kind,result,s->request.drive,
        s->c,s->h,s->r,s->n,0};
    if(!arc_rpc_floppy_valid(f,e.drive,e.generation))e.result=ARC_RPC_FLOPPY_CANCELLED;
    else if(kind==ARC_RPC_FLOPPY_EVENT_READ_BYTE){
        if(position>=s->bytes)e.result=ARC_RPC_FLOPPY_NOT_FOUND;
        else e.byte=s->data[position];
    }
    return e;
}
