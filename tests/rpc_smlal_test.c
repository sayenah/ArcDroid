/* D-157: actual guest instruction, including UBSan boundaries. GPL-2.0-only. */
#define main component_main
#include "rpc_cpu_test.c"
#undef main
int main(void)
{
    static const struct {uint64_t acc;uint32_t a,b;uint64_t want;} cases[]={
        {UINT64_C(0x8000000000000000),1,1,UINT64_C(0x8000000000000001)},
        {UINT64_C(0x7fffffffffffffff),1,1,UINT64_C(0x8000000000000000)},
        {UINT64_MAX,1,1,0}, {0,UINT32_MAX,1,UINT64_MAX},
        {0,0x80000000,0x80000000,UINT64_C(0x4000000000000000)},
        {UINT64_C(0x8000000000000000),0x80000000,0x7fffffff,UINT64_C(0x4000000080000000)}
    };
    arm_init();protected=1;config.mem_size=64;config.vram_size=2;
    machine.model=Model_RPCSA110;machine.cpu_model=CPUModel_SA110;
    if(setjmp(boundary)){fputs("Unexpected SMLAL fault\n",stderr);return 1;}
    mem_init();
    for(unsigned flags=0;flags<2;++flags)for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){
        cpu_reset(CPUModel_SA110);mode32(0,0);arm.reg[16]=0x30000013;
        rom[0]=flags?0xe0f10392:0xe0e10392;
        arm.reg[0]=(uint32_t)cases[i].acc;arm.reg[1]=(uint32_t)(cases[i].acc>>32);
        arm.reg[2]=cases[i].a;arm.reg[3]=cases[i].b;
        arc_rpc_arm_exec_count(1);
        assert(arm.reg[0]==(uint32_t)cases[i].want&&arm.reg[1]==(uint32_t)(cases[i].want>>32));
        uint32_t nz=flags?((cases[i].want>>63)?0x80000000u:0)|(cases[i].want?0:0x40000000u):0;
        assert((arm.reg[16]&0xf0000000)==(0x30000000u|nz));
    }
    arc_rpc_memory_close();puts("SMLAL/SMLALS: 12 modulo/sign/wrap/product/flag cases passed");return 0;
}
