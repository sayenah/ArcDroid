/* Project-authored ARM assembler; no firmware-derived instructions.
 * SWI contracts: RISC OS PRM, Program Environment and Window Manager.
 * GPL-2.0-only. */
#include "rpc_launch_controller.h"
#include <stdio.h>
#include <string.h>
enum { L_PLAN,L_NAME,L_LATCH,L_VALUE,L_COMMAND,L_STATUS,L_NOTIFY,L_REJECT,
    L_CLOSE,L_RETURN,L_FATAL,L_DUPLICATE,L_REGISTER,L_DESKTOP,L_RESULT,L_RAM,L_COUNT };
typedef struct { uint32_t words[2048], labels[L_COUNT]; unsigned n,count;
    struct {unsigned at,label,reg,condition;bool adr,link;} fix[128]; bool bad; } Arm;
static void word(Arm *a,uint32_t w) { if(a->n<2048)a->words[a->n++]=w;else a->bad=true; }
static void label(Arm *a,unsigned l) {a->labels[l]=a->n*4;}
static void branch(Arm *a,unsigned l,unsigned condition,bool link)
{if(a->count==128){a->bad=true;return;}a->fix[a->count++]=(typeof(a->fix[0])){a->n,l,0,condition,false,link};word(a,0);}
static void adr(Arm *a,unsigned r,unsigned l)
{if(a->count==128){a->bad=true;return;}a->fix[a->count++]=(typeof(a->fix[0])){a->n,l,r,14,true,false};word(a,0);word(a,0);}
static void literal(Arm *a,unsigned r,uint32_t v)
{word(a,0xe59f0000u|(r<<12));word(a,0xea000000);word(a,v);}
static void bytes(Arm *a,const void *data,size_t n)
{const uint8_t *p=data;for(size_t i=0;i<n;i+=4){uint32_t v=0;for(unsigned j=0;j<4&&i+j<n;++j)v|=(uint32_t)p[i+j]<<(8*j);word(a,v);}}
static bool immediate(uint32_t v,uint32_t *out)
{for(unsigned r=0;r<16;++r){unsigned shift=r*2;uint32_t x=shift?(v<<shift)|(v>>(32-shift)):v;
    if(x<256){*out=(r<<8)|x;return true;}}return false;}
static void notify(Arm *a,unsigned event)
{word(a,0xe3a00000|event);branch(a,L_NOTIFY,14,true);}
static void put(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
bool arc_riscpc_controller_build(const ArcRpcLaunchPlan *p,const ArcRpcAttemptToken *t,ArcRpcLaunchController *out)
{
    uint8_t plan[128],digest[32];size_t n=0;
    if(!out || !t || !t->session || !t->epoch || !t->serial || t->serial-1>UINT64_MAX-t->epoch ||
        arc_riscpc_plan_encode(p,plan,sizeof(plan),&n)!=ARC_RPC_PLAN_OK ||
        !arc_riscpc_plan_digest(p,digest) || memcmp(digest,t->plan,32))return false;
    memset(out,0,sizeof(*out));if(arc_riscpc_plan_command(p,out->text,sizeof(out->text))!=ARC_RPC_PLAN_OK)return false;
    Arm a={.n=16};word(&a,0xe92d5ff0); /* saved r4-r12,lr */
    word(&a,0xe1a0a002);word(&a,0xe3a08000);word(&a,0xe3a09003); /* ack, exact task, summary */
    adr(&a,7,L_PLAN);
    /* Compare the complete canonical plan, including target bytes and bounds,
     * before reading it as action data or making any effect. */
    for(size_t at=0;at<n;at+=4){size_t remain=n-at;uint32_t v=0;
        for(unsigned j=0;j<4&&j<remain;++j)v|=(uint32_t)plan[at+j]<<(8*j);
        if(remain>=4)word(&a,0xe5970000|(uint32_t)at);
        else {word(&a,0xe3a00000);for(unsigned j=0;j<remain;++j){word(&a,0xe5d71000|(uint32_t)(at+j));word(&a,0xe1800001|(j*8u<<7));}}
        literal(&a,1,v);word(&a,0xe1500001);branch(&a,L_REJECT,1,false);
    }
    adr(&a,0,L_LATCH);word(&a,0xe3e02000);word(&a,0xe3a03000);word(&a,0xe3a04000);
    word(&a,0xef020023); /* existence query: R2 zero iff absent; V ignored by documented contract */
    word(&a,0xe3520000);branch(&a,L_DUPLICATE,1,false);
    word(&a,0xe3a01000);notify(&a,ARC_RPC_STATUS_CONSUME);
    adr(&a,0,L_LATCH);adr(&a,1,L_VALUE);word(&a,0xe3a02001);word(&a,0xe3a03000);word(&a,0xe3a04000);
    word(&a,0xef020024);branch(&a,L_REJECT,6,false);
    if(p->flags&ARC_RPC_PLAN_RAMFS){
        word(&a,0xe3a00005);word(&a,0xef02005c);branch(&a,L_REJECT,6,false);
        word(&a,0xe3510000);branch(&a,L_REGISTER,1,false);
        word(&a,0xe3a00005);literal(&a,1,32768);word(&a,0xef02002a);branch(&a,L_REJECT,6,false);
    }label(&a,L_REGISTER);
    word(&a,0xe3a000c8);literal(&a,1,0x4b534154);adr(&a,2,L_NAME);word(&a,0xe3a03000);
    word(&a,0xef0600c0);branch(&a,L_REJECT,6,false);word(&a,0xe1a08001);
    word(&a,0xe1a01008);notify(&a,ARC_RPC_STATUS_REGISTERED);
    word(&a,0xe3a01000);notify(&a,ARC_RPC_STATUS_DISPATCH);
    adr(&a,0,L_COMMAND);out->start=a.n*4;word(&a,0xef0600de);branch(&a,L_REJECT,6,false);
    word(&a,0xe1a0b000);word(&a,0xe1a0100b);notify(&a,ARC_RPC_STATUS_START_RETURNED);
    word(&a,0xe35b0000);branch(&a,L_DESKTOP,1,false);
    word(&a,0xe3a09002);word(&a,0xe3a01000);notify(&a,ARC_RPC_STATUS_RETURNED);branch(&a,L_CLOSE,14,false);
    label(&a,L_DESKTOP);word(&a,0xe3a09006);word(&a,0xe1a0100b);notify(&a,ARC_RPC_STATUS_DESKTOP);branch(&a,L_CLOSE,14,false);
    label(&a,L_REJECT);word(&a,0xe3a09003);word(&a,0xe3a01000);notify(&a,ARC_RPC_STATUS_REJECTED);
    label(&a,L_CLOSE);word(&a,0xe3580000);branch(&a,L_RETURN,0,false);
    word(&a,0xe1a00008);literal(&a,1,0x4b534154);word(&a,0xef0600dd);branch(&a,L_FATAL,6,false);
    word(&a,0xe3a08000);word(&a,0xe3a01000);notify(&a,ARC_RPC_STATUS_CLOSED);branch(&a,L_RETURN,14,false);
    label(&a,L_DUPLICATE);word(&a,0xe3a09001);
    label(&a,L_RETURN);word(&a,0xe58a900c);word(&a,0xe8bd5ff0);word(&a,0xe10f3000);word(&a,0xe3130010);
    word(&a,0x01b0f00e);word(&a,0x11a0f00e);
    label(&a,L_FATAL);word(&a,0xef000011);branch(&a,L_FATAL,14,false);
    label(&a,L_NOTIFY);word(&a,0xe92d40ff);adr(&a,3,L_STATUS);word(&a,0xe5830020);word(&a,0xe5831028);
    word(&a,0xe5930024);word(&a,0xe2800001);word(&a,0xe5830024);
    word(&a,0xe3a00000);word(&a,0xe3a01001);word(&a,0xe3a02000);
    literal(&a,4,0x53435241);word(&a,0xe3a05040);word(&a,0xe3a06001);
    out->query=a.n*4;word(&a,0xef020006);word(&a,0xe8bd40ff);word(&a,0xe10f3000);word(&a,0xe3130010);
    word(&a,0x01b0f00e);word(&a,0x11a0f00e);
    label(&a,L_NAME);bytes(&a,"ArcLaunch",10);
    char latch[64];int length=snprintf(latch,sizeof(latch),"ArcLaunch$%016llX%016llX%016llX",(unsigned long long)t->session,(unsigned long long)t->epoch,(unsigned long long)t->serial);
    label(&a,L_LATCH);out->latch=a.n*4;bytes(&a,latch,(size_t)length+1);
    label(&a,L_VALUE);bytes(&a,"1",2);
    label(&a,L_COMMAND);out->command=a.n*4;bytes(&a,out->text,strlen(out->text)+1);
    label(&a,L_STATUS);out->status=a.n*4;
    uint64_t epoch=t->epoch+t->serial-1;
    const uint32_t status[]={0x53435241,1,64,1,(uint32_t)t->session,(uint32_t)(t->session>>32),
        (uint32_t)epoch,(uint32_t)(epoch>>32),0,0,0,0,0,0,0,0};
    for(unsigned i=0;i<16;++i)word(&a,status[i]);
    label(&a,L_PLAN);out->plan=a.n*4;bytes(&a,plan,n);
    if(a.bad)return false;
    for(unsigned i=0;i<a.count;++i){unsigned at=a.fix[i].at;int32_t delta=(int32_t)a.labels[a.fix[i].label]-(int32_t)(at*4+8);
        if(a.fix[i].adr){uint32_t imm,d=(uint32_t)(delta<0?-delta:delta);if(!immediate(d&~255u,&imm))return false;
            a.words[at]=(delta<0?0xe24f0000:0xe28f0000)|(a.fix[i].reg<<12)|imm;
            a.words[at+1]=(delta<0?0xe2400000:0xe2800000)|(a.fix[i].reg<<16)|(a.fix[i].reg<<12)|(d&255u);
        }else a.words[at]=(a.fix[i].condition<<28)|(a.fix[i].link?0x0b000000:0x0a000000)|((uint32_t)(delta/4)&0xffffff);}
    out->size=a.n*4;for(unsigned i=0;i<a.n;++i)put(out->image+i*4,a.words[i]);
    memcpy(out->image,"ARCRHLP1",8);
    const uint32_t fields[]={1,64,(uint32_t)out->size,1,1,64,1,1,1,out->plan,(uint32_t)n,0,0,0};
    for(unsigned i=0;i<14;++i)put(out->image+8+i*4,fields[i]);
    return arc_riscpc_helper_descriptor(out->image,out->size,NULL);
}
