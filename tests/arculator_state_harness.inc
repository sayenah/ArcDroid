/* ArcDroid authored save-state differential matrix. GPL-2.0-only. */
if(ss1_mode) {
   unsigned ss1_cost_samples=3u;
   const char *ss1_sample_text=getenv("ARCDROID_SS1_COST_SAMPLES");
   if(ss1_sample_text) {
      char *end;unsigned long n=strtoul(ss1_sample_text,&end,10);
      assert(!*end&&n&&n<=2000u);ss1_cost_samples=(unsigned)n;
   }
   bool (*retro_serialize)(void *,size_t);LOAD(retro_serialize);
   bool (*retro_unserialize)(const void *,size_t);LOAD(retro_unserialize);
   ss1_core_restore=retro_unserialize;retro_unserialize=ss1_restore;
   ss1_tables_check=dlsym(module,"arc_engine_test_state_tables");
   if(!strcmp(argv[5],"--ss1-shipping")) {
      option_machine="A3000";option_launch="off";option_writes="off";
      assert(ss1_quirks_calls==1u);phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
      for(unsigned i=0;i<4u;++i)retro_run();
      size_t capacity=retro_serialize_size();assert(capacity);
      uint8_t *snapshot=malloc(capacity),*canonical=malloc(capacity);assert(snapshot&&canonical);
      assert(retro_serialize(snapshot,capacity));retro_run();assert(retro_unserialize(snapshot,capacity));
      assert(retro_serialize(canonical,capacity)&&!memcmp(snapshot,canonical,capacity));
      assert(retro_serialize_size()==capacity&&ss1_quirks_calls==1u);
      free(snapshot);free(canonical);retro_unload_game();assert(!retro_serialize_size());retro_deinit();dlclose(module);
      puts("SS1 shipping: complete Arculator save/load, fixed size, canonical bytes and one quirks call");return 0;
   }
   if(strcmp(argv[5],"--ss1-output")) {
      assert(!(ss1_quirks&RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE));
      assert(ss1_quirks_calls==1u);
      assert((ss1_quirks&(RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE|
         RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT|RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT))==
         (RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE|
         RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT|RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT));
   }

   size_t (*cpu_snapshot)(void *,size_t)=dlsym(module,"arc_engine_test_cpu_snapshot");
   bool output_only=!strcmp(argv[5],"--ss1-output");
   if(!output_only)assert(cpu_snapshot&&ss1_tables_check);
   bool produce=!strcmp(argv[5],"--ss1-produce"),consume=!strcmp(argv[5],"--ss1-consume")||!strcmp(argv[5],"--ss1-publish");
   assert(argc==6||argc==7);
   const char *machines[]={"A3000","A3010","A3020","A4000"};
   const char *media_class=getenv("ARCDROID_SS1_CLASS");if(!media_class)media_class="adf";
   option_writes="off";option_launch="off";
   if(!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty")){option_hard_disc="auto";option_writes="shadow";}
   if(!strcmp(media_class,"adffs"))option_launch="auto";
   if(!strcmp(media_class,"adf-shadow")||!strcmp(media_class,"adl-shadow")||!strcmp(media_class,"hd-media-shadow"))option_writes="shadow";
   if(!strcmp(media_class,"adf-original"))option_writes="original";
   if(!strcmp(media_class,"jfd-shadow")||!strcmp(media_class,"jfd-swap")){option_writes="shadow";option_jfd_boot="native";}
   unsigned first=0,last=4;
   if(argc==7) {for(first=0;first<4u&&strcmp(argv[6],machines[first]);++first){}assert(first<4u);last=first+1u;}
   for(unsigned machine_index=first;machine_index<last;++machine_index) {
      char state_path[4096],trace_path[4096];
      snprintf(state_path,sizeof(state_path),"%s/state.bin",data_root);
      snprintf(trace_path,sizeof(trace_path),"%s/trace.bin",data_root);
      option_machine=machines[machine_index];option_joystick=machine_index==1u?"A3010":"Keyboard";
      phase=PHASE_LOAD;if(!retro_load_game(&game)){fprintf(stderr,"SS1 load refused class=%s machine=%s: %s\n",media_class,option_machine,last_message);abort();}phase=PHASE_RUN;
      if(!output_only)assert(ss1_tables_check());
      {struct retro_system_av_info av;retro_get_system_av_info(&av);geometry_width=av.geometry.base_width;geometry_height=av.geometry.base_height;current_reported_rate=last_notified_rate=av.timing.fps;}
      if(!output_only){memset(ss1_last_input,0,sizeof(ss1_last_input));retro_set_input_state(ss1_logged_input);}
      if(!strcmp(argv[5],"--ss1-created-cold")||!strcmp(argv[5],"--ss1-created-cold-refusal")) {
         bool (*create)(unsigned,unsigned,unsigned,uint8_t)=dlsym(module,"arc_libretro_test_ss1_jfd_create");
         uint8_t (*slot)(unsigned,unsigned,unsigned,uint32_t*,uint32_t*)=dlsym(module,"arc_libretro_test_jfd_slot_state");
         bool (*created)(unsigned,unsigned,unsigned,unsigned,uint8_t*)=dlsym(module,"arc_libretro_test_jfd_created_byte");
         void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");
         bool (*pending)(void)=dlsym(module,"arc_state_io_pending");
         assert(create&&slot&&created&&speculate&&pending);
         for(unsigned i=0;i<96u;++i)retro_run();
         unsigned track=0,id=0;
         for(;track<ARC_JFD_PDD5_TRACKS;++track) {
            for(id=0;id<ARC_JFD_PDD5_IDS;++id)if((slot(0,track,id,NULL,NULL)&12u)==4u)break;
            if(id<ARC_JFD_PDD5_IDS)break;
         }
         assert(track<ARC_JFD_PDD5_TRACKS);
         /* Exceptional real log: preserve the native frontier while capturing
          * an admitted created payload and its pending publication. */
         speculate(true);assert(create(0,track,id,0x5au)&&pending());
         size_t n=retro_serialize_size();uint8_t *saved=malloc(n),*again=malloc(n);
         assert(saved&&again&&retro_serialize(saved,n));uint64_t native=ss1_native_mutations;
         retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);retro_run();
         assert(disk_control.set_eject_state(true));
         uint8_t value;assert(!created(0,track,id,0,&value));
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
         if(!strcmp(argv[5],"--ss1-created-cold-refusal")) {
            size_t at=ss1_record(saved,"media.0.jfd.payloads",6);assert(at&&ss1_get64(saved+at+8)==6u);
            uint8_t *payload=saved+at+24;size_t count=(size_t)ss1_get64(payload),from=count+track*5u+id,to=count;
            assert(from!=to&&(payload[16u+from/8u]&(1u<<(from%8u))));
            payload[16u+from/8u]^=(uint8_t)(1u<<(from%8u));payload[16u+to/8u]|=(uint8_t)(1u<<(to%8u));ss1_checksum(saved);
            uint8_t before[1024],after[1024];size_t cpu_n=cpu_snapshot(before,sizeof(before));
            assert(!retro_unserialize(saved,n));retro_run();
            assert(cpu_snapshot(after,sizeof(after))==cpu_n&&!memcmp(before,after,cpu_n)&&native==ss1_native_mutations);
            retro_unload_game();assert(native==ss1_native_mutations);free(saved);free(again);
            printf("SS1 created cold refusal: machine=%s; forbidden slot rejects without guest execution/publication\n",option_machine);continue;
         }
         assert(retro_unserialize(saved,n));
         for(unsigned i=0;i<1024u;++i)assert(created(0,track,id,i,&value)&&value==0x5au);
         assert(retro_serialize(again,n)&&!memcmp(saved,again,n)&&pending()&&ss1_native_mutations==native);
         retro_run();assert(!pending()&&ss1_native_mutations>native);
         free(saved);free(again);retro_unload_game();
         printf("SS1 created cold: machine=%s track=%u id=%u; released graph rebuilt, all 1024 bytes/canonical state exact, publication once\n",option_machine,track,id);continue;
      }
      if(!strcmp(argv[5],"--ss1-large-derived")) {
         bool (*prepare)(void)=dlsym(module,"arc_libretro_test_ss1_large_derived");assert(prepare);
         retro_run();assert(prepare());size_t n=retro_serialize_size();assert(n<=16u*1024u*1024u);
         /* Even the conservative union of the parser's payload/slot limits,
          * maximal bitmap and 31 inactive graphs must fit the advertised cap. */
         size_t worst=n+65536u+16u+
            (ARC_JFD_MAX_TRACK_SIDES*ARC_JFD_MAX_SECTORS_PER_TRACK+ARC_JFD_PDD5_SLOTS+7u)/8u+
            ARC_JFD_MAX_DECODED+ARC_JFD_PDD5_SLOTS*1024u+(ARC_PLAYLIST_MAX_ENTRIES-1u)*96u;
         assert(worst<=16u*1024u*1024u);
         uint8_t *state=malloc(n),*again=malloc(n);assert(state&&again&&retro_serialize(state,n));
         assert(retro_unserialize(state,n)&&retro_serialize(again,n)&&!memcmp(state,again,n));
         uint64_t (*timings)[2]=dlsym(module,"mem_speed");assert(timings);
         uint64_t timing=timings[1][0];timings[1][0]^=1u;memset(again,0x69,n);
         assert(!retro_serialize(again,n));
         for(size_t i=0;i<n;++i)assert(again[i]==0x69);
         timings[1][0]=timing;
         assert(retro_serialize(again,n)&&!memcmp(state,again,n));
         free(state);free(again);retro_unload_game();
         printf("SS1 derived ownership: machine=%s base=%zu worst-floppy=%zu; 32 MiB OSK scratch restores within independent budgets\n",option_machine,n,worst);continue;
      }
      if(!strcmp(argv[5],"--ss1-video")) {
         void (*scenario)(unsigned)=dlsym(module,"arc_engine_test_video_scenario");
         void (*rebuild)(void)=dlsym(module,"arc_platform_video_rebuild");
         void (*partial)(int)=dlsym(module,"execarm");
         unsigned (*position)(ArcArculatorVidcResetState*)=dlsym(module,"arc_arculator_test_vidc_state");
         const uint32_t *(*picture)(unsigned*,unsigned*,size_t*,uint64_t*)=dlsym(module,"arc_platform_video_frame");
         assert(scenario&&rebuild&&partial&&position&&picture);unsigned parities[16]={0};
         for(unsigned i=0;i<96u;++i)retro_run();
         size_t n=retro_serialize_size();assert(n<=16u*1024u*1024u);
         uint8_t *state=malloc(n),*again=malloc(n);assert(state&&again);
         for(unsigned test=0;test<64u;++test) {
            if(test>=32u&&!(test&16u))continue; /* Noninterlace has no field parity. */
            ss1_set_input(200u);scenario(test);retro_run();partial((int)(16u+(test%7u)*16u));
            /* Capture the requested parity at this real partial-field phase. */
            scenario(test);
            ArcArculatorVidcResetState phase;unsigned parity=position(&phase);assert(parity==((test>>5u)&1u));
            if(test&16u)parities[test&15u]|=1u<<parity;
            assert(retro_serialize(state,n));
            uint64_t guest[2],audio[2],video[2];unsigned different=0;
            for(unsigned i=0;i<2u;++i){poweron_pcm_hash=UINT64_C(1469598103934665603);poweron_trace=true;
               retro_run();audio[i]=poweron_pcm_hash;video[i]=last_video_signature;
               assert(retro_serialize(again,n));guest[i]=ss1_get64(again+32);}
            for(unsigned repetition=0;repetition<2u;++repetition) {
               assert(retro_unserialize(state,n)&&retro_serialize(again,n)&&!memcmp(state,again,n));
               if(!repetition) {
                  rebuild();assert(retro_serialize(again,n)&&!memcmp(state,again,n));
               }
               for(unsigned i=0;i<2u;++i){poweron_pcm_hash=UINT64_C(1469598103934665603);retro_run();
                  different+=video[i]!=last_video_signature;
                  assert(audio[i]==poweron_pcm_hash&&retro_serialize(again,n)&&guest[i]==ss1_get64(again+32));}
            }
            /* One speculative frame, callbacks restored before rollback. */
            assert(retro_unserialize(state,n));
            retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);retro_run();
            unsigned ahead_different=video[0]!=last_video_signature;
            retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
            assert(retro_unserialize(state,n));poweron_pcm_hash=UINT64_C(1469598103934665603);retro_run();
            different+=video[0]!=last_video_signature;
            assert(audio[0]==poweron_pcm_hash&&retro_serialize(again,n)&&guest[0]==ss1_get64(again+32));
            printf("SS1 video: machine=%s control=%u interlace=%u seed-parity=%u saved-parity=%u line=%d phase=%d partial-cycles=%u compared=5 picture-different-frames=%u guest-exact runahead-displayed-picture-different=%u\n",
               option_machine,test&15u,(test>>4u)&1u,test>>5u,parity,phase.line,phase.phase,16u+(test%7u)*16u,different,ahead_different);
            /* Independently authored bit patterns check RAM/palette decoding
             * even where the boot ROM leaves ordinary scanout blank. */
            assert(retro_unserialize(state,n));scenario(test|128u);
            assert(retro_serialize(again,n));uint8_t *known=malloc(n);assert(known);memcpy(known,again,n);
            assert(retro_unserialize(known,n)&&retro_serialize(again,n)&&!memcmp(known,again,n));
            unsigned w,h;size_t stride;const uint32_t *pixels=picture(&w,&h,&stride,NULL);
            const unsigned expected[4][8]={{0,0,1,0,0,1,1,1},{0,1,2,3,0,1,2,3},{4,14,4,14,4,14,4,14},{228,228,228,228,228,228,228,228}};
            unsigned depth=(test>>2u)&3u,repeat=(test&2u)?1u:2u;assert(pixels&&w>=8u*repeat&&h);
            for(unsigned x=0;x<8u*repeat;++x)
               assert(pixels[x]==(depth==3u?0x00334400u:0x00112200u)+expected[depth][x/repeat]);
            free(known);assert(retro_unserialize(state,n));
         }
         for(unsigned control=0;control<16u;++control)assert(parities[control]==3u);
         free(state);free(again);retro_unload_game();continue;
      }
      if(!strcmp(argv[5],"--ss1-rewind-buffer")) {
         for(unsigned i=0;i<96u;++i)retro_run();
         size_t n=retro_serialize_size(),block=24u+((n+7u)&~7u),maximum=(block+1u)&~1u;
         maximum+=((block+131069u)/131070u)*4u+6u+3u*sizeof(size_t);
         assert(n<=16u*1024u*1024u&&maximum<20000000u);
         uint8_t *ring=malloc(20000000u),*previous=calloc(block,1u),*current=calloc(block,1u),*check=malloc(n);
         assert(ring&&previous&&current&&check);size_t offsets[12],lengths[12],used=0;
         uint64_t guest[13],pcm[12];
         for(unsigned copy=0;copy<2u;++copy) {
            uint8_t *wrapped=copy?current:previous;
            memcpy(wrapped,"RASTATE1MEM ",12u);
            for(unsigned byte=0;byte<4u;++byte)wrapped[12u+byte]=(uint8_t)(n>>(byte*8u));
            memcpy(wrapped+block-8u,"END ",4u);
         }
         assert(retro_serialize(previous+16u,n));
         guest[0]=ss1_get64(previous+48u);
         for(unsigned frame=0;frame<12u;++frame) {
            ss1_set_input(frame);poweron_trace=true;poweron_pcm_hash=UINT64_C(1469598103934665603);
            retro_run();pcm[frame]=poweron_pcm_hash;assert(retro_serialize(current+16u,n));
            guest[frame+1u]=ss1_get64(current+48u);
            offsets[frame]=used;
            /* Bounded reverse byte runs model rewind's previous-state patches. */
            for(size_t at=0;at<block;) {
               if(previous[at]==current[at]){++at;continue;}
               size_t start=at;while(at<block&&previous[at]!=current[at])++at;
               size_t count=at-start;assert(used+16u+count<=20000000u);
               ss1_put64(ring+used,start);ss1_put64(ring+used+8u,count);used+=16u;
               memcpy(ring+used,previous+start,count);used+=count;
            }
            lengths[frame]=used-offsets[frame];memcpy(previous,current,block);
         }
         for(unsigned frame=12u;frame--;) {
            for(unsigned repeat=0;repeat<2u;++repeat)
               assert(retro_unserialize(current+16u,n)&&retro_serialize(check,n)&&!memcmp(check,current+16u,n));
            size_t end=offsets[frame]+lengths[frame];
            for(size_t at=offsets[frame];at<end;) {
               size_t start=(size_t)ss1_get64(ring+at),count=(size_t)ss1_get64(ring+at+8u);at+=16u;
               assert(start<=block&&count<=block-start&&count<=end-at);memcpy(current+start,ring+at,count);at+=count;
            }
            assert(ss1_get64(current+48u)==guest[frame]&&retro_unserialize(current+16u,n));
            ss1_set_input(frame);poweron_pcm_hash=UINT64_C(1469598103934665603);retro_run();
            assert(pcm[frame]==poweron_pcm_hash&&retro_serialize(check,n)&&ss1_get64(check+32u)==guest[frame+1u]);
         }
         assert(retro_unserialize(current+16u,n)&&retro_serialize(check,n)&&!memcmp(check,current+16u,n));
         printf("SS1 default rewind: class=%s machine=%s capacity=20000000 core=%zu wrapped=%zu maximum-patch=%zu used=%zu captures=13 repeated-loads=24 forward-replays=12 passed\n",media_class,option_machine,n,block,maximum,used);
         free(ring);free(previous);free(current);free(check);retro_unload_game();continue;
      }
      if(!strcmp(media_class,"folder")||!strcmp(media_class,"playlist"))assert(disk_control.get_num_images()==2u);
      if(!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty"))assert(strstr(hard_disc_policy_log,"attach=yes"));
      if(!strcmp(media_class,"adffs")) {
         bool (*released)(void)=dlsym(module,"arc_libretro_test_adffs_released");assert(released&&!released());
      }
      if(!strcmp(argv[5],"--ss1-check-store")) {
         bool (*status)(uint64_t*)=dlsym(module,"arc_libretro_test_ss1_store_status");uint64_t values[4];assert(status&&status(values));
         printf("SS1 store-status %llu %llu %llu %llu\n",(unsigned long long)values[0],(unsigned long long)values[1],(unsigned long long)values[2],(unsigned long long)values[3]);
         retro_unload_game();continue;
      }
      if(!strcmp(argv[5],"--ss1-write-path")) {
         void *(*raw_file)(unsigned)=dlsym(module,"arc_engine_test_raw_file");
         const char *(*staged)(unsigned)=dlsym(module,"arc_libretro_test_staged_path");assert(raw_file&&staged);
         Ss1WritePath current={.file=raw_file(0)};assert(current.file&&retro_serialize_size());
         current.seek=dlsym(module,"arc_io_seek_file");current.write=dlsym(module,"arc_io_write_file");current.flush=dlsym(module,"arc_io_flush_file");
         assert(current.seek&&current.write&&current.flush);uint64_t samples[15],reference[30];
         const char *baseline=getenv("ARCDROID_SS1_IO_BASELINE");void *comparison=NULL;Ss1WritePath main_path={0};
         bool (*close_main)(void *)=NULL;
         if(baseline&&*baseline){
            comparison=dlopen(baseline,RTLD_NOW|RTLD_LOCAL);assert(comparison);
            void (*set_vfs)(struct retro_vfs_interface *)=dlsym(comparison,"arc_io_set_vfs");
            void *(*open_main)(const char *,bool)=dlsym(comparison,"arc_io_open_file");
            void (*set_observer)(void (*)(const char *))=dlsym(comparison,"arc_io_set_write_observer");
            void (*(*observer)(void))(const char *)=dlsym(module,"arc_libretro_test_ss1_write_observer");
            main_path.seek=dlsym(comparison,"arc_io_seek_file");main_path.write=dlsym(comparison,"arc_io_write_file");main_path.flush=dlsym(comparison,"arc_io_flush_file");close_main=dlsym(comparison,"arc_io_close_file");
            assert(set_vfs&&open_main&&set_observer&&observer&&main_path.seek&&main_path.write&&main_path.flush&&close_main);
            set_vfs(&vfs);set_observer(observer());main_path.file=open_main(staged(0),true);assert(main_path.file);
         }
         size_t capacity=retro_serialize_size();uint8_t *captured=malloc(capacity);assert(captured);
         const char *stages[]={"uncaptured","post-capture","reused-folder"};
         for(unsigned stage=0;stage<3u;++stage) {
            if(stage==1u)assert(retro_serialize(captured,capacity));
            if(stage==2u) {
               if(comparison){assert(close_main(main_path.file));main_path.file=NULL;}
               retro_unload_game();phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
               current.file=raw_file(0);assert(current.file);
               if(comparison){
                  void *(*open_main)(const char *,bool)=dlsym(comparison,"arc_io_open_file");
                  main_path.file=open_main(staged(0),true);assert(main_path.file);
               }
            }
            ss1_native_read_bytes=ss1_native_write_bytes=0;
            uint64_t first_ns=ss1_write_batch(&current); /* Includes first-write invalidation on reuse. */
            uint64_t first_reads=ss1_native_read_bytes,first_writes=ss1_native_write_bytes;
            uint64_t reads=stage==1u?1376u:0u,writes=stage==1u?1888u:512u;
            assert(first_reads==256u*reads+(stage==2u?168u:0u)||
               (stage==0u&&first_reads==168u)); /* The folder may already have state history. */
            assert(first_writes==256u*writes+(first_reads==168u?168u:0u));
            for(unsigned i=0;i<15u;++i){
               if(comparison)reference[i*2u]=ss1_write_batch(&main_path);
               ss1_native_read_bytes=ss1_native_write_bytes=0;samples[i]=ss1_write_batch(&current);
               assert(ss1_native_read_bytes==256u*reads&&ss1_native_write_bytes==256u*writes);
               if(comparison)reference[i*2u+1u]=ss1_write_batch(&main_path);
            }
            qsort(samples,15u,sizeof(*samples),ss1_ns_order);
            printf("SS1 sector IO: stage=%s machine=%s medium=%llu read=%llu written=%llu per sector; first-batch-read=%llu first-batch-written=%llu first-batch-mean=%llu ns; p50=%llu ns",
               stages[stage],option_machine,(unsigned long long)digest_file(staged(0)).size,
               (unsigned long long)reads,(unsigned long long)writes,(unsigned long long)first_reads,
               (unsigned long long)first_writes,(unsigned long long)first_ns,(unsigned long long)samples[7]);
            if(comparison){
               qsort(reference,30u,sizeof(*reference),ss1_ns_order);
               uint64_t band=reference[26]-reference[3];
               printf(" main-p50=%llu ns main-p10/p90=%llu/%llu ns noise=%llu ns\n",(unsigned long long)reference[15],(unsigned long long)reference[3],(unsigned long long)reference[26],(unsigned long long)band);fflush(stdout);
               if(stage!=1u)assert(samples[7]<=reference[26]+band);
            }else puts(" (main time comparison not requested)");
         }
         if(comparison){assert(close_main(main_path.file));assert(!dlclose(comparison));}
         /* RetroArch's save -> fake frame -> restored callbacks -> rollback.
          * Rewind each sample, so the runtime log budget is never accumulated
          * across independent predictions. Include the first staged raw write. */
         retro_run();assert(retro_serialize(captured,capacity));uint64_t native=ss1_native_mutations;
         for(unsigned i=0;i<15u;++i) {
            retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);retro_run();
            current.file=raw_file(0);assert(current.file);ss1_native_read_bytes=ss1_native_write_bytes=0;
            samples[i]=ss1_write_batch(&current);
            assert(!ss1_native_read_bytes&&!ss1_native_write_bytes&&ss1_native_mutations==native);
            retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
            assert(retro_unserialize(captured,capacity));current.file=raw_file(0);assert(current.file);
         }
         qsort(samples,15u,sizeof(*samples),ss1_ns_order);
         printf("SS1 sector IO: stage=speculative machine=%s read=0 written=0 per sector; p50=%llu ns; 15 actual fake-frame/rollback sequences, no publication\n",option_machine,(unsigned long long)samples[7]);
         free(captured);
         assert(!ss1_context_queries);retro_unload_game();continue;
      }
      if(!strcmp(argv[5],"--ss1-replacement")) {
         size_t bound=retro_serialize_size();uint8_t *buffer=malloc(bound);assert(buffer);
         assert(retro_serialize(buffer,bound));
         void *(*raw_file)(unsigned)=dlsym(module,"arc_engine_test_raw_file");assert(raw_file);
         Ss1WritePath writer={0};writer.seek=dlsym(module,"arc_io_seek_file");
         writer.write=dlsym(module,"arc_io_write_file");writer.flush=dlsym(module,"arc_io_flush_file");
         assert(writer.seek&&writer.write&&writer.flush);
         /* More replacements than the registry's simultaneous-store limit:
          * inactive registrations must not exhaust or enter current states. */
         for(unsigned cycle=0;cycle<72u;++cycle){
            assert(disk_control.set_eject_state(true));
            char path[4096];snprintf(path,sizeof(path),"%s/replacement-%u.adf",data_root,cycle);copy_file(argv[3],path);
            FILE *f=fopen(path,"r+b");assert(f&&!fseek(f,100u*1024u,SEEK_SET));assert(fputc((int)cycle,f)!=EOF&&!fclose(f));
            struct retro_game_info replacement={.path=path};assert(disk_control.replace_image_index(0,&replacement));
            assert(disk_control.set_eject_state(false));retro_run();assert(retro_serialize_size()==bound&&retro_serialize(buffer,bound));
            writer.file=raw_file(0);assert(writer.file);uint8_t byte=(uint8_t)(cycle^0xa5u);
            assert(writer.seek(writer.file,101u*1024u,RETRO_VFS_SEEK_POSITION_START)>=0&&writer.write(writer.file,&byte,1u)==1u&&writer.flush(writer.file));
            unsigned notices=total_osd_calls;assert(!retro_unserialize(buffer,bound)&&total_osd_calls>notices);
            assert(!remove(path));
         }
         assert(disk_control.set_eject_state(true));char larger[4096];
         snprintf(larger,sizeof(larger),"%s",argv[3]);char *slash=strrchr(larger,'/');assert(slash);*slash=0;
         size_t prefix=strlen(larger);assert(snprintf(larger+prefix,sizeof(larger)-prefix,"/hd-family/valid-f.adf")<(int)(sizeof(larger)-prefix));
         struct retro_game_info replacement={.path=larger};assert(disk_control.replace_image_index(0,&replacement));
         assert(disk_control.set_eject_state(false));retro_run();unsigned notices=total_osd_calls;
         assert(retro_serialize_size()==bound&&!retro_serialize(buffer,bound)&&total_osd_calls>notices);
         notices=total_osd_calls;assert(!retro_unserialize(buffer,bound)&&total_osd_calls>notices);
         retro_run();assert(retro_serialize_size()==bound);free(buffer);retro_unload_game();
         puts("SS1 replacement: 72 swaps retain write frontiers; larger media runs with a stable bound and notice-refused states");continue;
      }
      if(!strcmp(argv[5],"--ss1-secondary")) {
         struct retro_disk_control_ext_callback primary_disk=disk_control;
         void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");
         bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");assert(persist&&write_cmos);
         assert(write_cmos(216u,0x42));persist();uint64_t before=ss1_native_mutations;
         /* Default secondary creation, cheat set/reset and failed-preempt
          * re-entry all use a copied core and the same init/load lifecycle. */
         for(unsigned route=0;route<3u;++route) {
            char copy[4096];snprintf(copy,sizeof(copy),"%s/secondary-%u.dylib",data_root,route);copy_file(argv[1],copy);
            void *secondary=dlopen(copy,RTLD_NOW|RTLD_LOCAL);assert(secondary&&secondary!=module);
#define SS1_SECONDARY(name,signature) signature=dlsym(secondary,#name);assert(name)
            SS1_SECONDARY(retro_set_environment,void (*retro_set_environment)(retro_environment_t));
            SS1_SECONDARY(retro_init,void (*retro_init)(void));
            SS1_SECONDARY(retro_load_game,bool (*retro_load_game)(const struct retro_game_info *));
            SS1_SECONDARY(retro_serialize_size,size_t (*retro_serialize_size)(void));
            SS1_SECONDARY(retro_serialize,bool (*retro_serialize)(void *,size_t));
            SS1_SECONDARY(retro_unserialize,bool (*retro_unserialize)(const void *,size_t));
            SS1_SECONDARY(retro_run,void (*retro_run)(void));
            SS1_SECONDARY(retro_reset,void (*retro_reset)(void));
            SS1_SECONDARY(retro_cheat_set,void (*retro_cheat_set)(unsigned,bool,const char *));
            SS1_SECONDARY(retro_cheat_reset,void (*retro_cheat_reset)(void));
            SS1_SECONDARY(retro_unload_game,void (*retro_unload_game)(void));
            SS1_SECONDARY(retro_deinit,void (*retro_deinit)(void));
#undef SS1_SECONDARY
            retro_set_environment(ss1_secondary_environment);retro_init();
            assert(!retro_load_game(&game)&&!retro_serialize_size());uint8_t poison[64];memset(poison,0xa7,sizeof(poison));
            assert(!retro_serialize(poison,sizeof(poison))&&!retro_unserialize(poison,sizeof(poison)));
            retro_run();retro_reset();retro_cheat_set(0,true,"authored");retro_cheat_reset();retro_unload_game();retro_deinit();
            assert(!ss1_secondary_calls&&ss1_secondary_log_queries&&!ss1_context_queries&&before==ss1_native_mutations);
            assert(!memcmp(&primary_disk,&disk_control,sizeof(primary_disk)));assert(!dlclose(secondary));assert(!remove(copy));
         }
         retro_run();assert(write_cmos(216u,0x52));persist();assert(ss1_native_mutations>before);
         puts("SS1 secondary: default, cheats and failed-preempt copied modules inert before registration/load; primary persists");
         retro_unload_game();continue;
      }
      if(!strcmp(argv[5],"--ss1-publish-failure")) {
         bool (*pending)(void)=dlsym(module,"arc_state_io_pending");
         bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
         void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");assert(pending&&write_cmos&&persist);
         retro_run();size_t bound=retro_serialize_size();uint8_t *buffer=malloc(bound);assert(buffer&&retro_serialize(buffer,bound));
         retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);
         retro_run();assert(write_cmos(216u,0x64u));persist();assert(pending());
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
         /* Restored callbacks cannot turn speculative bytes into a new save. */
         uint64_t native=ss1_native_mutations;uint8_t cpu_before[1024],cpu_after[1024];
         size_t cpu_n=cpu_snapshot(cpu_before,sizeof(cpu_before));
         uint8_t *poison=malloc(bound);assert(poison);memset(poison,0xa7,bound);
         assert(!retro_serialize(poison,bound));
         for(size_t i=0;i<bound;++i)assert(poison[i]==0xa7);
         assert(cpu_snapshot(cpu_after,sizeof(cpu_after))==cpu_n&&!memcmp(cpu_before,cpu_after,cpu_n));
         assert(ss1_native_mutations==native&&pending());free(poison);
         assert(retro_unserialize(buffer,bound)&&!pending());
         puts("SS1 speculative save: restored callbacks refuse capture without output/guest/store changes; rollback remains available");
         retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);
         retro_run();assert(write_cmos(216u,0x65u));persist();assert(pending());
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
         /* R-SS4e: failed rollback never finalizes speculation. */
         cpu_n=cpu_snapshot(cpu_before,sizeof(cpu_before));
         uint8_t bad[40]={0};assert(!retro_unserialize(bad,sizeof(bad)));
         for(unsigned i=0;i<4u;++i)retro_run();
         retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);
         for(unsigned i=0;i<4u;++i)retro_run();
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
         assert(cpu_snapshot(cpu_after,sizeof(cpu_after))==cpu_n&&!memcmp(cpu_before,cpu_after,cpu_n));
         assert(ss1_native_mutations==native&&pending());
         assert(!retro_unserialize(buffer,bound));
         retro_reset();retro_run();assert(retro_serialize(buffer,bound));
         for(unsigned early=0;early<2u;++early) {
            retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);retro_run();
            assert(write_cmos(216u,(uint8_t)(0x66u+early)));persist();assert(pending());
            retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
            cpu_n=cpu_snapshot(cpu_before,sizeof(cpu_before));native=ss1_native_mutations;
            assert(!retro_unserialize(early?buffer:NULL,early?0u:bound));
            assert(!retro_unserialize(buffer,bound)); /* Only reset/reload may recover. */
            retro_set_input_poll(ss1_fake_poll);retro_run();retro_set_input_poll(poll);retro_run();
            assert(cpu_snapshot(cpu_after,sizeof(cpu_after))==cpu_n&&!memcmp(cpu_before,cpu_after,cpu_n));
            assert(ss1_native_mutations==native&&pending());
            retro_reset();retro_run();assert(retro_serialize(buffer,bound));
         }
         retro_unload_game();free(buffer);
         puts("SS1 failed rollback: real and fake polls cannot execute or publish until reset; reset recovers");continue;
      }
      if(!strcmp(argv[5],"--ss1-preempt")) {
         bool (*pending)(void)=dlsym(module,"arc_state_io_pending");
         bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
         void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");assert(pending&&write_cmos&&persist);
         size_t bound=retro_serialize_size();assert(bound);uint8_t *buffer=malloc(bound);assert(buffer);
         /* preempt_init runs a first real frame, allocates once and installs
          * its fixed callbacks. First save fails before preempt's real run. */
         retro_run();assert(retro_serialize(buffer,bound));
         for(unsigned transition=0;transition<3u;++transition) {
            retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_preempt_input);
            unsigned notices=total_osd_calls;uint64_t native=ss1_native_mutations;
            assert(retro_serialize_size()==bound&&!retro_serialize(buffer,bound));
            assert(total_osd_calls==notices+(transition==0u)&&strstr(last_message,"Preemptive Frames"));
            assert(!pending()&&ss1_native_mutations==native);
            /* preempt_deinit restores original callbacks and re-enables
             * run-ahead/secondary. No zero-size latch or frozen primary. */
            retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
            retro_run();assert(write_cmos(216u,(uint8_t)(0x60u+transition)));persist();
            assert(!pending()&&ss1_native_mutations>native&&retro_serialize_size()==bound);
            assert(retro_serialize(buffer,bound));
         }
         assert(!ss1_context_queries);free(buffer);retro_unload_game();
         puts("SS1 preempt: refused before first real frame, repeated mode changes/failure cleanup retain eager final writes");continue;
      }
      size_t session_capacity=output_only?0u:retro_serialize_size();
      if(!output_only)assert(session_capacity);
      int *ram_kib=dlsym(module,"realmemsize");assert(ram_kib);int measured_ram=*ram_kib;
      assert(measured_ram==4096);
      size_t bytes=0;uint8_t *saved=NULL,*again=NULL;
      uint64_t frames[12],pcm[12];uint8_t cpus[12][1024];size_t cpu_size=0;
      FILE *trace=NULL;
      uint64_t serialize_ns=0,unserialize_ns=0,first_load_ns=0;
      unsigned picture_differences=0;uint64_t guest_hashes[12]={0};
      if(consume) {
         saved=ss1_read_state(state_path,&bytes);again=malloc(bytes);assert(again);
         struct timespec start;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
         assert(retro_unserialize(saved,bytes));unserialize_ns=first_load_ns=ss1_elapsed_ns(start);
         if(!retro_serialize(again,bytes)){fputs("SS1 canonical serialization failed after successful restore\n",stderr);abort();}
         if(memcmp(saved,again,bytes)) {
            for(size_t at=40u;at<(size_t)ss1_get64(saved+16);at+=24u+(size_t)ss1_get64(saved+at+16)) {
               size_t n=24u+(size_t)ss1_get64(saved+at+16);
               if(memcmp(saved+at,again+at,n)) {fprintf(stderr,"SS1 canonical record key=%llx kind=%llu bytes=%zu saved-used=%llu again-used=%llu\n",(unsigned long long)ss1_get64(saved+at),(unsigned long long)ss1_get64(saved+at+8),n,(unsigned long long)ss1_get64(saved+16),(unsigned long long)ss1_get64(again+16));break;}
            }
         }
         assert(!memcmp(saved,again,bytes));
         assert(!clock_gettime(CLOCK_MONOTONIC,&start));
         /* Measure the public core call; exhaustive table verification is
          * harness work and runs outside this timing interval. */
         for(unsigned sample=0;sample<ss1_cost_samples;++sample)assert(ss1_core_restore(saved,bytes));
         unserialize_ns=ss1_elapsed_ns(start)/ss1_cost_samples;
         assert(ss1_tables_check());
         assert(retro_serialize(again,bytes));assert(!memcmp(saved,again,bytes));
         if(!strcmp(argv[5],"--ss1-publish")) {
            const char *phase=getenv("ARCDROID_SS1_KILL_PHASE");ss1_kill_phase=phase?(unsigned)strtoul(phase,NULL,10):0;
            bool (*publish)(void)=dlsym(module,"arc_state_io_publish");assert(publish&&publish());
            ss1_kill_phase=0;retro_unload_game();free(saved);free(again);continue;
         }
         trace=fopen(trace_path,"rb");assert(trace);
      } else {
         for(unsigned i=0;i<96;++i){
            ss1_set_input(i);retro_run();if(!output_only)assert(retro_serialize_size()==session_capacity);
            if(i==30u&&!strcmp(media_class,"jfd-swap")) {
               bool (*commit)(unsigned,size_t,uint8_t)=dlsym(module,"arc_libretro_test_jfd_commit");
               assert(commit&&commit(0,0,0x4du));
               assert(disk_control.set_eject_state(true));assert(disk_control.set_image_index(1));assert(disk_control.set_eject_state(false));
            }
         }
         if(!output_only) {
            if(machine_index==0u) {
               void (*latch_a)(uint8_t)=dlsym(module,"wd1770_writelatch_a");
               void (*latch_b)(uint8_t)=dlsym(module,"wd1770_writelatch_b");
               void (*write_reg)(uint16_t,uint8_t)=dlsym(module,"wd1770_write");
               assert(latch_a&&latch_b&&write_reg);latch_a(0x1eu);latch_b(0);write_reg(4,0);write_reg(8,0);write_reg(0,0x80u);
            } else {
               bool (*read_start)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_newio_read_start");assert(read_start&&read_start(0,3));
            }
            bool (*advance)(unsigned)=dlsym(module,"arc_engine_test_advance_timers");assert(advance&&advance(80));
            const char *history_hooks[]={"arc_engine_test_seed_raster_history","arc_platform_test_seed_video_history","arc_libretro_test_seed_present_history","arc_engine_test_seed_raw_cache"};
            for(unsigned h=0;h<4u;++h){void (*seed)(void)=dlsym(module,history_hooks[h]);assert(seed);seed();}
            /* Authored retained-register and bus fixtures also cover imported
             * conservative state which the small boot ROM does not exercise. */
            double *fp=dlsym(module,"fparegs");assert(fp);fp[0]=1.5;
            void (*cp)(int,uint32_t)=dlsym(module,"writecp15");assert(cp);cp(3,0x120u);
            void (*onewire)(int)=dlsym(module,"ds2401_write");assert(onewire);onewire(1);onewire(0);
            void (*ioeb)(uint32_t,uint8_t)=dlsym(module,"ioeb_write");assert(ioeb);ioeb(0x48u,4u);
            void (*busy)(int)=dlsym(module,"printer_set_busy");assert(busy);busy(1);
            if(machine_index) {
               void (*superio)(uint32_t,uint8_t)=dlsym(module,"c82c711_write");assert(superio);
               superio(0x3f0u<<2,0x55u);superio(0x3f0u<<2,0x55u);
               superio(0x3f0u<<2,7u);superio(0x3f1u<<2,0x93u);
            }
            void (*watch_arm)(void)=dlsym(module,"arc_engine_boot_watch_arm");assert(watch_arm);watch_arm();
            if(!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty")) {
               void *ide=dlsym(module,"ide_internal");void (*write_ide)(void*,uint32_t,uint8_t)=dlsym(module,"writeide");assert(ide&&write_ide);
               write_ide(ide,0x1f2,2);write_ide(ide,0x1f3,1);write_ide(ide,0x1f6,0);write_ide(ide,0x1f7,0x30);
               for(unsigned j=0;j<117u;++j)write_ide(ide,0x1f0,(uint8_t)(j*7u));
            }
            if(strcmp(argv[5],"--ss1-features")&&(!strcmp(media_class,"jfd-shadow")||!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty")||
               !strcmp(media_class,"adf-shadow")||!strcmp(media_class,"adf-original"))) {
               void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");assert(speculate);speculate(true);
               if(!strcmp(media_class,"jfd-shadow")) {
                  bool (*commit)(unsigned,size_t,uint8_t)=dlsym(module,"arc_libretro_test_jfd_commit");assert(commit&&commit(0,0,0xa6u));
               } else if(!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty")) {
                  bool (*write_hd)(unsigned)=dlsym(module,"arc_libretro_test_hd_observation_write");assert(write_hd&&write_hd(!strcmp(media_class,"hd-dirty")?5u:1u));
               } else {
                  const char *(*staged)(unsigned)=dlsym(module,"arc_libretro_test_staged_path");assert(staged);
                  void *(*open_file)(const char*,bool)=dlsym(module,"arc_io_open_file");
                  int64_t (*seek_file)(void*,int64_t,int)=dlsym(module,"arc_io_seek_file");
                  size_t (*write_file)(void*,const void*,size_t)=dlsym(module,"arc_io_write_file");
                  bool (*close_file)(void*)=dlsym(module,"arc_io_close_file");assert(open_file&&seek_file&&write_file&&close_file);
                  void *f=open_file(staged(0),true);uint8_t payload[512];memset(payload,0x9d,sizeof(payload));
                  assert(f&&seek_file(f,100u*1024u,RETRO_VFS_SEEK_POSITION_START)>=0&&write_file(f,payload,sizeof(payload))==sizeof(payload)&&close_file(f));
               }
            }
            if(!strcmp(media_class,"cmos-pending")) {
               void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");
               bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
               void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");
               assert(speculate&&write_cmos&&persist);speculate(true);assert(write_cmos(216u,0x97u));persist();
            }
            bytes=retro_serialize_size();assert(bytes==session_capacity);saved=malloc(bytes);again=malloc(bytes);assert(saved&&again);
            struct timespec start;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
            assert(retro_serialize(saved,bytes));serialize_ns=ss1_elapsed_ns(start);
            assert(!clock_gettime(CLOCK_MONOTONIC,&start));
            for(unsigned sample=0;sample<ss1_cost_samples;++sample){assert(retro_serialize_size()==bytes);assert(retro_serialize(saved,bytes));}
            serialize_ns=ss1_elapsed_ns(start)/ss1_cost_samples;
            if(produce) {
               ss1_write_state(state_path,saved,bytes);
               char checkpoint[4096];snprintf(checkpoint,sizeof(checkpoint),"%s/checkpoint-save",data_root);
               ss1_copy_tree(save_path,checkpoint);
               if(!strcmp(media_class,"adf-original")){snprintf(checkpoint,sizeof(checkpoint),"%s/checkpoint-original.adf",data_root);copy_file(game.path,checkpoint);}
            }
         }
         if(produce||output_only){trace=fopen(trace_path,"wb");assert(trace);}
      }
      if(!strcmp(argv[5],"--ss1-compat")) {
         assert(ss1_quirks_calls==1u&&!(ss1_quirks&RETRO_SERIALIZATION_QUIRK_INCOMPLETE)&&!ss1_context_queries);
         assert(ss1_quirks&RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE);
         assert(retro_unserialize(saved,bytes)&&retro_serialize(again,bytes)&&!memcmp(saved,again,bytes));
         puts("SS1 context-free boundary: complete save/load without secondary-creating context queries");
         retro_unload_game();free(saved);free(again);continue;
      }
      if(!strcmp(argv[5],"--ss1-boundary")) {
         void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");
         bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
         bool (*pending)(void)=dlsym(module,"arc_state_io_pending");assert(persist&&write_cmos&&pending);
         ss1_context=RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;
         /* A failed snapshot must not arm speculative publication. A frontend
          * abandoning run-ahead after buffer refusal still runs real frames. */
         memset(again,0xa7,40u);assert(!retro_serialize(again,40u));
         for(unsigned b=0;b<40u;++b)assert(again[b]==0xa7u);
         uint64_t mutations=ss1_native_mutations;
         retro_run();assert(write_cmos(216u,0x39u));persist();
         assert(!pending()&&ss1_native_mutations>mutations);
         size_t needed=retro_serialize_size();assert(needed);
         assert(needed==bytes&&bytes==session_capacity);
         assert(retro_serialize(saved,bytes));
         mutations=ss1_native_mutations;
         retro_run();assert(write_cmos(216u,0x49u));persist();
         assert(!pending()&&mutations<ss1_native_mutations);
         /* Every original-callback frame is final and publishes eagerly.
          * Switching callbacks never delays or publishes a real-frame queue. */
         retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);
         assert(!pending()&&ss1_native_mutations>mutations);
         mutations=ss1_native_mutations;retro_run();assert(write_cmos(216u,0x59u));persist();
         assert(pending()&&mutations==ss1_native_mutations);
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
         unsigned notices=total_osd_calls;assert(!retro_unserialize(saved,bytes));
         assert(total_osd_calls>notices&&mutations==ss1_native_mutations);
         ss1_context=RETRO_SAVESTATE_CONTEXT_NORMAL;
         retro_unload_game();assert(mutations==ss1_native_mutations);
         puts("SS1 boundary: real effects eager, speculative effects staged, stale restore and unload never publish them");
         free(saved);free(again);continue;
      }
      if(!strcmp(argv[5],"--ss1-store")) {
         if(!strcmp(media_class,"adf")) {
            bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");assert(write_cmos&&write_cmos(216u,0x97u));
            retro_run();
         } else if(!strcmp(media_class,"jfd-swap")) {
            assert(disk_control.set_eject_state(true));assert(disk_control.set_image_index(0));assert(disk_control.set_eject_state(false));
            bool (*commit)(unsigned,size_t,uint8_t)=dlsym(module,"arc_libretro_test_jfd_commit");assert(commit&&commit(0,0,0x65u));
            assert(disk_control.set_eject_state(true));assert(disk_control.set_image_index(1));assert(disk_control.set_eject_state(false));
         } else {
            void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");
            bool (*publish)(void)=dlsym(module,"arc_state_io_publish");assert(speculate&&publish);
            speculate(false);assert(publish());
         }
         size_t current_bytes=retro_serialize_size();assert(current_bytes==bytes&&bytes==session_capacity);
         assert(retro_serialize(again,current_bytes));
         uint8_t *current=malloc(current_bytes);assert(current);memcpy(current,again,current_bytes);
         for(unsigned attempt=0;attempt<2u;++attempt) {
            unsigned notices=total_osd_calls;uint64_t mutations=ss1_native_mutations;assert(!retro_unserialize(saved,bytes));
            assert(mutations==ss1_native_mutations);
            assert(total_osd_calls>notices&&strstr(last_message,"save state"));
            assert(retro_serialize(again,current_bytes));assert(!memcmp(current,again,current_bytes));
         }
         free(current);retro_run();
         puts("SS1 store: eager publication, stale load and rewind refused twice without changing state");
         retro_unload_game();free(saved);free(again);continue;
      }
      if(!strcmp(argv[5],"--ss1-refusals")) {
         const char *identities[]={"identity.build","identity.schema","identity.platform","identity.engine","identity.abi","identity.machine","identity.firmware",
            "identity.options","identity.media-count","identity.media.0","frontier.hd","frontier.jfd"};
         unsigned refused=0;
         for(unsigned test=0;test<sizeof(identities)/sizeof(identities[0])+26u;++test) {
            memcpy(again,saved,bytes);size_t length=bytes;
            if(test<sizeof(identities)/sizeof(identities[0])) {
               size_t at=ss1_record(again,identities[test],5);assert(at&&ss1_get64(again+at+16));
               again[at+24]^=0x40u;ss1_checksum(again);
            } else switch(test-sizeof(identities)/sizeof(identities[0])) {
               case 0:again[0]^=1;break;
               case 1:again[8]^=1;break;
               case 2:again[32]^=1;break;
               case 3:length=39;break;
               case 4:again[bytes-1]=1;break;
               case 5:{size_t at=ss1_record(again,NULL,2);assert(at);memset(again+at+24,0xff,8);ss1_checksum(again);break;}
               case 6:ss1_put64(again+56,UINT64_MAX);ss1_checksum(again);break;
               case 7:{size_t at=ss1_compressed_record(again);assert(at);ss1_put64(again+at+32,0);ss1_checksum(again);break;}
               case 8:{size_t at=ss1_compressed_record(again);assert(at);ss1_put64(again+at+24,UINT64_MAX);ss1_checksum(again);break;}
               case 9:{size_t second=64u+(size_t)ss1_get64(again+56);assert(second+24u<bytes);memcpy(again+second,again+40,8);ss1_checksum(again);break;}
               case 10:{
                  uint8_t field[24];ss1_put64(field,ss1_key("libretro/arcdroid_libretro.c:arc_firmware_size"));
                  ss1_put64(field+8,0);ss1_put64(field+16,sizeof(size_t));
                  uint64_t key=UINT64_C(14695981039346656037);for(unsigned b=0;b<24u;++b)key=(key^field[b])*UINT64_C(1099511628211);
                  size_t at=40u;while(at<(size_t)ss1_get64(again+16)&&ss1_get64(again+at)!=key)at+=24u+(size_t)ss1_get64(again+at+16);
                  assert(at<(size_t)ss1_get64(again+16));memset(again+at+24,0xff,sizeof(size_t));ss1_checksum(again);break;
               }
               default:{
                  unsigned index=test-(unsigned)(sizeof(identities)/sizeof(identities[0]))-11u;
                  const char *names[]={"libretro/arcdroid_libretro.c:arc_audio_fraction",
                     "libretro/arcdroid_libretro.c:arc_stick_fraction_x",
                     "libretro/arcdroid_libretro.c:arc_stick_fraction_y"};
                  const long double bad[]={__builtin_nanl(""),__builtin_infl(),-__builtin_infl(),-1.0L,1.0L};
                  assert(index<15u);uint8_t field[24];ss1_put64(field,ss1_key(names[index/5u]));
                  ss1_put64(field+8,0);ss1_put64(field+16,sizeof(long double));
                  uint64_t key=UINT64_C(14695981039346656037);for(unsigned b=0;b<24u;++b)key=(key^field[b])*UINT64_C(1099511628211);
                  size_t at=40u;while(at<(size_t)ss1_get64(again+16)&&ss1_get64(again+at)!=key)at+=24u+(size_t)ss1_get64(again+at+16);
                  assert(at<(size_t)ss1_get64(again+16)&&ss1_get64(again+at+16)==sizeof(long double));
                  memcpy(again+at+24,&bad[index%5u],sizeof(long double));ss1_checksum(again);break;
               }
            }
            unsigned notices=total_osd_calls;last_message[0]=0;uint64_t mutations=ss1_native_mutations;
            assert(!retro_unserialize(again,length));assert(mutations==ss1_native_mutations);
            assert(total_osd_calls>notices&&strstr(last_message,"save state"));
            assert(retro_serialize(again,bytes));assert(!memcmp(saved,again,bytes));++refused;
         }
         printf("SS1 refusal matrix: %u rejected, notice and unchanged canonical session verified\n",refused);
         /* Oversized out-of-run captures refuse before output/frontier/native
          * mutation. The runtime queue may exceed the saved-log allowance. */
         void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");
         void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");
         bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
         assert(speculate&&persist&&write_cmos);speculate(true);
         for(unsigned i=0;i<300u;++i){assert(write_cmos(216u,(uint8_t)i));persist();}
         uint64_t mutations=ss1_native_mutations;memset(again,0x69,bytes);
         assert(!retro_serialize(again,bytes)&&mutations==ss1_native_mutations);
         for(size_t i=0;i<bytes;++i)assert(again[i]==0x69);
         assert(retro_unserialize(saved,bytes)&&retro_serialize(again,bytes)&&!memcmp(saved,again,bytes));
         puts("SS1 saved-log refusal: oversized queue leaves output, guest and native stores unchanged");
         retro_unload_game();free(saved);free(again);continue;
      }
      uint64_t cmos_generation_at_k=0;
      bool (*cmos_generation)(const char*,uint64_t*)=dlsym(module,"arc_state_io_test_generation");
      const char *(*cmos_path)(void)=dlsym(module,"arc_libretro_test_cmos_path");
      if(!strcmp(media_class,"cmos-pending"))assert(cmos_generation&&cmos_path&&cmos_generation(cmos_path(),&cmos_generation_at_k));
      for(unsigned i=0;i<12;++i) {
         if(i==6u&&!strcmp(media_class,"jfd-swap")){
            retro_reset();assert(retro_serialize_size()==session_capacity);
            if(!output_only){void (*seed_clock)(void)=dlsym(module,"arc_engine_test_rtc_seed");assert(seed_clock);seed_clock();}
         }
         ss1_set_input(96u+i);poweron_pcm_hash=UINT64_C(1469598103934665603);poweron_trace=true;
         retro_run();frames[i]=last_video_signature;pcm[i]=poweron_pcm_hash;
         if(!output_only){assert(retro_serialize(again,bytes));guest_hashes[i]=ss1_get64(again+32);}

         memset(cpus[i],0,sizeof(cpus[i]));if(cpu_snapshot){cpu_size=cpu_snapshot(cpus[i],sizeof(cpus[i]));assert(cpu_size<=sizeof(cpus[i]));}
         if(trace) {
            if(consume) {
               uint64_t video_hash=ss1_read_u64(trace),pcm_hash=ss1_read_u64(trace),guest_hash=ss1_read_u64(trace),expected_cpu_size=ss1_read_u64(trace);
               picture_differences+=video_hash!=frames[i];
               uint8_t expected_cpu[1024];assert(expected_cpu_size<=sizeof(expected_cpu));assert(fread(expected_cpu,1,(size_t)expected_cpu_size,trace)==expected_cpu_size);
               if(guest_hash!=guest_hashes[i]||pcm_hash!=pcm[i]||expected_cpu_size!=cpu_size||memcmp(expected_cpu,cpus[i],cpu_size)) {
                  fprintf(stderr,"SS1 fresh-process mismatch class=%s machine=%s frame=%u video=%u pcm=%u cpu=%u\n",media_class,option_machine,i,video_hash!=frames[i],pcm_hash!=pcm[i],memcmp(expected_cpu,cpus[i],cpu_size)!=0);abort();
               }
            } else {
               ss1_write_u64(trace,frames[i]);ss1_write_u64(trace,pcm[i]);ss1_write_u64(trace,guest_hashes[i]);ss1_write_u64(trace,cpu_size);assert(fwrite(cpus[i],1,cpu_size,trace)==cpu_size);
            }
         }
      }
      if(!strcmp(media_class,"cmos-pending")) {
         uint64_t generation=0;assert(cmos_generation(cmos_path(),&generation));
         if(generation!=cmos_generation_at_k+1u)fprintf(stderr,"SS1 CMOS publication frontier: before=%llu after=%llu\n",(unsigned long long)cmos_generation_at_k,(unsigned long long)generation);
         assert(generation==cmos_generation_at_k+1u);
      }
      if(trace){if(consume)assert(fgetc(trace)==EOF);assert(!fclose(trace));}
      if(!strcmp(argv[5],"--ss1-features")) {
         /* Rewind through public APIs without restarting the instance. */
         assert(retro_unserialize(saved,bytes));
         for(unsigned i=0;i<12u;++i) {
            ss1_set_input(96u+i);poweron_pcm_hash=UINT64_C(1469598103934665603);retro_run();
            uint8_t cpu[1024]={0};assert(cpu_snapshot(cpu,sizeof(cpu))==cpu_size);
            picture_differences+=frames[i]!=last_video_signature;
            assert(retro_serialize(again,bytes)&&ss1_get64(again+32)==guest_hashes[i]);
            assert(pcm[i]==poweron_pcm_hash&&!memcmp(cpu,cpus[i],cpu_size));
         }
         assert(retro_unserialize(saved,bytes));
         ss1_context=RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;
         for(unsigned i=0;i<12u;++i) {
            /* RetroArch 1.22.2 keeps its initial buffers for the session. */
            assert(retro_serialize_size()==bytes);
            assert(retro_serialize(saved,bytes));
            /* RetroArch 1.22.2 switches both callbacks around each fake frame,
             * including its final frame with A/V enabled. */
            for(unsigned ahead=0;ahead<2u;++ahead) {
               retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);
               uint64_t mutations=ss1_native_mutations;
               ss1_set_input(96u+i+ahead);retro_run();
               if(!strcmp(media_class,"jfd-shadow")) {
                  bool (*commit)(unsigned,size_t,uint8_t)=dlsym(module,"arc_libretro_test_jfd_commit");assert(commit&&commit(0,0,(uint8_t)(0x71u+i)));
               }
               bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
               void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");assert(write_cmos&&persist);
               assert(write_cmos(216u,(uint8_t)(0x80u+i)));persist();
               assert(mutations==ss1_native_mutations);
               retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
            }
            assert(retro_unserialize(saved,bytes));
            assert(retro_serialize(again,bytes));assert(!memcmp(saved,again,bytes));
            ss1_set_input(96u+i);poweron_pcm_hash=UINT64_C(1469598103934665603);retro_run();
            uint8_t cpu[1024]={0};assert(cpu_snapshot(cpu,sizeof(cpu))==cpu_size);
            picture_differences+=frames[i]!=last_video_signature;
            assert(retro_serialize(again,bytes)&&ss1_get64(again+32)==guest_hashes[i]);
            assert(pcm[i]==poweron_pcm_hash&&!memcmp(cpu,cpus[i],cpu_size));
         }
         assert(!ss1_context_queries);
         puts("SS1 retro_*: 12 rewind frames and 24 speculative frames preserve guest/PCM/CPU; picture differences reported separately");
      } else if(!consume&&!produce&&!output_only) {
         retro_unload_game();phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
         {struct retro_system_av_info av;retro_get_system_av_info(&av);geometry_width=av.geometry.base_width;geometry_height=av.geometry.base_height;current_reported_rate=last_notified_rate=av.timing.fps;}
         struct timespec start;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
         assert(retro_unserialize(saved,bytes));unserialize_ns=ss1_elapsed_ns(start);
         assert(retro_serialize(again,bytes));
         if(memcmp(saved,again,bytes)) {size_t at=0;while(at<bytes&&saved[at]==again[at])++at;fprintf(stderr,"SS1 canonical mismatch class=%s machine=%s offset=%zu\n",media_class,option_machine,at);abort();}
         for(unsigned i=0;i<12;++i) {
            uint8_t cpu[1024]={0};ss1_set_input(96u+i);poweron_pcm_hash=UINT64_C(1469598103934665603);
            retro_run();assert(cpu_snapshot(cpu,sizeof(cpu))==cpu_size);
            picture_differences+=last_video_signature!=frames[i];
            assert(retro_serialize(again,bytes));
            if(ss1_get64(again+32)!=guest_hashes[i]||poweron_pcm_hash!=pcm[i]||memcmp(cpu,cpus[i],cpu_size)) {
               fprintf(stderr,"SS1 differential mismatch class=%s machine=%s frame=%u video=%u pcm=%u cpu=%u\n",media_class,option_machine,i,last_video_signature!=frames[i],poweron_pcm_hash!=pcm[i],memcmp(cpu,cpus[i],cpu_size)!=0);abort();
            }
         }
      }
      size_t exact=saved?(size_t)ss1_get64(saved+16):0u;
      retro_unload_game();free(saved);free(again);
      printf("SS1 %s: class=%s machine=%s bytes=%zu exact=%zu serialize-ns=%llu unserialize-ns=%llu first-load-ns=%llu ram-kib=%d picture-different-frames=%u passed\n",argv[5],media_class,option_machine,bytes,exact,(unsigned long long)serialize_ns,(unsigned long long)unserialize_ns,(unsigned long long)first_load_ns,measured_ram,picture_differences);
   }
   poweron_trace=false;retro_deinit();assert(!open_files&&!open_dirs);assert(dlclose(module)==0);return 0;
}
