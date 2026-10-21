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
      char larger[4096];assert(snprintf(larger,sizeof(larger),"%s",argv[3])<(int)sizeof(larger));
      char *slash=strrchr(larger,'/');assert(slash);*slash=0;size_t prefix=strlen(larger);
      assert(snprintf(larger+prefix,sizeof(larger)-prefix,"/hd-family/valid-f.adf")<(int)(sizeof(larger)-prefix));
      struct retro_game_info replacement={.path=larger};
      assert(disk_control.set_eject_state(true)&&disk_control.replace_image_index(0,&replacement)&&disk_control.set_eject_state(false));
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
      free(saved);free(buffer);retro_unload_game();
   }
   puts("SS1b notices: every capture refusal once per cause; rewind/reset retain latches; unload and deinit reopen reset them");
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
   free(initial);free(before);free(after);free(latest);retro_unload_game();
   puts("SS1b first-frame: initial load and rewind to start refuse once without mutation; exact forward continuation");continue;
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
   const int cursors[]={-1,65536,INT_MAX,256,65535};
   for(unsigned i=0;i<sizeof(cursors)/sizeof(cursors[0]);++i) {
      memcpy(edited,saved,n);memcpy(edited+at+24,&cursors[i],sizeof(int));ss1_checksum(edited);
      uint64_t native=ss1_native_mutations;unsigned notices=total_osd_calls;
      if(i<3u) {
         assert(!retro_unserialize(edited,n)&&total_osd_calls==notices+1u);
         assert(retro_serialize(again,n)&&!memcmp(saved,again,n));
      } else {
         assert(retro_unserialize(edited,n)&&total_osd_calls==notices);
         assert(retro_serialize(again,n)&&!memcmp(edited,again,n));
         assert(retro_unserialize(saved,n));
      }
      assert(ss1_native_mutations==native);
   }
   free(saved);free(edited);free(again);retro_unload_game();
   puts("SS1b CMOS: -1/65536/INT_MAX refused transactionally; 256/65535 canonical");continue;
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
   size_t n=retro_serialize_size();
   /* All four shipping profiles have 4 MiB; the former all-32-slot read-only
    * reservation was 7,356,582. A one-slot session must shed unused metadata. */
   assert(n<7356582u);retro_run();uint8_t *saved=malloc(n),*again=malloc(n);assert(saved&&again&&retro_serialize(saved,n));
   assert(retro_unserialize(saved,n)&&retro_serialize(again,n)&&!memcmp(saved,again,n));
   assert(disk_control.set_eject_state(true));
   unsigned slots=disk_control.get_num_images();assert(slots==1u&&disk_control.add_image_index());
   assert(retro_serialize_size()==n);memset(again,0x69,n);unsigned notices=total_osd_calls;uint64_t native=ss1_native_mutations;
   assert(!retro_serialize(again,n)&&total_osd_calls==notices+1u&&ss1_native_mutations==native);
   for(size_t i=0;i<n;++i)assert(again[i]==0x69);
   assert(!retro_unserialize(saved,n)&&ss1_native_mutations==native);
   assert(disk_control.replace_image_index(slots,NULL));assert(retro_serialize_size()==n);
   assert(retro_serialize(again,n));free(saved);free(again);retro_unload_game();
   printf("SS1b capacity: machine=%s old=7356582 new=%zu; slot growth refuses before output; removing extra slot recovers\n",option_machine,n);continue;
}
