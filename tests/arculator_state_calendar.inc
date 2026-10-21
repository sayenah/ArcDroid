/* SS-1 / AY-1 fresh-process calendar restore/reset differential. GPL-2.0-only. */
if(save_state_mode){
   assert(argc==10);
   bool produce=!strcmp(argv[5],"ss1-produce");
   assert(produce || !strcmp(argv[5],"ss1-consume"));
   unsigned mask=(unsigned)strtoul(argv[8],NULL,10);assert(mask<8);
   API(retro_serialize_size);API(retro_serialize);API(retro_unserialize);API(arc_sha256_hex);
   void (*persisted)(uint8_t *);LOAD(persisted,"arc_engine_cmos");
   void (*overlay)(int,const uint8_t *);LOAD(overlay,"cmos_session_year");
   void (*poison_cache)(void);LOAD(poison_cache,"arc_libretro_test_ss1_poison_calendar_cache");
   size_t (*cpu_snapshot)(void *,size_t);LOAD(cpu_snapshot,"arc_engine_test_cpu_snapshot");
   uint32_t (*read_word)(uint32_t);LOAD(read_word,"readmemf_debug");
   const unsigned raw[]={0xc0u,0xc1u,0x3fu};
   struct {
      size_t bytes,cpu_bytes;
      unsigned saved_year;
      uint8_t persisted[240],cpus[24][1024],images[24][256];
      uint64_t pcm[24],pictures[24];
      char states[24][65];
   } trace={0}; /* Private same-executable interchange, not a core format. */
   char state_path[4096],trace_path[4096];
   assert(snprintf(state_path,sizeof(state_path),"%s/state.bin",argv[9])<(int)sizeof(state_path));
   assert(snprintf(trace_path,sizeof(trace_path),"%s/trace.bin",argv[9])<(int)sizeof(trace_path));
   clock((int)base,7,15,12,0,0,0);assert(p_retro_load_game(&game));
   if(produce)for(unsigned f=0;read_word(0x02000408)!=0xa5u;++f){assert(f<10);p_retro_run();}
   uint8_t live[256];assert(image(live) && live[192]+100u*live[193]==base);
   size_t bytes=p_retro_serialize_size();assert(bytes && bytes<=16u*1024u*1024u);
   uint8_t *saved=malloc(bytes),*again=malloc(bytes);assert(saved && again);
   if(produce){
      uint8_t before[240],expected[256];persisted(before);memcpy(expected,live,sizeof(expected));
      unsigned edited=base+8u;
      if(mask&1u){expected[192]=(uint8_t)(edited%100u);assert(write(192,expected[192]));}
      if(mask&2u){expected[193]=(uint8_t)(edited/100u);assert(write(193,expected[193]));}
      unsigned sum=1;for(unsigned at=16;at<256;++at)if(at!=63)sum+=expected[at];
      if(mask&4u){expected[63]=(uint8_t)sum;assert(write(63,expected[63]));}
      /* Publish real guest writes before capture. The parent copies only this
       * quiescent committed branch while this process waits for its reply. */
      p_retro_run();assert(image(live));persisted(trace.persisted);
      trace.bytes=bytes;trace.saved_year=live[192]+100u*live[193];
      for(unsigned i=0;i<3;++i)assert(trace.persisted[raw[i]-16u]==
         ((mask&(1u<<i))?expected[raw[i]]:before[raw[i]-16u]));
      assert(p_retro_serialize(saved,bytes));
      FILE *f=fopen(state_path,"wb");assert(f && fwrite(saved,1,bytes,f)==bytes && !fclose(f));
      puts("SS1 calendar checkpoint");assert(!fflush(stdout));assert(getchar()=='c');
   }else{
      FILE *f=fopen(trace_path,"rb");assert(f && fread(&trace,1,sizeof(trace),f)==sizeof(trace) && fgetc(f)==EOF && !fclose(f));
      assert(trace.bytes==bytes && trace.cpu_bytes && trace.cpu_bytes<=sizeof(trace.cpus[0]));
      f=fopen(state_path,"rb");assert(f && fread(saved,1,bytes,f)==bytes && fgetc(f)==EOF && !fclose(f));
      /* Load before this fresh session publishes any frame. In particular,
       * normal construction may repair a partially guest-written checksum.
       * Poison all new objects without publishing. A fresh process starts on
       * the copied capture frontier, never on the producer's newer stores. */
      uint8_t alien[]={77u,18u};overlay(1,alien);overlay(0,NULL);
      /* Track all three writes after disabling the overlay, so its setter
       * cannot clear the write-mask poison back to the saved zero mask. */
      for(unsigned i=0;i<3;++i)assert(write(raw[i],(uint8_t)(0x71u+i)));
      poison_cache();clock((int)base+4,8,16,12,0,0,0);
      assert(p_retro_unserialize(saved,bytes));
      assert(p_retro_serialize(again,bytes) && !memcmp(saved,again,bytes));
      assert(p_retro_unserialize(saved,bytes)); /* Repeated loads publish nothing. */
      uint8_t restored[240];persisted(restored);assert(!memcmp(trace.persisted,restored,sizeof(restored)));
   }
   unsigned different=0;
   for(unsigned phase=0;phase<2;++phase){
      if(phase){
         /* Both independent branches reset from k+12 with the same future
          * external clock. Loading must not rewind that host-only input. */
         clock((int)base+4,8,16,12,0,0,0);p_retro_reset();
         assert(image(live) && live[192]+100u*live[193]==trace.saved_year);
      }
      for(unsigned f=phase*12;f<(phase+1)*12;++f){
         pcm_hash=UINT64_C(1469598103934665603);p_retro_run();
         uint8_t cpu[1024],actual[256];char state_hash[65];
         size_t cpu_bytes=cpu_snapshot(cpu,sizeof(cpu));assert(cpu_bytes && cpu_bytes<=sizeof(cpu));
         assert(image(actual) && p_retro_serialize(again,bytes));
         uint64_t used=0;for(unsigned i=0;i<8;++i)used|=(uint64_t)again[16u+i]<<(i*8u);
         assert(used>=40u && used<=bytes);p_arc_sha256_hex(again,(size_t)used,state_hash);
         if(produce){
            trace.cpu_bytes=cpu_bytes;memcpy(trace.cpus[f],cpu,cpu_bytes);memcpy(trace.images[f],actual,sizeof(actual));
            trace.pcm[f]=pcm_hash;trace.pictures[f]=picture_hash;memcpy(trace.states[f],state_hash,sizeof(state_hash));
         }else{
            assert(cpu_bytes==trace.cpu_bytes && !memcmp(cpu,trace.cpus[f],cpu_bytes));
            assert(pcm_hash==trace.pcm[f] && !memcmp(actual,trace.images[f],sizeof(actual)));
            assert(!strcmp(state_hash,trace.states[f]));different+=picture_hash!=trace.pictures[f];
         }
      }
   }
   assert(read_word(0x02000408)==0xa5u);
   assert(read_word(0x02000400)==trace.saved_year && read_word(0x02000404)==1u);
   assert((read_word(0x02000018)&0x1fu)==8u);
   if(produce){FILE *f=fopen(trace_path,"wb");assert(f && fwrite(&trace,1,sizeof(trace),f)==sizeof(trace) && !fclose(f));}
   p_retro_unload_game();free(saved);free(again);clock(0,0,0,0,0,0,0);
   p_retro_deinit();vfs_assert_no_open_handles();
   if(!produce)printf("SS1 calendar: machine=%s host-year=%u written-mask=%u guest-year=%u canonical/24-frame CPU/PCM/guest/CMOS exact (12 before and 12 after reset); reset after load retains year and samples external month passed; picture-different=%u/24\n",machine,base,mask,trace.saved_year,different);
   return 0;
}
