/* ArcDroid session, media and owned JFD graph codec. GPL-2.0-only. */
static void arc_state_heap_bytes(ArcState *s,const char *name,void *slot,size_t bytes,size_t maximum)
{
   void *data=arc_state_heap(s,name,slot,bytes,maximum);
   if(arc_state_pass(s)&&data) {
      arc_state_object(s,name);
      arc_state_value(s,data,arc_state_heap_size(s,name));
   }
}
typedef struct {ArcJfdOverlay *previous;size_t sectors;bool all;} ArcStateJfdRetirement;
static void arc_state_jfd_retire_commit(void *opaque)
{
   ArcStateJfdRetirement *r=opaque;ArcJfdOverlay *old=r->previous;
   if(old->data)for(size_t i=0;i<r->sectors;++i)free(old->data[i]);
   if(r->all) {
      free(old->data);free(old->eligible);free(old->store);
      for(unsigned t=0;t<ARC_JFD_PDD5_TRACKS;++t)for(unsigned id=0;id<ARC_JFD_PDD5_IDS;++id)free(old->created[t][id]);
   }
   free(r);
}
static void arc_state_jfd_retire(ArcState *s,ArcJfdOverlay *old,size_t sectors,bool all)
{
   ArcStateJfdRetirement *r=malloc(sizeof(*r));
   if(!r){arc_state_fail(s,"allocation");return;}
   *r=(ArcStateJfdRetirement){old,sectors,all};arc_state_action(s,arc_state_jfd_retire_commit,arc_state_free,r);
}
static void arc_state_jfd(ArcState *s,unsigned index)
{
   ArcJfdImage *image=&arc_media[index].jfd;
   char name[160],prefix[80];
   snprintf(prefix,sizeof(prefix),"media.%u.jfd",index);
   snprintf(name,sizeof(name),"%s.decoded",prefix);
   uint8_t *decoded=arc_state_heap(s,name,&image->decoded,image->decoded_size,ARC_JFD_MAX_DECODED);
   size_t decoded_size=arc_state_heap_size(s,name);
   if(decoded)arc_state_target_range(s,decoded,decoded_size,ARC_STATE_REFERENCE_JFD_DATA);
   arc_state_target(s,image,ARC_STATE_REFERENCE_JFD_IMAGE);
   if(decoded) {
      size_t base_size=0;ArcDigest base_digest={0};const void *base=arc_io_state_base(index*2u+1u,&base_size,&base_digest);
      if(!base||base_size!=decoded_size||strcmp(base_digest.sha1,arc_media[index].digest.sha1))arc_state_fail(s,"JFD immutable base");
      snprintf(name,sizeof(name),"%s.base",prefix);arc_state_expect(s,name,base_digest.sha1,sizeof(base_digest.sha1));
      if(arc_state_loading(s)&&!arc_state_pass(s)&&base&&base_size==decoded_size)memcpy(decoded,base,decoded_size);
   }
   snprintf(name,sizeof(name),"%s.sectors",prefix);
   ArcJfdSector *sectors=arc_state_heap(s,name,&image->sectors,image->sector_count*sizeof(*sectors),
      ARC_JFD_MAX_TRACK_SIDES*ARC_JFD_MAX_SECTORS_PER_TRACK*sizeof(*sectors));
   size_t sector_bytes=arc_state_heap_size(s,name),sector_count=sector_bytes/sizeof(*sectors);
   if(sector_bytes%sizeof(*sectors))arc_state_fail(s,"JFD descriptors");
   if(arc_state_pass(s)&&decoded_size) {
      arc_state_object(s,name);
      for(size_t i=0;i<sector_count;++i)
         arc_state_value(s,&sectors[i],offsetof(ArcJfdSector,flags)+sizeof(sectors[i].flags));
      arc_state_object(s,"libretro/arcdroid_libretro.c:arc_media");
      arc_state_jfd_image_values(s,image);
      size_t incoming_size,incoming_count;
      arc_state_project(s,&incoming_size,&image->decoded_size,sizeof(incoming_size));
      arc_state_project(s,&incoming_count,&image->sector_count,sizeof(incoming_count));
      if(incoming_size!=decoded_size||incoming_count!=sector_count)arc_state_fail(s,"JFD extent");
      ArcJfdImage projected;arc_state_project(s,&projected,image,sizeof(projected));
      if(projected.track_count>ARC_JFD_MAX_TRACK_SIDES)arc_state_fail(s,"JFD track count");
      for(unsigned t=0;t<projected.track_count&&t<ARC_JFD_MAX_TRACK_SIDES;++t) {
         const ArcJfdTrack *track=&projected.tracks[t];
         if(track->first_sector>sector_count||track->sector_count>sector_count-track->first_sector)
            arc_state_fail(s,"JFD track extent");
      }
      for(size_t i=0;i<sector_count;++i) {
         const ArcJfdSector *sector=&sectors[i];
         if(!sector->size||sector->size>65536u||
            (sector->data_offset!=UINT32_MAX&&(sector->data_offset>decoded_size||sector->size>decoded_size-sector->data_offset)))
            arc_state_fail(s,"JFD payload extent");
      }
   }
   if(arc_state_loading(s)&&arc_state_pass(s)&&!decoded_size) {
      ArcJfdImage empty={0};arc_state_stage(s,image,&empty,sizeof(empty));
   }
   snprintf(name,sizeof(name),"%s.overlay",prefix);
   ArcJfdOverlay *old=image->overlay;
   ArcJfdOverlay *overlay=arc_state_heap(s,name,&image->overlay,old?sizeof(*old):0,sizeof(*old));
   if(!overlay) {
      if(arc_state_loading(s)&&!arc_state_pass(s)&&old)arc_state_jfd_retire(s,old,image->sector_count,true);
      return;
   }
   if(arc_state_pass(s)) {arc_state_object(s,name);arc_state_jfd_overlay_values(s,overlay);}
   snprintf(name,sizeof(name),"%s.eligible",prefix);
   arc_state_heap_bytes(s,name,&overlay->eligible,old&&old->eligible?image->sector_count:0,
      ARC_JFD_MAX_TRACK_SIDES*ARC_JFD_MAX_SECTORS_PER_TRACK);
   snprintf(name,sizeof(name),"%s.replacements",prefix);
   uint8_t **replacements=arc_state_heap(s,name,&overlay->data,
      old&&old->data?image->sector_count*sizeof(uint8_t*):0,
      ARC_JFD_MAX_TRACK_SIDES*ARC_JFD_MAX_SECTORS_PER_TRACK*sizeof(uint8_t*));
   size_t replacement_count=arc_state_heap_size(s,name)/sizeof(uint8_t*);
   if(arc_state_loading(s)&&!arc_state_pass(s)&&!replacements&&old&&old->data)
      arc_state_jfd_retire(s,old,image->sector_count,false);
   if(replacements&&replacement_count!=sector_count)arc_state_fail(s,"JFD replacements");
   for(size_t i=0;i<replacement_count;++i) {
      snprintf(name,sizeof(name),"%s.replacement.%zu",prefix,i);
      size_t old_size=old&&old->data&&i<image->sector_count&&old->data[i]?image->sectors[i].size:0;
      arc_state_heap_bytes(s,name,&replacements[i],old_size,65536u);
      size_t n=arc_state_heap_size(s,name);
      if(n)arc_state_target_range(s,replacements[i],n,ARC_STATE_REFERENCE_JFD_DATA);
      if(arc_state_pass(s)&&n&&n!=sectors[i].size)arc_state_fail(s,"JFD sector extent");
   }
   for(unsigned t=0;t<ARC_JFD_PDD5_TRACKS;++t)for(unsigned id=0;id<ARC_JFD_PDD5_IDS;++id) {
      snprintf(name,sizeof(name),"%s.created.%u.%u",prefix,t,id);
      arc_state_heap_bytes(s,name,&overlay->created[t][id],
         old&&old->created[t][id]?1024u:0,1024u);
      size_t n=arc_state_heap_size(s,name);
      if(n)arc_state_target_range(s,overlay->created[t][id],n,ARC_STATE_REFERENCE_JFD_DATA);
      if(n&&n!=1024u)arc_state_fail(s,"JFD creation extent");
   }
   snprintf(name,sizeof(name),"%s.store",prefix);
   arc_jfd_state_binding(s,name,overlay,decoded,decoded_size,arc_save_path,index+1u);
   if(arc_state_loading(s)) {
      overlay->log_refusal=arc_jfd_log_refusal;
      overlay->log_map_unavailable=arc_jfd_log_map_unavailable;
   }
}
static void arc_state_frontend_extra(ArcState *s)
{
   arc_state_region(s,"media.jfd.blank",(void*)arc_jfd_blank_sector(),1024u);
   arc_state_target_range(s,(void*)arc_jfd_blank_sector(),1024u,ARC_STATE_REFERENCE_JFD_DATA);
   arc_state_io_root(arc_save_path);
   /* Stable logical file names bind to the current session's prepared paths. */
   for(unsigned i=0;i<ARC_PLAYLIST_MAX_ENTRIES;++i) {
      bool writable=arc_plan.disk_writes!=ARC_DISK_WRITES_OFF;
      arc_io_state_binding(i*2u,arc_media[i].source,writable);
      arc_io_state_binding(i*2u+1u,arc_media[i].staged,writable);
      if(i<arc_media_count) {
         char base_path[4096];if(!arc_state_media_base_path(&arc_media[i],base_path))arc_state_fail(s,"media base path");
         arc_io_state_base_path(i*2u,base_path);arc_io_state_base_path(i*2u+1u,base_path);
      }
   }
   arc_io_state_binding(64,":arcdroid-memory-0:",false);
   arc_io_state_binding(65,":arcdroid-memory-1:",false);
   arc_io_state_binding(66,arc_helper_path,false);arc_io_state_base_path(66,arc_helper_path);
   arc_state_io_snapshot(s);
   arc_state_io_frontiers(s);
   arc_state_region(s,"media.carrier",arc_adffs_carrier,arc_adffs_carrier?ARC_ADFFS_DISC_SIZE:0);
   arc_state_region(s,"media.support",arc_adffs_support,arc_adffs_support?ARC_ADFFS_DISC_SIZE:0);
   (void)arc_state_heap(s,"video.osk.pixels",&arc_osk_surface,arc_osk_surface_pixels*sizeof(*arc_osk_surface),4096u*2048u*4u);
   arc_state_raster(s,"libretro/arcdroid_libretro.c:arc_present_frame",arc_present_frame,4096u,arc_present_rows);
   if(arc_adffs_carrier)arc_state_target_range(s,arc_adffs_carrier,ARC_ADFFS_DISC_SIZE,ARC_STATE_REFERENCE_MEDIA_DATA);
   if(arc_adffs_support)arc_state_target_range(s,arc_adffs_support,ARC_ADFFS_DISC_SIZE,ARC_STATE_REFERENCE_MEDIA_DATA);
   unsigned decoded_images=0;
   for(unsigned i=0;i<arc_media_count;++i) {
      if(arc_state_pass(s)) {
         arc_state_object(s,"libretro/arcdroid_libretro.c:arc_media");
         arc_state_media_entry_values(s,&arc_media[i]);
      }
      arc_state_jfd(s,i);
      char decoded_name[80];snprintf(decoded_name,sizeof(decoded_name),"media.%u.jfd.decoded",i);
      if(arc_state_heap_size(s,decoded_name))++decoded_images;
   }
   if(decoded_images>1u)arc_state_fail(s,"JFD ownership");
   uint64_t hd[2],jfd[ARC_PLAYLIST_MAX_ENTRIES];
   memcpy(hd,arc_state_committed_hd,sizeof(hd));memcpy(jfd,arc_state_committed_jfd,sizeof(jfd));
   if(arc_state_pass(s)) {
      bool speculative=arc_state_io_speculative();arc_state_io_speculate(false);
      if(arc_state_loading(s)&&!arc_install_store_frontier(arc_hd_store,hd))arc_state_fail(s,"HD frontier unavailable");
      for(unsigned i=0;i<arc_media_count;++i)if(arc_media[i].info.kind==ARC_MEDIA_JFD) {
         if(!arc_jfd_store_frontier_base(arc_save_path,&arc_media[i].digest,arc_media[i].jfd_sha256,&jfd[i]))arc_state_fail(s,"JFD frontier unavailable");
         if(jfd[i]<arc_state_committed_jfd[i])arc_state_fail(s,"JFD frontier lost");
         if(arc_media[i].jfd.overlay&&arc_media[i].jfd.overlay->store) {
            uint64_t active=0;
            if(!arc_jfd_store_frontier(&arc_media[i].jfd,&active)||active!=jfd[i])arc_state_fail(s,"JFD frontier invalid");
            if(!arc_state_loading(s)&&active!=arc_state_committed_jfd[i])arc_state_fail(s,"JFD committed frontier changed");
         }
      }
      arc_state_io_speculate(speculative);
   }
   /* Expectations are checked once, while native I/O excludes the staged view. */
   if(arc_state_pass(s)) {
      arc_state_expect(s,"frontier.hd",hd,sizeof(hd));
      arc_state_expect(s,"frontier.jfd",jfd,sizeof(jfd));
   }
   const char build[]=ARCDROID_VERSION ";" ARC_BUILD_COMMIT ";" ARC_BUILD_TARGET ";" ARC_BUILD_CONFIG ";closed;" ARC_BUILD_SUBSTITUTIONS;
   arc_state_expect(s,"identity.build",build,sizeof(build));
   const char platform[]=ARCDROID_STATE_PLATFORM;
   arc_state_expect(s,"identity.platform",platform,sizeof(platform));
   const char schema[]=ARCDROID_STATE_SCHEMA;
   arc_state_expect(s,"identity.schema",schema,sizeof(schema));
   arc_state_expect(s,"identity.engine",&arc_kind,sizeof(arc_kind));
#if defined(ARCDROID_RPC_MEASUREMENT)
   const char substitutions[]="measurement,test-plans";
#elif defined(ARCDROID_TEST_PLANS)
   const char substitutions[]="test-faults,test-plans";
#elif defined(ARCDROID_TEST_FAULTS)
   const char substitutions[]="test-faults";
#else
   const char substitutions[]=ARC_BUILD_SUBSTITUTIONS;
#endif
   arc_state_expect(s,"identity.substitutions",substitutions,sizeof(substitutions));
#if defined(ARCDROID_TEST_FAULTS)
   bool catalogue_override=arc_test_rpc_catalogue!=NULL;
#else
   bool catalogue_override=false;
#endif
   arc_state_expect(s,"identity.catalogue",&catalogue_override,sizeof(catalogue_override));
   uint64_t abi[]={sizeof(void*),sizeof(size_t),sizeof(long double),sizeof(ArcCoreOptions),sizeof(ArcJfdImage),UINT64_C(0x0102030405060708)};
   arc_state_expect(s,"identity.abi",abi,sizeof(abi));
   arc_state_expect(s,"identity.machine",&arc_session_machine,sizeof(arc_session_machine));
   if(!arc_state_firmware_valid){arc_state_firmware_digest=arc_digest(arc_firmware,arc_firmware_size);arc_state_firmware_valid=true;}
   arc_state_expect(s,"identity.firmware",arc_state_firmware_digest.sha1,sizeof(arc_state_firmware_digest.sha1));
   arc_state_expect(s,"identity.options",&arc_core_options,sizeof(arc_core_options));
   arc_state_expect(s,"identity.media-count",&arc_media_count,sizeof(arc_media_count));
   for(unsigned i=0;i<arc_media_count;++i) {
      char key[64];snprintf(key,sizeof(key),"identity.media.%u",i);
      arc_state_expect(s,key,arc_media[i].digest.sha1,sizeof(arc_media[i].digest.sha1));
      if(arc_media[i].info.kind==ARC_MEDIA_JFD) {
         snprintf(key,sizeof(key),"identity.media-sha256.%u",i);
         arc_state_expect(s,key,arc_media[i].jfd_sha256,sizeof(arc_media[i].jfd_sha256));
      }
   }
   if(arc_state_loading(s)&&arc_state_pass(s)&&!arc_state_error(s)) {
      ARC_STATE_MATCH(s,arc_firmware_size);
      ARC_STATE_MATCH(s,arc_kind);ARC_STATE_MATCH(s,arc_loaded);ARC_STATE_MATCH(s,arc_media_count);ARC_STATE_MATCH(s,arc_session_machine);
      ARC_STATE_BOUND(s,arc_image_index,0,arc_media_count);
      ARC_STATE_BOUND(s,arc_video_width,1,4096);ARC_STATE_BOUND(s,arc_video_height,1,2048);
      size_t pitch,pixels;unsigned width;
      arc_state_project(s,&pitch,&arc_video_pitch,sizeof(pitch));arc_state_project(s,&width,&arc_video_width,sizeof(width));
      arc_state_project(s,&pixels,&arc_osk_surface_pixels,sizeof(pixels));
      if((pitch&&(pitch<(size_t)width*4u||pitch>4096u*4u))||pixels>4096u*2048u||
         pixels*4u!=arc_state_heap_size(s,"video.osk.pixels"))arc_state_fail(s,"presentation extent");
      double rate;arc_state_project(s,&rate,&arc_video_rate,sizeof(rate));
      if(!(rate>0&&rate<=1000.0))arc_state_fail(s,"frame rate");
      /* Frame-boundary accumulators are remainders, not arbitrary floating
       * values. Ordered comparisons also reject NaNs and infinities before
       * any live write or subsequent floating-to-integer conversion. */
      long double audio,x,y;
      arc_state_project(s,&audio,&arc_audio_fraction,sizeof(audio));
      arc_state_project(s,&x,&arc_stick_fraction_x,sizeof(x));
      arc_state_project(s,&y,&arc_stick_fraction_y,sizeof(y));
      if(!(audio>=0&&audio<1)||!(x>-1&&x<1)||!(y>-1&&y<1))
         arc_state_fail(s,"frontend remainders");
      ARC_STATE_BOUND(s,arc_inputs.fifo_count,0,ARC_INPUT_FIFO_CAPACITY);
      ArcInputState input;arc_state_project(s,&input,&arc_inputs,sizeof(input));
      for(unsigned i=0;i<input.fifo_count&&i<ARC_INPUT_FIFO_CAPACITY;++i) {
         const ArcInputEvent *event=&input.fifo[i];
         if(event->type<ARC_INPUT_EVENT_NONE||event->type>ARC_INPUT_EVENT_MOTION||
            (event->type==ARC_INPUT_EVENT_KEY&&event->code>=ARC_INPUT_KEY_COUNT)||
            (event->type==ARC_INPUT_EVENT_BUTTON&&event->code>=3u))arc_state_fail(s,"input event");
      }
      ARC_STATE_BOUND(s,arc_osk.key_count,0,ARC_OSK_MAX_KEYS);ARC_STATE_BOUND(s,arc_osk.activation_modifier_count,0,ARC_OSK_MODIFIER_COUNT);
      ARC_STATE_BOUND(s,arc_chooser.row_count,0,ARC_LAUNCH_CHOOSER_MAX_ROWS);
      ARC_STATE_BOUND(s,arc_boot_discovery.candidate_count,0,ARC_LAUNCH_MAX_CANDIDATES);
      ARC_STATE_BOUND(s,arc_chooser_discovery.candidate_count,0,ARC_LAUNCH_MAX_CANDIDATES);
      ArcLaunchPlan plan;arc_state_project(s,&plan,&arc_plan,sizeof(plan));
      if(plan.machine!=arc_plan.machine||plan.cpu!=arc_plan.cpu||plan.memory_kib!=arc_plan.memory_kib||plan.os!=arc_plan.os||
         memcmp(plan.content_id,arc_plan.content_id,sizeof(plan.content_id)))arc_state_fail(s,"machine plan");
      ArcCoreOptions options;arc_state_project(s,&options,&arc_core_options,sizeof(options));
      if(memcmp(&options,&arc_core_options,sizeof(options)))arc_state_fail(s,"effective options");
   }

}
