/* SS-1b public-API regression checks. SPDX-License-Identifier: GPL-2.0-only */
if(!strcmp(argv[5],"--ss1-depth")) {
   ss1_rewind_model_check();
   const size_t capacity=20000000u;size_t n=retro_serialize_size(),block=24u+((n+7u)&~7u);
   size_t maximum=((block+1u)&~1u)+((block+131069u)/131070u)*4u+6u+2u*sizeof(size_t);
   if(maximum+sizeof(size_t)>=capacity) {
      printf("SS1b depth: class=%s machine=%s core=%zu maximum=%zu history=0 (worst patch exceeds default)\n",media_class,option_machine,n,maximum);
      retro_unload_game();continue;
   }
   uint8_t *ring=calloc(capacity,1),*a=calloc(block+24u,1),*b=calloc(block+24u,1);assert(ring&&a&&b);
   ((uint16_t *)b)[block/2u+3u]=1u;
   for(unsigned copy=0;copy<2u;++copy) {
      uint8_t *p=copy?b:a;memcpy(p,"RASTATE1MEM ",12u);
      for(unsigned i=0;i<4u;++i)p[12u+i]=(uint8_t)(n>>(i*8u));memcpy(p+block-8u,"END ",4u);
   }
   void (*seed_clock)(void)=dlsym(module,"arc_engine_test_rtc_seed");assert(seed_clock);seed_clock();
   for(unsigned i=0;i<96u;++i)retro_run();assert(retro_serialize(a+16u,n));
   size_t head=sizeof(size_t),tail=head,entries=1u,low=SIZE_MAX,high=0,wraps=0;
   for(unsigned frame=0;frame<2400u;++frame) {
      ss1_set_input(frame);retro_run();assert(retro_serialize_size()==n&&retro_serialize(b+16u,n));
      size_t remaining=(tail+capacity-sizeof(size_t)-head-1u)%capacity+1u;
      while(remaining<=maximum) {
         assert(entries>1u);memcpy(&tail,ring+tail,sizeof(tail));--entries;
         remaining=(tail+capacity-sizeof(size_t)-head-1u)%capacity+1u;
      }
      size_t patch=ss1_rewind_patch_size((uint16_t *)a,(uint16_t *)b,block);
      assert(patch+2u*sizeof(size_t)<=maximum);
      size_t end=head+sizeof(size_t)+patch;
      if(end+maximum>capacity) {
         end=0;++wraps;
         if(tail==sizeof(size_t)){memcpy(&tail,ring+tail,sizeof(tail));--entries;}
      }
      memcpy(ring+end,&head,sizeof(head));end+=sizeof(size_t);
      memcpy(ring+head,&end,sizeof(end));head=end;++entries;
      size_t cursor=head,links=0;
      while(cursor!=tail) {
         assert(cursor>=sizeof(size_t)&&cursor<=capacity&&links<entries);
         memcpy(&cursor,ring+cursor-sizeof(size_t),sizeof(cursor));++links;
      }
      assert(links==entries-1u);
      uint8_t *swap=a;a=b;b=swap;
      if(frame>=1200u){if(entries-1u<low)low=entries-1u;if(entries-1u>high)high=entries-1u;}
   }
   printf("SS1b depth: class=%s machine=%s core=%zu wrapped=%zu maximum=%zu history=%zu..%zu final=%zu wraps=%zu captures=2401 granularity=1 buffer=20000000\n",media_class,option_machine,n,block,maximum,low,high,entries-1u,wraps);
   free(ring);free(a);free(b);retro_unload_game();continue;
}
if(!strcmp(argv[5],"--ss1-notices")) {
   /* Rewind captures before every frame, ignoring serialize's false return.
    * Exercise each cause independently and repeat all causes in new sessions. */
   for(unsigned session=0;session<3u;++session) {
      if(session) {
         if(session==2u) {
            retro_deinit();phase=PHASE_ENV;retro_set_environment(environment);
            phase=PHASE_INIT;retro_init();
            retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);
         }
         phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
         retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
      }
      retro_run();size_t n=retro_serialize_size();uint8_t *saved=malloc(n),*buffer=malloc(n);
      assert(saved&&buffer&&retro_serialize(saved,n));
      unsigned notices=total_osd_calls;
      retro_set_input_poll(ss1_fake_poll);
      for(unsigned frame=0;frame<32u;++frame)assert(!retro_serialize(buffer,n));
      assert(total_osd_calls==notices+1u&&strstr(last_message,"Preemptive Frames"));
      retro_set_input_poll(poll);
      assert(retro_unserialize(saved,n)); /* Rewind must not rewind the latch. */
      retro_reset();retro_run();notices=total_osd_calls;
      retro_set_input_poll(ss1_fake_poll);assert(!retro_serialize(buffer,n));
      assert(total_osd_calls==notices);retro_set_input_poll(poll);
      assert(retro_serialize(saved,n));
      void (*speculate)(bool)=dlsym(module,"arc_state_io_speculate");
      void (*persist)(void)=dlsym(module,"arc_libretro_test_ss1_persist_cmos");
      bool (*write_cmos)(unsigned,uint8_t)=dlsym(module,"arc_engine_test_cmos_write");
      assert(speculate&&persist&&write_cmos);speculate(true);
      for(unsigned i=0;i<300u;++i){assert(write_cmos(216u,(uint8_t)i));persist();}
      notices=total_osd_calls;uint64_t native=ss1_native_mutations;memset(buffer,0x69,n);
      for(unsigned call=0;call<32u;++call)assert(!retro_serialize(buffer,n));
      assert(total_osd_calls==notices+1u&&strstr(last_message,"pending disc changes"));
      assert(ss1_native_mutations==native);
      for(size_t i=0;i<n;++i)assert(buffer[i]==0x69);
      assert(retro_unserialize(saved,n));
      /* A successful load recovers the queue and rearms its notice (F-4). */
      speculate(true);
      for(unsigned i=0;i<300u;++i){assert(write_cmos(216u,(uint8_t)i));persist();}
      notices=total_osd_calls;
      for(unsigned call=0;call<32u;++call)assert(!retro_serialize(buffer,n));
      assert(total_osd_calls==notices+1u&&strstr(last_message,"pending disc changes"));
      assert(retro_unserialize(saved,n));
      char larger[4096];assert(snprintf(larger,sizeof(larger),"%s",argv[3])<(int)sizeof(larger));
      char *slash=strrchr(larger,'/');assert(slash);*slash=0;size_t prefix=strlen(larger);
      assert(snprintf(larger+prefix,sizeof(larger)-prefix,"/hd-family/valid-f.adf")<(int)(sizeof(larger)-prefix));
      char recovery[4096];assert(snprintf(recovery,sizeof(recovery),"%s/recovery.adf",data_root)<(int)sizeof(recovery));copy_file(argv[3],recovery);
      unsigned extra=disk_control.get_num_images();assert(disk_control.add_image_index());
      struct retro_game_info replacement={.path=larger};
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(extra,&replacement)&&disk_control.set_eject_state(false));
      notices=total_osd_calls;memset(buffer,0x69,n);
      uint8_t before[1024],after[1024];size_t cpu_n=cpu_snapshot(before,sizeof(before));
      for(unsigned frame=0;frame<32u;++frame) {
         native=ss1_native_mutations;notices=total_osd_calls;
         assert(retro_serialize_size()==n&&!retro_serialize(buffer,n));
         assert(ss1_native_mutations==native&&total_osd_calls==notices+(frame==0u));
         if(!frame)assert(strstr(last_message,"larger save-state buffer"));
         for(size_t i=0;i<n;++i)assert(buffer[i]==0x69);
         retro_run();
      }
      assert(cpu_snapshot(after,sizeof(after))==cpu_n&&memcmp(before,after,cpu_n));
      /* Recover the extra media envelope without replacing the mounted store. */
      replacement.path=recovery;
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(extra,&replacement)&&disk_control.set_eject_state(false));
      /* The reopened store has a new native frontier. Capture that frontier;
       * a save alone must not rearm the media notice. */
      assert(retro_serialize(saved,n));replacement.path=larger;
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(extra,&replacement)&&disk_control.set_eject_state(false));
      notices=total_osd_calls;assert(!retro_serialize(buffer,n)&&total_osd_calls==notices);
      replacement.path=recovery;
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(extra,&replacement)&&disk_control.set_eject_state(false));
      assert(retro_serialize(saved,n)&&retro_unserialize(saved,n));replacement.path=larger;
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(extra,&replacement)&&disk_control.set_eject_state(false));
      notices=total_osd_calls;
      for(unsigned call=0;call<32u;++call)assert(!retro_serialize(buffer,n));
      assert(total_osd_calls==notices+1u&&strstr(last_message,"larger save-state buffer"));
      free(saved);free(buffer);retro_unload_game();
   }
   puts("SS1b notices: capture causes deduplicated; successful loads rearm media/pending; new sessions reset notices");
   continue;
}
if(!strcmp(argv[5],"--ss1-session-poll")) {
   retro_run();size_t n=retro_serialize_size();uint8_t *state=malloc(n),*again=malloc(n);assert(state&&again);
   assert(retro_serialize(state,n));
   /* Menu preempt_init installs null polling but executes no fake frame.
    * core_unload_game can run before preempt_deinit restores that poll. */
   retro_set_input_poll(ss1_fake_poll);retro_unload_game();
   phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
   retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
   assert(retro_serialize_size()==n);unsigned notices=total_osd_calls;
   retro_run();assert(retro_serialize(state,n)&&total_osd_calls==notices);
   retro_run();assert(retro_unserialize(state,n)&&retro_serialize(again,n)&&!memcmp(state,again,n));
   /* The fresh session must still reject a genuine preempt activation. */
   retro_set_input_poll(ss1_fake_poll);assert(!retro_serialize(again,n));
   assert(total_osd_calls==notices+1u);retro_set_input_poll(poll);
   free(state);free(again);retro_unload_game();
   puts("SS1b poll: paused-menu preempt close followed by a new primary callback saves normally");continue;
}
if(!strcmp(argv[5],"--ss1-first-frame")) {
   size_t n=retro_serialize_size();uint8_t *initial=malloc(n),*before=malloc(n),*after=malloc(n),*latest=malloc(n);
   assert(initial&&before&&after&&latest&&retro_serialize(initial,n));
   unsigned notices=total_osd_calls;uint64_t native=ss1_native_mutations;
   assert(!retro_unserialize(initial,n)&&total_osd_calls==notices+1u&&ss1_native_mutations==native);
   assert(retro_serialize(after,n)&&!memcmp(initial,after,n));
   /* Rewind's initial capture precedes execution. Replay completed captures
    * back to frame one, then hit that initial state, with no native rollback. */
   retro_run();assert(retro_serialize(before,n));
   retro_run();assert(retro_serialize(latest,n)&&retro_unserialize(before,n));
   notices=total_osd_calls;native=ss1_native_mutations;
   assert(!retro_unserialize(initial,n)&&total_osd_calls==notices+1u&&ss1_native_mutations==native);
   assert(retro_serialize(after,n)&&!memcmp(before,after,n));
   retro_run();assert(retro_serialize(after,n)&&!memcmp(latest,after,n));
   /* 1.22.2 pops its oldest block again on every held frame, even when
    * pop reports end-of-buffer. It still executes the ensuing core frame. */
   for(unsigned held=0;held<32u;++held) {
      assert(retro_serialize(before,n));notices=total_osd_calls;native=ss1_native_mutations;
      assert(!retro_unserialize(initial,n)&&total_osd_calls==notices&&ss1_native_mutations==native);
      assert(retro_serialize(after,n)&&!memcmp(before,after,n));retro_run();
   }
   /* A new content session gets its own first refusal, even in this library. */
   retro_unload_game();phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
   assert(retro_serialize(initial,n));
   for(unsigned held=0;held<32u;++held) {
      notices=total_osd_calls;native=ss1_native_mutations;
      assert(!retro_unserialize(initial,n)&&total_osd_calls==notices+(held==0u));
      assert(ss1_native_mutations==native&&retro_serialize(after,n));
      if(!held)assert(!memcmp(initial,after,n));retro_run();
   }
   free(initial);free(before);free(after);free(latest);retro_unload_game();
   puts("SS1b first-frame: held rewind at oldest capture refuses once until a successful load, unchanged guest/stores and exact continuation");continue;
}
if(!strcmp(argv[5],"--ss1-load-rearm")) {
   size_t n=retro_serialize_size();uint8_t *oldest=malloc(n),*saved=malloc(n),*again=malloc(n),*bad=malloc(n);
   assert(oldest&&saved&&again&&bad);
   for(unsigned append=0;append<2u;++append) {
      if(append){phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;}
      assert(retro_serialize(oldest,n)); /* Initial pre-first-frame capture. */
      retro_run();assert(retro_serialize(saved,n));retro_run();
      if(append) {
         /* Use a valid pre-append capture for the ordered-media refusal. */
         memcpy(oldest,saved,n);char path[4096];
         assert(snprintf(path,sizeof(path),"%s/rearm-appended.adf",data_root)<(int)sizeof(path));copy_file(argv[3],path);
         struct retro_game_info image={.path=path};unsigned slot=disk_control.get_num_images();
         assert(disk_control.set_eject_state(true)&&disk_control.add_image_index());
         assert(disk_control.replace_image_index(slot,&image)&&disk_control.set_image_index(slot)&&disk_control.set_eject_state(false));
      }
      /* 1.22.2 holds at the oldest block: load then run, with no successful
       * load between these failures. Extra saves only observe canonical state. */
      for(unsigned held=0;held<32u;++held) {
         assert(retro_serialize(saved,n));unsigned notices=total_osd_calls,logs=ss1_state_refusal_logs;
         uint64_t native=ss1_native_mutations;
         assert(!retro_unserialize(oldest,n)&&total_osd_calls==notices+(held==0u));
         assert(ss1_state_refusal_logs==logs+(held==0u)&&ss1_native_mutations==native);
         assert(retro_serialize(again,n)&&!memcmp(saved,again,n));retro_run();
      }
      /* Release, play and successfully restore a current state. The subsequent
       * mismatching manual load must announce its own notice and log reason. */
      for(unsigned frame=0;frame<8u;++frame)retro_run();
      assert(retro_serialize(saved,n));retro_run();
      unsigned notices=total_osd_calls,logs=ss1_state_refusal_logs;
      assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n));
      assert(total_osd_calls==notices&&ss1_state_refusal_logs==logs);
      memcpy(bad,saved,n);size_t at=ss1_record(bad,"identity.machine",5);assert(at);
      bad[at+24]^=0x40u;ss1_checksum(bad);
      for(unsigned attempt=0;attempt<32u;++attempt) {
         notices=total_osd_calls;logs=ss1_state_refusal_logs;uint64_t native=ss1_native_mutations;
         assert(!retro_unserialize(bad,n)&&total_osd_calls==notices+(attempt==0u));
         assert(ss1_state_refusal_logs==logs+(attempt==0u)&&ss1_native_mutations==native);
         assert(strstr(last_message,"does not match the running session"));
         assert(strstr(ss1_state_refusal_reason,"identity.machine"));
         assert(retro_serialize(again,n)&&!memcmp(saved,again,n));
      }
      assert(retro_serialize_size()==n);retro_unload_game();
   }
   free(oldest);free(saved);free(again);free(bad);
   puts("SS1b load rearm: session-start and pre-append holds notify once; successful restore rearms later manual mismatch and reason");continue;
}
if(!strcmp(argv[5],"--ss1-cmos-cursor")) {
   retro_run();size_t n=retro_serialize_size();uint8_t *saved=malloc(n),*edited=malloc(n),*again=malloc(n);
   assert(saved&&edited&&again&&retro_serialize(saved,n));
   /* cmos.addr follows device_addr (byte plus ABI padding) and int state. Locate its
    * keyed scalar independently; a repaired CRC reaches the range validator. */
   uint8_t field[24];ss1_put64(field,ss1_key("engine/arculator/src/cmos.c:cmos"));
   ss1_put64(field+8,2u*sizeof(int));ss1_put64(field+16,sizeof(int));
   uint64_t key=UINT64_C(14695981039346656037);
   for(unsigned i=0;i<sizeof(field);++i)key=(key^field[i])*UINT64_C(1099511628211);
   size_t at=40;while(at<ss1_get64(saved+16)&&ss1_get64(saved+at)!=key)at+=24u+(size_t)ss1_get64(saved+at+16);
   assert(at<ss1_get64(saved+16)&&ss1_get64(saved+at+16)==sizeof(int));
   const int cursors[]={-1,INT_MIN,0,256,65535,65536,INT_MAX};
   for(unsigned i=0;i<sizeof(cursors)/sizeof(cursors[0]);++i) {
      memcpy(edited,saved,n);memcpy(edited+at+24,&cursors[i],sizeof(int));ss1_checksum(edited);
      uint64_t native=ss1_native_mutations;unsigned notices=total_osd_calls;
      if(i<2u) {
         assert(!retro_unserialize(edited,n)&&total_osd_calls==notices+(i==0u));
         assert(retro_serialize(again,n)&&!memcmp(saved,again,n));
      } else {
         assert(retro_unserialize(edited,n)&&total_osd_calls==notices);
         assert(retro_serialize(again,n)&&!memcmp(edited,again,n));
         assert(retro_unserialize(saved,n));
      }
      assert(ss1_native_mutations==native);
   }
   free(saved);free(edited);free(again);retro_unload_game();
   puts("SS1b CMOS: negative cursors refused; 0/256/65535/65536/INT_MAX restore without masking");continue;
}
if(!strcmp(argv[5],"--ss1-capacity-set")) {
   size_t n=retro_serialize_size();assert(disk_control.get_num_images()==ARC_PLAYLIST_MAX_ENTRIES);
   size_t worst=n+65536u+16u+
      (ARC_JFD_MAX_TRACK_SIDES*ARC_JFD_MAX_SECTORS_PER_TRACK+ARC_JFD_PDD5_SLOTS+7u)/8u+
      ARC_JFD_MAX_DECODED+ARC_JFD_PDD5_SLOTS*1024u+(ARC_PLAYLIST_MAX_ENTRIES-1u)*96u;
   assert(worst<=16u*1024u*1024u&&!disk_control.add_image_index());
   retro_run();uint8_t *saved=malloc(n),*again=malloc(n);assert(saved&&again&&retro_serialize(saved,n));
   assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n));
   printf("SS1b full set: machine=%s slots=32 base=%zu worst-floppy=%zu; full metadata and parser envelope fit\n",option_machine,n,worst);
   free(saved);free(again);retro_unload_game();continue;
}
if(!strcmp(argv[5],"--ss1-capacity")) {
   size_t n=retro_serialize_size();assert(n&&n<=16u*1024u*1024u);
   retro_run();uint8_t *saved=malloc(n),*again=malloc(n),*next=malloc(n);assert(saved&&again&&next);
   assert(retro_serialize(saved,n)&&retro_unserialize(saved,n));
   /* 1.22.2 Load New Disc: eject, append, replace the new slot, select, close.
    * The appended authored image fits the already reserved raw envelope. */
   char path[4096];assert(snprintf(path,sizeof(path),"%s/appended.adf",data_root)<(int)sizeof(path));copy_file(argv[3],path);
   struct retro_game_info appended={.path=path};unsigned slot=disk_control.get_num_images();assert(slot==1u);
   assert(disk_control.set_eject_state(true)&&disk_control.add_image_index());
   assert(disk_control.replace_image_index(slot,&appended)&&disk_control.set_image_index(slot)&&disk_control.set_eject_state(false));
   assert(disk_control.get_num_images()==2u&&disk_control.get_image_index()==slot&&retro_serialize_size()==n);
   /* Old one-disc rewind entries still fail the unchanged ordered-media
    * identity check. Holding at that entry announces this cause only once.
    * The extra captures below observe canonical state, not frontend pushes. */
   for(unsigned held=0;held<32u;++held) {
      assert(retro_serialize(again,n));unsigned notice=total_osd_calls;uint64_t native=ss1_native_mutations;
      assert(!retro_unserialize(saved,n)&&total_osd_calls==notice+(held==0u));
      assert(ss1_native_mutations==native&&retro_serialize(next,n)&&!memcmp(again,next,n));retro_run();
   }
   retro_run();unsigned notices=total_osd_calls;
   assert(retro_serialize(saved,n)&&total_osd_calls==notices);
   retro_run();notices=total_osd_calls;assert(retro_serialize(next,n)&&total_osd_calls==notices);
   /* Rewind post-append states, then replay their exact forward continuation. */
   for(unsigned held=0;held<32u;++held) {
      notices=total_osd_calls;
      assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n)&&total_osd_calls==notices);
      retro_run();notices=total_osd_calls;
      assert(retro_serialize(again,n)&&!memcmp(next,again,n)&&total_osd_calls==notices);
   }
   /* A one-frame run-ahead cycle must still roll back after append. */
   notices=total_osd_calls;assert(retro_unserialize(saved,n)&&total_osd_calls==notices);
   retro_set_input_poll(ss1_fake_poll);retro_set_input_state(ss1_fake_input);retro_run();
   retro_set_input_poll(poll);retro_set_input_state(ss1_logged_input);
   notices=total_osd_calls;
   assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n)&&total_osd_calls==notices);
   assert(retro_serialize_size()==n);
   free(saved);free(again);free(next);retro_unload_game();
   printf("SS1b capacity: machine=%s bound=%zu reserves 32 slots; append, held rewind and run-ahead remain usable\n",option_machine,n);continue;
}
if(!strcmp(argv[5],"--ss1-held-media")) {
   size_t n=retro_serialize_size();uint8_t *states[3],*scratch=malloc(n);assert(scratch);
   for(unsigned frame=0;frame<3u;++frame){states[frame]=malloc(n);assert(states[frame]&&retro_serialize(states[frame],n));retro_run();}
   char larger[4096];assert(snprintf(larger,sizeof(larger),"%s",argv[3])<(int)sizeof(larger));
   char *slash=strrchr(larger,'/');assert(slash);*slash=0;size_t prefix=strlen(larger);
   assert(snprintf(larger+prefix,sizeof(larger)-prefix,"/hd-family/valid-f.adf")<(int)(sizeof(larger)-prefix));
   struct retro_game_info image={.path=larger};unsigned slot=disk_control.get_num_images();
   assert(disk_control.set_eject_state(true)&&disk_control.add_image_index()&&disk_control.replace_image_index(slot,&image));
   assert(disk_control.set_image_index(slot)&&disk_control.set_eject_state(false));
   const uint8_t *history[32]={states[0],states[1],states[2]};
   unsigned entries=3u;
   for(unsigned burst=0;burst<2u;++burst) {
      /* Held means deserialize then run, never a rewind capture. Past the
       * final pop, state_manager_pop returns its same oldest block forever. */
      for(unsigned held=0;held<64u;++held) {
         const uint8_t *oldest=history[held<entries?entries-1u-held:0u];
         unsigned notices=total_osd_calls;uint64_t native=ss1_native_mutations;
         uint8_t before[1024],after[1024];size_t cpu_n=cpu_snapshot(before,sizeof(before));
         assert(!retro_unserialize(oldest,n)&&total_osd_calls==notices+(burst==0u&&held==0u));
         assert(ss1_native_mutations==native&&cpu_snapshot(after,sizeof(after))==cpu_n&&!memcmp(before,after,cpu_n));
         if(!burst&&!held)assert(strstr(last_message,"larger save-state buffer"));
         retro_run();
      }
      if(!burst) {
         /* Release rewind: 1.22.2 ignores false capture and still pushes the
          * alternating stale nextblock/thisblock pair. Model those 32 entries,
          * then hold again and exhaust them in the second burst. */
         entries=0u; /* The previous held burst popped every old entry. */
         for(unsigned frame=0;frame<32u;++frame) {
            const uint8_t *stale=states[frame&1u?0u:1u];memcpy(scratch,stale,n);
            unsigned notices=total_osd_calls;uint64_t native=ss1_native_mutations;
            assert(!retro_serialize(scratch,n)&&retro_serialize_size()==n&&total_osd_calls==notices);
            assert(ss1_native_mutations==native&&!memcmp(scratch,stale,n));history[entries++]=stale;retro_run();
         }
      }
   }
   for(unsigned i=0;i<3u;++i)free(states[i]);free(scratch);retro_unload_game();
   puts("SS1b held media: oversized appended image, failed captures and repeated oldest loads share one cause notice");continue;
}
if(!strcmp(argv[5],"--ss1-retained-poll")) {
   size_t n=retro_serialize_size();uint8_t *saved=malloc(n),*again=malloc(n);assert(saved&&again);
   /* Retain both callbacks across reload: no setters after either load. */
   retro_unload_game();phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
   unsigned notices=total_osd_calls;
   assert(retro_serialize(saved,n)&&total_osd_calls==notices); /* serialize fallback */
   retro_run();assert(retro_serialize(saved,n));retro_run();
   assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n));
   retro_unload_game();phase=PHASE_LOAD;assert(retro_load_game(&game));phase=PHASE_RUN;
   retro_run(); /* boundary_frame fallback, before any serialization or setter */
   notices=total_osd_calls;retro_set_input_poll(ss1_fake_poll);
   assert(!retro_serialize(again,n)&&total_osd_calls==notices+1u&&strstr(last_message,"Preemptive Frames"));
   retro_set_input_poll(poll);assert(retro_serialize(saved,n));retro_run();
   assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n));
   free(saved);free(again);retro_unload_game();
   puts("SS1b retained callbacks: serialize and frame fallback bind primary poll independently; later preempt still refuses");continue;
}
