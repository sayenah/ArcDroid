/* Sparse authored HD ranges: no whole-disc allocation/read. GPL-2.0-only. */
#include "rpc_boot_filecore.h"
#include "rpc_launch_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ArcBootLayout layout;static ArcBootNode nodes[6];
static uint8_t boot[512],maps[5120],root[2048],child[2048],parent[2048],resource[2048];
static uint32_t parent_at,resource_at;
static uint32_t child_at;static size_t read_bytes;static unsigned reads;
static void checksum(uint8_t *d)
{
    unsigned count=0;while(count<77 && d[5+26*count])++count;
    uint32_t sum=0;unsigned end=5+26*count;
    for(unsigned i=0;i<(end&~3u);i+=4){uint32_t w=0;for(unsigned j=0;j<4;++j)w|=(uint32_t)d[i+j]<<(8*j);sum=w^((sum>>13)|(sum<<19));}
    for(unsigned i=end&~3u;i<end;++i)sum=d[i]^((sum>>13)|(sum<<19));
    for(unsigned i=2008;i<2044;i+=4){uint32_t w=0;for(unsigned j=0;j<4;++j)w|=(uint32_t)d[i+j]<<(8*j);sum=w^((sum>>13)|(sum<<19));}
    d[2047]=(uint8_t)(sum^(sum>>8)^(sum>>16)^(sum>>24));
}
static bool read_range(void *opaque,uint32_t offset,void *out,size_t n)
{
    (void)opaque;++reads;read_bytes+=n;
    const struct {uint32_t at;const uint8_t *data;size_t n;} ranges[]={
        {3072,boot,512},{ARC_BOOT_MAP_AT,maps,5120},{ARC_BOOT_ROOT_AT,root,2048},{child_at,child,2048},
        {parent_at,parent,2048},{resource_at,resource,2048}};
    for(unsigned i=0;i<6;++i)if(ranges[i].at && offset>=ranges[i].at && offset-ranges[i].at<=ranges[i].n && n<=ranges[i].n-(offset-ranges[i].at)){
        memcpy(out,ranges[i].data+offset-ranges[i].at,n);return true;}
    return false;
}
static void prepare(void)
{
    parent_at=resource_at=0;
    memset(nodes,0,sizeof(nodes));strcpy(nodes[0].name,"$");nodes[0].directory=true;nodes[0].count=1;nodes[0].child[0]=1;
    strcpy(nodes[1].name,"!Example");nodes[1].directory=true;nodes[1].parent=0;nodes[1].count=1;nodes[1].child[0]=2;
    strcpy(nodes[2].name,"!Run");nodes[2].parent=1;nodes[2].size=4;nodes[2].attr=3;
    arc_boot_layout_init(&layout);assert(arc_boot_layout_directory(&layout,nodes,0));assert(arc_boot_layout_directory(&layout,nodes,1));
    arc_boot_layout_finish(&layout);arc_boot_layout_boot(boot);memcpy(maps,layout.map,2560);memcpy(maps+2560,maps,2560);
    arc_boot_layout_nick(nodes,0,root);arc_boot_layout_nick(nodes,1,child);
    uint32_t count;assert(arc_boot_layout_resolve(&layout,nodes[1].address,0,2048,&child_at,&count) && count==2048);
    read_bytes=reads=0;
}
static void nested(void)
{
    prepare();
    /* !Boot is an independent root resource beside Apps.!Example. */
    strcpy(nodes[3].name,"Apps");nodes[3].directory=true;nodes[3].count=1;nodes[3].child[0]=1;
    nodes[1].parent=3;strcpy(nodes[4].name,"!Boot");nodes[4].directory=true;
    nodes[0].count=2;nodes[0].child[0]=4;nodes[0].child[1]=3;
    arc_boot_layout_init(&layout);
    const unsigned dirs[]={0,1,3,4};
    for(unsigned i=0;i<4;++i)assert(arc_boot_layout_directory(&layout,nodes,dirs[i]));
    arc_boot_layout_finish(&layout);arc_boot_layout_boot(boot);
    memcpy(maps,layout.map,2560);memcpy(maps+2560,maps,2560);
    arc_boot_layout_nick(nodes,0,root);arc_boot_layout_nick(nodes,1,child);
    arc_boot_layout_nick(nodes,3,parent);arc_boot_layout_nick(nodes,4,resource);
    uint32_t count;
    assert(arc_boot_layout_resolve(&layout,nodes[1].address,0,2048,&child_at,&count));
    assert(arc_boot_layout_resolve(&layout,nodes[3].address,0,2048,&parent_at,&count));
    assert(arc_boot_layout_resolve(&layout,nodes[4].address,0,2048,&resource_at,&count));
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
    nested();assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.readable && d.application_count==1 && !d.scan_incomplete);
    unsigned selected=0;while(selected<d.candidate_count && !arc_launch_candidate_is_target(&d.candidates[selected]))++selected;
    assert(arc_riscpc_plan_build_product(&d,(int)selected,false,id,id,id,&p)==ARC_RPC_PLAN_OK);
    assert(!strcmp(p.target,"ADFS::4.$.Apps.!Example"));
    nested();root[5+26+18]=0;root[5+26+19]=4;checksum(root);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.scan_incomplete && !d.application_count);
    nested();parent[30]^=1;
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.scan_incomplete && !d.application_count);
    nested();nodes[3].parent=3;arc_boot_layout_nick(nodes,3,parent);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.scan_incomplete && !d.application_count);
    nested();nodes[0].parent=3;arc_boot_layout_nick(nodes,0,root);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && !d.readable);
    /* Ordering, prefixes and invalid guest names are checked after a valid CRC. */
    const char *names[]={"!Examplf","!ExampleZ","!Exam","!Exampkf","!Bad*"};
    for(unsigned i=0;i<5;++i){
        prepare();nodes[3]=nodes[1];strcpy(nodes[3].name,names[i]);
        nodes[0].count=2;nodes[0].child[1]=3;arc_boot_layout_nick(nodes,0,root);
        assert(arc_launch_discover_rpc(read_range,NULL,&d));assert(d.readable==(i<2));
    }
    nested();nodes[5].parent=0;strcpy(nodes[5].name,"ZFile");nodes[0].child[2]=5;nodes[0].count=3;
    arc_boot_layout_nick(nodes,0,root);
    assert(arc_launch_discover_rpc(read_range,NULL,&d) && d.readable && d.application_count==1 && !d.scan_incomplete);
    assert(!arc_launch_discover_rpc(NULL,NULL,&d));assert(!arc_launch_discover_rpc(read_range,NULL,NULL));
    puts("RPC products: sparse bounded reads, live map/directory checks, post-install !Run and edited-product refusal passed");return 0;
}
