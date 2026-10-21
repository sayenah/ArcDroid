/* SPDX-License-Identifier: GPL-2.0-only */
/* Authored public-API model, not copied RetroArch code. Source ledger:
 * RetroArch v1.22.2 runloop.c core_init_libretro_cbs/core_set_default_callbacks,
 * core_run; runahead.c runahead_run/runahead_core_run_use_last_input,
 * runahead_err/preempt_init/preempt_run/preempt_deinit; input/input_driver.c
 * input_driver_poll (command bits cleared before polling the socket).
 * In particular the initial poll wrapper and retro_ctx.poll_cb DIFFER. */
static unsigned model_poll_type,model_polls,model_saves,model_refusals,model_loads;
static bool model_polled,model_command,model_datagram;
static void model_driver_poll(void)
{
    ++model_polls;model_command=model_datagram;model_datagram=false;
}
static void model_poll_maybe(void){if(model_poll_type==0)model_driver_poll();}
static int16_t model_input(unsigned p,unsigned d,unsigned i,unsigned id)
{
    if(model_poll_type==2 && !model_polled){model_driver_poll();model_polled=true;}
    return input(p,d,i,id);
}
static void model_core_run(void)
{
    model_polled=false;
    if(model_poll_type==1)model_driver_poll();
    actual_run();
    if(model_poll_type==2 && !model_polled)model_driver_poll();
}
static bool model_save(void *state,size_t bytes)
{
    ++model_saves;bool okay=serialize(state,bytes);if(!okay)++model_refusals;return okay;
}
static void model_secondary(const struct retro_game_info *game,const char *path)
{
    unsigned w=writes,r=registrations,n=notices;
#ifdef _WIN32
    HMODULE primary=library,copy=LoadLibraryA(path);
#else
    void *primary=library,*copy=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
    assert(copy);library=copy;
    void (*se)(retro_environment_t),(*si)(void),(*su)(void),(*sd)(void);
    bool (*sl)(const struct retro_game_info *);
    LOAD(se,"retro_set_environment");LOAD(si,"retro_init");LOAD(sl,"retro_load_game");
    LOAD(su,"retro_unload_game");LOAD(sd,"retro_deinit");
    se(secondary_environment);si();assert(!sl(game));su();sd();
#ifdef _WIN32
    assert(FreeLibrary(copy));
#else
    assert(!dlclose(copy));
#endif
    library=primary;assert(writes==w && registrations==r && notices==n);
}
static void model_runahead(const struct retro_game_info *game,const char *copy,
    unsigned frames,bool secondary,bool *available,bool *secondary_available,
    void *state,size_t bytes)
{
    if(!*available){model_core_run();return;}
    /* The failed secondary creation does NOT disable primary run-ahead.
     * It runs one ordinary frame and tries the single instance next time. */
    if(secondary && *secondary_available){
        model_secondary(game,copy);*secondary_available=false;model_core_run();return;
    }
    model_core_run();
    if(!model_save(state,bytes)){*available=false;return;} /* runahead_err */
    for(unsigned i=0;i<frames;++i){
        set_poll(null_poll);set_input(input);actual_run();
        /* core_set_default_callbacks initializes THIS to input_driver_poll,
         * not model_poll_maybe. The former harness restored the same pointer. */
        set_poll(model_driver_poll);set_input(model_input);
    }
    ++model_loads;
    if(!unserialize(state,bytes))*available=false;
}
static void model_session(const struct retro_game_info *game,const char *copy,
    void (*unload)(void),void (*deinit)(void),unsigned settings)
{
    bool secondary=(settings&1u)!=0,preempt=(settings&2u)!=0,rewind=(settings&4u)!=0;
    unsigned frames=(settings&8u)?3u:1u;
    allow_arculator=true;assert(primary_load(game));
    set_video(video);set_sample(sample);set_audio(audio);
    set_poll(model_poll_maybe);set_input(model_input);
    callbacks_ready=true;
    struct retro_system_av_info av={0};av_info(&av);assert(av.timing.fps>0);
    model_saves=model_refusals=model_loads=model_polls=0;
    model_command=model_datagram=false;
    size_t bytes=state_size();assert(bytes && bytes<=16u*1024u*1024u);
    unsigned char *state=malloc(bytes),*rewind_state=malloc(bytes);assert(state && rewind_state);
    bool available=true,secondary_available=true,preempt_live=false;
    unsigned n=mode_notices,preempt_attempts=0,stable_saves=0;
    /* Startup preempt_init runs a frame BEFORE installing null_poll. A
     * runtime CMD_EVENT_PREEMPT_UPDATE is the only re-entry after failure. */
    model_core_run();
    for(unsigned phase=0;phase<5;++phase){
        bool use_preempt=phase==0?preempt:phase==1?true:phase==3?true:false;
        /* Startup config can have BOTH booleans true. Runtime PREEMPT_TOGGLE
         * instead disables run_ahead_enabled (retroarch.c:3254); switching
         * back with RUNAHEAD_TOGGLE disables preempt and calls its teardown. */
        bool use_ahead=phase==0 || phase==2;
        if(use_preempt){
            preempt_live=true;available=secondary_available=false;
            set_poll(null_poll);set_input(model_input);++preempt_attempts;
        }
        unsigned calls=model_saves,failures=model_refusals;
        bool retry_secondary=secondary && (use_preempt || secondary_available);
        for(unsigned f=0;f<24;++f){
            unsigned polls=model_polls;
            if(f==23)model_datagram=true; /* Commands while each mode is active. */
            /* runloop.c checks rewind BEFORE runahead/preempt. Rewind keeps
             * calling serialize even after runahead_err; it ignores a failed
             * capture. Never treat those calls as repeated runahead attempts. */
            if(rewind){
                bool saved=model_save(rewind_state,bytes);
                assert(saved==!preempt_live);if(saved)++stable_saves;
            }
            if(preempt_live){
                model_driver_poll();
                assert(!model_save(state,bytes));
                /* preempt_run error path frees preempt_data and restores
                 * the ORIGINAL callback; availability bits are both reset. */
                preempt_live=false;available=secondary_available=true;
                set_poll(model_poll_maybe);set_input(model_input);
            } else if(use_ahead)
                model_runahead(game,copy,frames,secondary,&available,&secondary_available,state,bytes);
            else model_core_run();
            assert(model_polls==polls+1); /* Never erase a network command. */
            if(f==23)assert(model_command && !model_datagram);
            if(f==12 && rewind){++model_loads;assert(unserialize(rewind_state,bytes));}
        }
        unsigned expected_ahead=use_ahead?24u-(use_preempt?1u:0u)-(retry_secondary?1u:0u):0u;
        assert(model_saves-calls==expected_ahead+(use_preempt?1u:0u)+(rewind?24u:0u));
        assert(model_refusals-failures==(use_preempt?(rewind?2u:1u):0u));
        assert(available && !preempt_live);
        assert(mode_notices-n==(preempt_attempts?1u:0u));
    }
    /* QUIT and CLOSE_CONTENT are command bits, consumed at the following
     * runloop check. Deliver one datagram; a duplicate poll would erase it. */
    model_datagram=true;
    model_runahead(game,copy,frames,secondary,&available,&secondary_available,state,bytes);
    assert(model_command && !model_datagram);
    unsigned calls=model_saves;
#ifdef _WIN32
    ULONGLONG start=GetTickCount64();unload();deinit();
    assert(GetTickCount64()-start<2000);
#else
    struct timespec start,end;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
    unload();deinit();assert(!clock_gettime(CLOCK_MONOTONIC,&end));
    assert((double)(end.tv_sec-start.tv_sec)+(double)(end.tv_nsec-start.tv_nsec)/1e9<2.0);
#endif
    assert(state_size()==0 && model_saves==calls);vfs_assert_no_open_handles();
    printf("SS1 RA1.22.2 settings=%u poll=%u frames=%u saves=%u refused=%u restores=%u notices=%u rewind=%u: command delivered; unload/deinit complete\n",
        settings,model_poll_type,frames,model_saves,model_refusals,model_loads,mode_notices-n,stable_saves);
    free(state);free(rewind_state);
}
