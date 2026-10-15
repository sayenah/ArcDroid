/* Sparse authored HD ranges: no whole-disc allocation/read. GPL-2.0-only. */
#include "rpc_boot_filecore.h"
#include "rpc_launch_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ArcBootLayout layout;static ArcBootNode nodes[3];
static uint8_t boot[512],maps[5120],root[2048],child[2048];
static uint32_t child_at;static size_t read_bytes;static unsigned reads;
static bool read_range(void *opaque,uint32_t offset,void *out,size_t n)
{
    (void)opaque;++reads;read_bytes+=n;
    const struct {uint32_t at;const uint8_t *data;size_t n;} ranges[]={
        {3072,boot,512},{ARC_BOOT_MAP_AT,maps,5120},{ARC_BOOT_ROOT_AT,root,2048},{child_at,child,2048}};
    for(unsigned i=0;i<4;++i)if(offset>=ranges[i].at && offset-ranges[i].at<=ranges[i].n && n<=ranges[i].n-(offset-ranges[i].at)){
        memcpy(out,ranges[i].data+offset-ranges[i].at,n);return true;}
    return false;
}
static void prepare(void)
{
    memset(nodes,0,sizeof(nodes));strcpy(nodes[0].name,"$");nodes[0].directory=true;nodes[0].count=1;nodes[0].child[0]=1;
    strcpy(nodes[1].name,"!Example");nodes[1].directory=true;nodes[1].parent=0;nodes[1].count=1;nodes[1].child[0]=2;
    strcpy(nodes[2].name,"!Run");nodes[2].parent=1;nodes[2].size=4;nodes[2].attr=3;
    arc_boot_layout_init(&layout);assert(arc_boot_layout_directory(&layout,nodes,0));assert(arc_boot_layout_directory(&layout,nodes,1));
    arc_boot_layout_finish(&layout);arc_boot_layout_boot(boot);memcpy(maps,layout.map,2560);memcpy(maps+2560,maps,2560);
    arc_boot_layout_nick(nodes,0,root);arc_boot_layout_nick(nodes,1,child);
    uint32_t count;assert(arc_boot_layout_resolve(&layout,nodes[1].address,0,2048,&child_at,&count) && count==2048);
    read_bytes=reads=0;
}
int main(void)
{
    prepare();ArcLaunchDiscovery d;assert(arc_launch_discover_rpc(read_range,NULL,&d));
    assert(d.readable && d.application_count==1 && d.candidate_count==1 && !d.scan_incomplete);
    assert(reads==5 && read_bytes==9760);ArcRpcLaunchPlan p;uint8_t id[32]={1};
    assert(arc_riscpc_plan_build_product(&d,0,false,id,id,id,&p)==ARC_RPC_PLAN_OK);
    assert(!strcmp(p.target,"ADFS::4.$.!Example"));
    for(unsigned field=0;field<512;field+=31){prepare();boot[field]^=1;assert(arc_launch_discover_rpc(read_range,NULL,&d));assert(!d.readable);}
    prepare();maps[2560+100]^=1;assert(arc_launch_discover_rpc(read_range,NULL,&d));assert(!d.readable);
    prepare();root[30]^=1;assert(arc_launch_discover_rpc(read_range,NULL,&d));assert(!d.readable);
    prepare();child[30]^=1;assert(arc_launch_discover_rpc(read_range,NULL,&d));assert(d.application_count==0);
    assert(arc_riscpc_plan_build_product(&d,0,false,id,id,id,&p)!=ARC_RPC_PLAN_OK);
    prepare();strcpy(nodes[2].name,"Other");arc_boot_layout_nick(nodes,1,child);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.readable && !d.application_count);
    prepare();nodes[1].parent=1;arc_boot_layout_nick(nodes,1,child);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.application_count);
    prepare();strcpy(nodes[1].name,"!Other");arc_boot_layout_nick(nodes,1,child);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.application_count);
    prepare();nodes[2].size=0;arc_boot_layout_nick(nodes,1,child);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.application_count);
    prepare();nodes[2].address=0xffffff;arc_boot_layout_nick(nodes,1,child);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.application_count);
    prepare();nodes[0].count=2;nodes[0].child[1]=1;arc_boot_layout_nick(nodes,0,root);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.readable);
    assert(!arc_launch_discover_rpc(NULL,NULL,&d));assert(!arc_launch_discover_rpc(read_range,NULL,NULL));
    puts("RPC products: sparse bounded reads, live map/directory checks, post-install !Run and edited-product refusal passed");return 0;
}
