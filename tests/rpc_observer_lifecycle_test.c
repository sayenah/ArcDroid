/* Protected session retirement with observation state. GPL-2.0-only. */
#define main session_fixture_main
#include "rpc_session_test.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);open_library(argv[1]);load_api();
    uint64_t (*epoch)(void),(*generation)(void);void (*seed_pending)(void);bool (*clean)(void);
    LOAD(epoch,"arc_rpc_observer_test_epoch");LOAD(generation,"arc_rpc_test_session_generation");
    LOAD(seed_pending,"arc_rpc_observer_test_seed_pending");LOAD(clean,"arc_rpc_observer_test_clean");
    rom=calloc(1,2097152);assert(rom);rom_words((const uint32_t[]){0xeafffffe},1);
    for(unsigned model=0;model<4;++model){
        uint64_t previous=epoch();assert(start_rpc((ArcRpcModel)model,rom,2097152,seed,946684800,error,sizeof(error)));
        assert(epoch()>previous&&clean());uint64_t g=generation();
        assert(advance_rpc(10000));assert(snapshot().now_ns==10000);
        seed_pending();assert(!clean());previous=epoch();assert(api->reset(error,sizeof(error)));
        assert(epoch()>previous&&generation()==g&&snapshot().now_ns==0&&clean());
        assert(advance_rpc(10000));seed_pending();previous=epoch();fault_next();assert(!advance_rpc(1000));
        assert(api->faulted(NULL)&&epoch()>previous&&generation()==g&&clean());
        previous=epoch();api->stop();assert(epoch()>previous&&clean()&&!snapshot().live_allocations);
        seed_pending();previous=epoch();fail_alloc(1);
        assert(!start_rpc((ArcRpcModel)model,rom,2097152,seed,946684800,error,sizeof(error)));
        assert(epoch()>previous&&generation()==g&&clean()&&!snapshot().live_allocations);fail_alloc(0);
        assert(start_rpc((ArcRpcModel)model,rom,2097152,seed,946684800,error,sizeof(error)));
        assert(generation()==g+1&&clean());seed_pending();previous=epoch();api->stop();
        assert(epoch()>previous&&clean()&&!snapshot().live_allocations);
    }
    free(rom);
#ifdef _WIN32
    assert(FreeLibrary(library));
#else
    assert(!dlclose(library));
#endif
    puts("RPC observation lifecycle: start/reset/fault/failed construction/teardown; epoch, time and session generation");return 0;
}
