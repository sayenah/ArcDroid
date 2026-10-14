/* D-155: every shipping fault entry in one process. GPL-2.0-only. */
#define main session_main
#include "rpc_session_test.c"
#undef main
#if defined(ARCDROID_SANITIZER_PROFILE)
enum { repetitions=32 }; /* Whole 64 MiB sessions; native Windows keeps 1000. */
#else
enum { repetitions=1000 };
#endif
int main(int argc,char **argv)
{
    assert(argc==3);open_library(argv[1]);load_api();
    void (*seed_pending)(void);bool (*observer_clean)(void);
    LOAD(seed_pending,"arc_rpc_observer_test_seed_pending");LOAD(observer_clean,"arc_rpc_observer_test_clean");
    rom=calloc(1,2097152);assert(rom);rom_words((const uint32_t[]){0xeafffffe},1);
    for(unsigned i=0;i<repetitions;++i) {
        seed_pending();fault_next();assert(!start_rpc(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));
        assert(!snapshot().active&&!snapshot().live_allocations&&error[0]&&observer_clean());
        seed_pending();fail_alloc(1);assert(!start_rpc(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));
        assert(!snapshot().live_allocations&&observer_clean());fail_alloc(0);
        assert(start_rpc(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));
        seed_pending();fault_next();assert(!api->reset(error,sizeof(error))&&api->faulted(NULL));assert(observer_clean());api->stop();
        assert(start_rpc(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));
        assert(observer_clean());seed_pending();fault_next();assert(!advance_rpc(1000)&&api->faulted(NULL));assert(observer_clean());api->stop();
    }
    void (*arc_fault_next)(void);LOAD(arc_fault_next,"arc_engine_test_fault_next");
    FILE *f=fopen(argv[2],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);
    uint8_t *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));
    uint8_t cmos[ARC_CMOS_MACHINE_SIZE]={0},mask[32]={0};
    const ArcMachineDescriptor *model=arc_machine_descriptor(ARC_MACHINE_A3000);
    for(unsigned i=0;i<repetitions;++i) {
        arc_fault_next();
        assert(!api->start(bytes,(size_t)n,NULL,NULL,model,ARC_OS_TEST,NULL,cmos,mask,NULL,NULL,error,sizeof(error)));
        assert(error[0]&&!api->active());api->stop();
        assert(api->start(bytes,(size_t)n,NULL,NULL,model,ARC_OS_TEST,NULL,cmos,mask,NULL,NULL,error,sizeof(error)));
        ArcInputState input;arc_input_reset(&input);ArcEngineFrame frame;
        arc_fault_next();assert(!api->run_frame(&input,&frame)&&api->faulted(NULL));assert(observer_clean());api->stop();
    }
    free(bytes);free(rom);
#ifdef _WIN32
    assert(FreeLibrary(library));
#else
    assert(!dlclose(library));
#endif
    printf("jump repeat: RPC start/allocation/reset/advance and Arculator create/frame faults %u each\n",repetitions);return 0;
}
