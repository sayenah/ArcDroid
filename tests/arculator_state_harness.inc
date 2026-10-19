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
   if(!output_only)assert(cpu_snapshot);
   bool produce=!strcmp(argv[5],"--ss1-produce"),consume=!strcmp(argv[5],"--ss1-consume")||!strcmp(argv[5],"--ss1-publish");
   assert(argc==6||argc==7);
   const char *machines[]={"A3000","A3010","A3020","A4000"};
   const char *media_class=getenv("ARCDROID_SS1_CLASS");if(!media_class)media_class="adf";
   option_writes="off";option_launch="off";
   if(!strcmp(media_class,"hd")||!strcmp(media_class,"hd-jfd")||!strcmp(media_class,"hd-dirty")){option_hard_disc="auto";option_writes="shadow";}
   if(!strcmp(media_class,"adffs"))option_launch="auto";
   if(!strcmp(media_class,"adf-shadow"))option_writes="shadow";
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
      {struct retro_system_av_info av;retro_get_system_av_info(&av);geometry_width=av.geometry.base_width;geometry_height=av.geometry.base_height;current_reported_rate=last_notified_rate=av.timing.fps;}
      if(!output_only){memset(ss1_last_input,0,sizeof(ss1_last_input));retro_set_input_state(ss1_logged_input);}
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
         (void)ss1_write_batch(&current); /* First-touch and stdio warm-up. */
         for(unsigned i=0;i<15u;++i){
            if(comparison)reference[i*2u]=ss1_write_batch(&main_path);
            ss1_native_read_bytes=ss1_native_write_bytes=0;samples[i]=ss1_write_batch(&current);
            assert(!ss1_native_read_bytes&&ss1_native_write_bytes==256u*512u);
            if(comparison)reference[i*2u+1u]=ss1_write_batch(&main_path);
         }
         qsort(samples,15u,sizeof(*samples),ss1_ns_order);
         printf("SS1 no-state sector IO: machine=%s medium=%llu bytes read=0 written=512 per sector; p50=%llu ns",option_machine,(unsigned long long)digest_file(staged(0)).size,(unsigned long long)samples[7]);
         if(comparison){
            qsort(reference,30u,sizeof(*reference),ss1_ns_order);
            uint64_t band=reference[26]-reference[3];
            printf(" main-p50=%llu ns main-p10/p90=%llu/%llu ns noise=%llu ns\n",(unsigned long long)reference[15],(unsigned long long)reference[3],(unsigned long long)reference[26],(unsigned long long)band);fflush(stdout);
            assert(samples[7]<=reference[26]+band);assert(close_main(main_path.file));assert(!dlclose(comparison));
         }else puts(" (main time comparison not requested)");
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
            assert(!ss1_secondary_calls&&!ss1_context_queries&&before==ss1_native_mutations);
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
         /* Model a failed restore: the running machine is retained, then the
          * next original-callback frame makes it final. Its publish fails. */
         uint8_t bad[40]={0};assert(!retro_unserialize(bad,sizeof(bad)));
         fault_write_substring="/state-stores/";unsigned notices=total_osd_calls;
         retro_run();
         if(fault_write_substring||!pending()||total_osd_calls!=notices+1u)fprintf(stderr,"publication failure: fault=%u pending=%u notices=%u last=%s\n",fault_write_substring!=NULL,pending(),total_osd_calls-notices,last_message);
         assert(!fault_write_substring&&pending()&&total_osd_calls==notices+1u);
         uint64_t native=ss1_native_mutations;
         for(unsigned i=0;i<4u;++i){retro_run();assert(write_cmos(216u,(uint8_t)(0x74u+i)));persist();}
         assert(ss1_native_mutations==native&&pending()&&total_osd_calls==notices+1u);
         retro_unload_game();assert(ss1_native_mutations==native&&total_osd_calls==notices+1u);free(buffer);
         puts("SS1 publication failure: one notice, later real-frame writes cannot overtake the failed queue, unload fail-closed");continue;
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
            assert(total_osd_calls>notices&&strstr(last_message,"Preemptive Frames"));
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
         for(unsigned sample=0;sample<ss1_cost_samples;++sample)assert(retro_unserialize(saved,bytes));
         unserialize_ns=ss1_elapsed_ns(start)/ss1_cost_samples;
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
         puts("SS1 boundary: real effects eager, speculative effects staged, stale restore refused");
         retro_unload_game();free(saved);free(again);continue;
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
         retro_unload_game();free(saved);free(again);continue;
      }
      uint64_t cmos_generation_at_k=0;
      bool (*cmos_generation)(const char*,uint64_t*)=dlsym(module,"arc_state_io_test_generation");
      const char *(*cmos_path)(void)=dlsym(module,"arc_libretro_test_cmos_path");
      if(!strcmp(media_class,"cmos-pending"))assert(cmos_generation&&cmos_path&&cmos_generation(cmos_path(),&cmos_generation_at_k));
      for(unsigned i=0;i<12;++i) {
         if(i==6u&&!strcmp(media_class,"jfd-swap")){retro_reset();assert(retro_serialize_size()==session_capacity);}
         ss1_set_input(96u+i);poweron_pcm_hash=UINT64_C(1469598103934665603);poweron_trace=true;
         retro_run();frames[i]=last_video_signature;pcm[i]=poweron_pcm_hash;
         memset(cpus[i],0,sizeof(cpus[i]));if(cpu_snapshot){cpu_size=cpu_snapshot(cpus[i],sizeof(cpus[i]));assert(cpu_size<=sizeof(cpus[i]));}
         if(trace) {
            if(consume) {
               uint64_t video_hash=ss1_read_u64(trace),pcm_hash=ss1_read_u64(trace),expected_cpu_size=ss1_read_u64(trace);
               uint8_t expected_cpu[1024];assert(expected_cpu_size<=sizeof(expected_cpu));assert(fread(expected_cpu,1,(size_t)expected_cpu_size,trace)==expected_cpu_size);
               if(video_hash!=frames[i]||pcm_hash!=pcm[i]||expected_cpu_size!=cpu_size||memcmp(expected_cpu,cpus[i],cpu_size)) {
                  fprintf(stderr,"SS1 fresh-process mismatch class=%s machine=%s frame=%u video=%u pcm=%u cpu=%u\n",media_class,option_machine,i,video_hash!=frames[i],pcm_hash!=pcm[i],memcmp(expected_cpu,cpus[i],cpu_size)!=0);abort();
               }
            } else {
               ss1_write_u64(trace,frames[i]);ss1_write_u64(trace,pcm[i]);ss1_write_u64(trace,cpu_size);assert(fwrite(cpus[i],1,cpu_size,trace)==cpu_size);
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
            assert(frames[i]==last_video_signature&&pcm[i]==poweron_pcm_hash&&!memcmp(cpu,cpus[i],cpu_size));
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
            assert(frames[i]==last_video_signature&&pcm[i]==poweron_pcm_hash&&!memcmp(cpu,cpus[i],cpu_size));
         }
         assert(!ss1_context_queries);
         puts("SS1 retro_*: 12 rewind frames and 24 speculative frames match the real-frame video/PCM/CPU trace");
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
            if(last_video_signature!=frames[i]||poweron_pcm_hash!=pcm[i]||memcmp(cpu,cpus[i],cpu_size)) {
               fprintf(stderr,"SS1 differential mismatch class=%s machine=%s frame=%u video=%u pcm=%u cpu=%u\n",media_class,option_machine,i,last_video_signature!=frames[i],poweron_pcm_hash!=pcm[i],memcmp(cpu,cpus[i],cpu_size)!=0);abort();
            }
         }
      }
      size_t exact=saved?(size_t)ss1_get64(saved+16):0u;
      retro_unload_game();free(saved);free(again);
      printf("SS1 %s: class=%s machine=%s bytes=%zu exact=%zu serialize-ns=%llu unserialize-ns=%llu first-load-ns=%llu ram-kib=%d passed\n",argv[5],media_class,option_machine,bytes,exact,(unsigned long long)serialize_ns,(unsigned long long)unserialize_ns,(unsigned long long)first_load_ns,measured_ram);
   }
   poweron_trace=false;retro_deinit();assert(!open_files&&!open_dirs);assert(dlclose(module)==0);return 0;
}
