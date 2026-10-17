/* Authored generator oracle; no private material. GPL-2.0-only. */
#include "rpc_launch_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void write_file(const char *path,const void *bytes,size_t n)
{FILE *f=fopen(path,"wb");assert(f);assert(fwrite(bytes,1,n,f)==n);assert(!fclose(f));}
int main(int argc,char **argv)
{
    ArcRpcLaunchPlan p={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(p.target,"ADFS::0.$.!Boot");
    if(argc==4 && !strcmp(argv[1],"--bound")){
        uint8_t wire[64+ARC_RPC_PLAN_TARGET_MAX];FILE *f=fopen(argv[3],"rb");assert(f);
        size_t n=fread(wire,1,sizeof(wire),f);assert(fgetc(f)==EOF && !ferror(f));assert(!fclose(f));
        assert(arc_riscpc_plan_decode(wire,n,&p)==ARC_RPC_PLAN_OK);
    }
    if(argc>1 && !strcmp(argv[1],"--write-ramfs"))p.flags=ARC_RPC_PLAN_RAMFS;
    if(argc>2 && !strcmp(argv[1],"--app")){p.action=ARC_RPC_PLAN_APPLICATION;snprintf(p.target,sizeof(p.target),"ADFS::0.$.%s",argv[2]);}
    ArcRpcLaunchAttempt attempt;ArcRpcAttemptToken token;ArcRpcLaunchController c;
    assert(arc_riscpc_attempt_init(&attempt,UINT64_C(0x123456789abcdef),1));
    assert(arc_riscpc_attempt_prepare(&attempt,&p,0,&token));
    assert(arc_riscpc_controller_build(&p,&token,&c));
    assert(c.size>64 && c.size<=8192 && c.query<c.status && c.status<c.plan && c.latch<c.command);
    uint32_t entry;assert(arc_riscpc_helper_descriptor(c.image,c.size,&entry) && entry==64);
    assert(!memcmp(c.image+c.plan,"ARCRLPLN\2\0\0\0",12));
    assert(!strcmp((char *)c.image+c.command,c.text));
    /* D-204: pin each OldId independently of allocation/checksum differences. */
    uint8_t *carrier=malloc(ARC_RPC_HELPER_SIZE);assert(carrier);
    assert(arc_riscpc_helper_carrier(carrier,ARC_RPC_HELPER_SIZE,c.image,c.size));
    assert(carrier[507]==0x45 && carrier[508]==0x41);
    assert(arc_riscpc_helper_build(carrier,ARC_RPC_HELPER_SIZE));
    assert(carrier[507]==0x44 && carrier[508]==0x41);free(carrier);
    if(argc==4 && !strcmp(argv[1],"--bound")){write_file(argv[2],c.image,c.size);return 0;}
    if(argc>2 && (!strcmp(argv[1],"--write") || !strcmp(argv[1],"--write-ramfs"))){write_file(argv[2],c.image,c.size);return 0;}
    if(argc>3 && !strcmp(argv[1],"--app")){uint8_t *disc=malloc(ARC_RPC_HELPER_SIZE);assert(disc);
        assert(arc_riscpc_helper_carrier(disc,ARC_RPC_HELPER_SIZE,c.image,c.size));write_file(argv[3],disc,ARC_RPC_HELPER_SIZE);free(disc);return 0;}
    if(argc>2 && !strcmp(argv[1],"--carrier")){uint8_t *disc=malloc(ARC_RPC_HELPER_SIZE);assert(disc);
        assert(arc_riscpc_helper_carrier(disc,ARC_RPC_HELPER_SIZE,c.image,c.size));write_file(argv[2],disc,ARC_RPC_HELPER_SIZE);free(disc);return 0;}
    token.plan[0]^=1;assert(!arc_riscpc_controller_build(&p,&token,&c));token.plan[0]^=1;
    token.session=0;assert(!arc_riscpc_controller_build(&p,&token,&c));token.session=1;
    token.epoch=0;assert(!arc_riscpc_controller_build(&p,&token,&c));token.epoch=1;
    token.serial=0;assert(!arc_riscpc_controller_build(&p,&token,&c));token.serial=1;
    token.epoch=UINT64_MAX;token.serial=2;assert(!arc_riscpc_controller_build(&p,&token,&c));
    token.serial=1;assert(arc_riscpc_controller_build(&p,&token,&c));token.epoch=1;
    p.action=100;assert(!arc_riscpc_controller_build(&p,&token,&c));
    puts("RPC controller: canonical plan, descriptor, command, latch and token bounds passed");return 0;
}
