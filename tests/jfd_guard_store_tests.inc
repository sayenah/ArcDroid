/* D-120 declared-map, policy separation and persistent-store evidence.
 * Included by the existing VFS test driver. GPL-2.0-only. */
static void guard_u24(uint8_t *p, unsigned value)
{ p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8u); p[2]=(uint8_t)(value>>16u); }
static void guard_checksums(uint8_t *map)
{
   map[255]=old_map_checksum(map);
   map[511]=old_map_checksum(map+256u);
}
static void guard_pending(uint8_t map[1024], unsigned kind)
{
   if (kind==0u) { guard_u24(map,16u); guard_u24(map+256u,3247u); }
   if (kind==1u) guard_u24(map+256u,3250u);
   if (kind==2u) map[255]^=1u;
   if (kind==3u)
   { guard_u24(map+252u,3264u); guard_u24(map+256u,3252u); }
   if (kind!=2u) guard_checksums(map);
}
static void test_guard_store(const char *root)
{
   static const struct { const char *name; bool supported, guarded; } cases[]={
      {"guard-crossing.jfd",true,true}, {"guard-physical.jfd",true,true},
      {"guard-one-tail.jfd",true,true}, {"guard-split-tail.jfd",true,true},
      {"guard-extra.jfd",true,true}, {"guard-u-allocated.jfd",true,true},
      {"guard-tail-allocated.jfd",false,false}, {"guard-partial-u.jfd",true,true},
      {"guard-sparse-crossing.jfd",true,false},
      {"guard-profile-fail.jfd",false,false},
      {"guard-allocated-profile-fail.jfd",false,false},
      {"guard-placement-fail.jfd",false,false}
   };
   const char *why=NULL;
   for (unsigned c=0u;c<sizeof(cases)/sizeof(cases[0]);++c)
   {
      clear_files();
      ArcJfdImage image=allocation_fixture(root,cases[c].name);
      assert(arc_jfd_store_load(&image,"save",true,false,1u,&why));
      ArcJfdOverlay *o=image.overlay;
      assert(o->layout_supported==cases[c].supported &&
             o->guarded==cases[c].guarded && o->writable==cases[c].supported);
      if (o->guarded)
      {
         assert(o->unavailable_mask[0]==8u && o->unavailable_slots==1u &&
                !o->creation_mask[0] && o->map_kind==1u && o->map_valid);
         ArcJfdView v;
         assert(!arc_jfd_effective_sector(&image,0u,
                   image.tracks[0].sector_count+3u,false,&v));
         assert(arc_jfd_effective_sector(&image,0u,1u,false,&v) &&
                v.write_eligible && v.key==1u && v.sector.size==1024u);
         if (image.track_count>160u)
         {
            assert(!o->creation_mask[159] || o->creation_capacity<=800u);
            uint8_t data[1024]={0};
            assert(!strcmp(arc_jfd_write_veto_reason(&image,
                       image.tracks[160].first_sector,data),"guard-tail"));
            /* The final T unit intersects ID 0 on side 163. Captured
             * sectors wholly beyond T retain D-075; they add no capacity. */
            assert(!strcmp(arc_jfd_write_veto_reason(&image,
                       image.tracks[163].first_sector,data),"guard-tail"));
            size_t outside=image.tracks[163].first_sector+1u;
            assert(!arc_jfd_write_veto_reason(&image,outside,data) &&
                   o->eligible[outside] && o->commit(&image,outside,data));
            assert(!arc_jfd_write_veto_reason(&image,
                       image.tracks[164].first_sector,data));
            assert(!arc_jfd_guard_tail_target(o,160u,9u));
            ArcJfdOverlay empty_tail=*o;
            empty_tail.guard_declared_units=3200u;
            empty_tail.guard_tail_units=0u;
            assert(!arc_jfd_guard_tail_target(&empty_tail,160u,0u));
         }
      }
      /* C-2 regression: the legacy free reader still rejects declared-tail
       * extents, even when the guarded parser accepts them. */
      if (strcmp(cases[c].name,"guard-physical.jfd"))
      {
         bool free_track[ARC_JFD_PDD5_TRACKS];
         if (strcmp(cases[c].name,"guard-tail-allocated.jfd"))
            assert(!arc_jfd_base_free_tracks(&image,free_track));
      }
      if (!o->guarded)
      {
         o->map_enabled=true; arc_jfd_map_prepare(&image);
         assert(o->map_kind==0u && !o->map_valid);
         uint8_t zeros[ARC_JFD_PDD5_TRACKS]={0};
         assert(!memcmp(o->allocation_mask,zeros,sizeof(zeros)));
         uint8_t map[1024]; memcpy(map,image.decoded+image.sectors[0].data_offset,1024u);
         o->probe_mask[0]=8u;
         assert(!arc_jfd_map_write_veto(&image,0u,map));
         ArcJfdView v;
         assert(!arc_jfd_effective_sector(&image,0u,
                  image.tracks[0].sector_count+3u,false,&v));
         for (size_t i=0u;i<image.sector_count;++i)
         {
            unsigned t=0u;
            while (i >= (size_t)image.tracks[t].first_sector+image.tracks[t].sector_count) ++t;
            assert(arc_jfd_effective_sector(&image,t,
                     (unsigned)(i-image.tracks[t].first_sector),false,&v) &&
                   !memcmp(v.data,image.decoded+image.sectors[i].data_offset,v.sector.size));
         }
      }
      if (!strcmp(cases[c].name,"guard-sparse-crossing.jfd"))
      {
         arc_jfd_prepare_identification(&image,true);
         assert(!strcmp(image.identification_reason,"presented") &&
                image.identification_mask==8u && !o->map_valid);
         ArcJfdView v;
         assert(arc_jfd_effective_sector(&image,0u,image.tracks[0].sector_count+3u,false,&v) &&
                !v.data && v.write_eligible);
      }
      arc_jfd_free(&image);
   }
   /* Quarantine and new maps retain their explicit gate rejection. */
   clear_files();
   ArcJfdImage image=allocation_fixture(root,"guard-crossing.jfd");
   image.tracks[0].quarantine_reason="size-code";
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          !image.overlay->writable && !image.overlay->guarded);
   arc_jfd_free(&image);
   image=creation_fixture(root,"irregular-newmap-free.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          !image.overlay->writable && !image.overlay->guarded);
   arc_jfd_free(&image);

   clear_files(); image=allocation_fixture(root,"guard-crossing.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why));
   uint8_t base[1024], pending[1024], mask[ARC_JFD_PDD5_TRACKS];
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   uint32_t declared=0u; bool tail=false;
   assert(arc_jfd_old_declared(base,mask,&declared,&tail) && declared==3263u && !tail);
   /* Zero lengths, overlapping/descending entries and declared bounds. */
   for (unsigned kind=0u;kind<4u;++kind)
   {
      memcpy(pending,base,1024u);
      if (kind==0u) guard_u24(pending+256u,0u);
      if (kind==1u || kind==2u)
      {
         pending[510]=6u; guard_u24(pending+3u,kind==1u?13u:1u);
         guard_u24(pending+259u,1u);
      }
      if (kind==3u) guard_u24(pending+256u,3252u);
      guard_checksums(pending);
      assert(!arc_jfd_old_declared(pending,mask,&declared,&tail));
   }
   /* A quarter-sector allocation is already an unavailable-byte claim. */
   memcpy(pending,base,1024u); guard_u24(pending,13u); guard_u24(pending+256u,3250u);
   guard_checksums(pending);
   assert(!strcmp(arc_jfd_write_veto_reason(&image,0u,pending),"guard-unavailable"));
   static const char *reasons[]={"guard-unavailable","guard-tail","guard-invalid-map","guard-size"};
   for (unsigned kind=0u;kind<4u;++kind)
   {
      memcpy(pending,base,1024u); guard_pending(pending,kind);
      assert(!strcmp(arc_jfd_write_veto_reason(&image,0u,pending),reasons[kind]));
      assert(!image.overlay->commit(&image,0u,pending) && !image.overlay->failed &&
             image.overlay->writable && !image.overlay->generation && !image.overlay->records &&
             !files[0].exists && !files[1].exists && !files[2].exists &&
             image.overlay->map_valid);
   }
   image.overlay->map_valid=false;
   assert(!strcmp(arc_jfd_write_veto_reason(&image,0u,base),"guard-invalid-map"));
   arc_jfd_map_refresh(&image);
   image.overlay->data=calloc(image.sector_count,sizeof(*image.overlay->data));
   assert(image.overlay->data);
   image.overlay->data[0]=malloc(1024u); assert(image.overlay->data[0]);
   memcpy(image.overlay->data[0],base,1024u); image.overlay->data[0][255]^=1u;
   /* Even a stale valid flag cannot bypass the current-map validation. */
   assert(!strcmp(arc_jfd_write_veto_reason(&image,0u,base),"guard-invalid-map"));
   free(image.overlay->data[0]); image.overlay->data[0]=NULL;
   /* U stays permanent while D-080 marks coexist on creatable side 3. */
   image.overlay->probe_mask[3]=1u;
   memcpy(pending,base,1024u); guard_u24(pending+256u,48u);
   pending[510]=6u; guard_u24(pending+3u,64u); guard_u24(pending+259u,3199u);
   guard_checksums(pending);
   assert(!strcmp(arc_jfd_write_veto_reason(&image,0u,pending),"probed-unformatted"));
   image.overlay->probe_mask[3]=0u;
   assert(!arc_jfd_map_write_veto(&image,0u,pending));
   assert(image.overlay->commit(&image,0u,pending) && blank_at(&image,3u,0u));
   uint8_t merged[1024]={0}; memset(merged+256u,0x5a,256u);
   assert(image.overlay->commit_created(&image,3u,0u,merged));
   /* Refusal after a saved allocation must preserve bytes and the effective
    * view as well as the generation/record counters. */
   char saved_hash[3][41];
   for (unsigned f=0u;f<3u;++f) arc_sha1_hex(files[f].bytes,files[f].size,saved_hash[f]);
   uint8_t saved_allocation[ARC_JFD_PDD5_TRACKS];
   memcpy(saved_allocation,image.overlay->allocation_mask,sizeof(saved_allocation));
   memcpy(pending,base,1024u); guard_pending(pending,0u);
   assert(!image.overlay->commit(&image,0u,pending) &&
          image.overlay->generation==2u && image.overlay->records==2u &&
          !memcmp(saved_allocation,image.overlay->allocation_mask,sizeof(saved_allocation)) &&
          !memcmp(image.overlay->created[3][0],merged,1024u));
   for (unsigned f=0u;f<3u;++f)
   { char after[41]; arc_sha1_hex(files[f].bytes,files[f].size,after); assert(!strcmp(after,saved_hash[f])); }
   uint64_t generation=image.overlay->generation;
   unsigned records=image.overlay->records;
   arc_jfd_free(&image);
   image=allocation_fixture(root,"guard-crossing.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          image.overlay->writable && image.overlay->map_valid &&
          image.overlay->generation==generation && image.overlay->records==records &&
          !memcmp(image.overlay->created[3][0],merged,1024u) &&
          !image.overlay->probe_mask[3]);
   /* Checksummed v2 saved metadata that violates U, T, size or syntax.
    * Keep created records, both slot files and a stale candidate untouched. */
   arc_jfd_free(&image);
   unsigned active=files[0].exists && files[1].exists ?
      (files[0].bytes[68]>files[1].bytes[68]?0u:1u):files[0].exists?0u:1u;
   files[2].exists=true; files[2].size=3u; files[2].bytes=malloc(3u); assert(files[2].bytes);
   memcpy(files[2].bytes,"tmp",3u);
   char identity[41];
   ArcJfdImage candidate_image=allocation_fixture(root,"guard-crossing.jfd");
   arc_sha1_hex(candidate_image.decoded,candidate_image.decoded_size,identity);
   arc_jfd_free(&candidate_image);
   snprintf(files[2].path,sizeof(files[2].path),"save/arcdroid/jfd/%s.tmp",identity);
   uint8_t *snapshot=malloc(files[active].size); assert(snapshot);
   memcpy(snapshot,files[active].bytes,files[active].size);
   for (unsigned kind=0u;kind<4u;++kind)
   {
      memcpy(files[active].bytes,snapshot,files[active].size);
      memcpy(files[active].bytes+89u,base,1024u);
      guard_pending(files[active].bytes+89u,kind); fix_checksum(&files[active]);
      char digests[3][41];
      for (unsigned f=0u;f<3u;++f) arc_sha1_hex(files[f].bytes,files[f].size,digests[f]);
      image=allocation_fixture(root,"guard-crossing.jfd");
      assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
             !strcmp(why,"guard-violated") && image.overlay->guard_violated &&
             !image.overlay->writable && !image.overlay->map_enabled &&
             !image.overlay->map_valid && !image.overlay->damaged &&
             image.overlay->generation==generation && image.overlay->records==records);
      ArcJfdView v;
      assert(arc_jfd_effective_sector(&image,0u,0u,false,&v) &&
             !memcmp(v.data,files[active].bytes+89u,1024u));
      assert(arc_jfd_effective_sector(&image,3u,0u,false,&v) &&
             !memcmp(v.data,merged,1024u));
      arc_jfd_file_data_prepare(&image,true,false,"guard-violated");
      assert(!image.file_data.presented && !strcmp(image.file_data.reason,"guard-violated"));
      assert(!image.overlay->commit(&image,1u,merged));
      arc_jfd_free(&image);
      image=allocation_fixture(root,"guard-crossing.jfd");
      assert(arc_jfd_store_load(&image,"save",false,false,1u,&why) &&
             !strcmp(why,"disk-writes-off") && !image.overlay->visible);
      assert(arc_jfd_effective_sector(&image,0u,0u,false,&v) && !memcmp(v.data,base,1024u));
      assert(!arc_jfd_effective_sector(&image,3u,0u,false,&v));
      arc_jfd_file_data_prepare(&image,true,false,"disk-writes-off");
      assert(strcmp(image.file_data.reason,"guard-violated"));
      arc_jfd_free(&image);
      for (unsigned f=0u;f<3u;++f)
      { char after[41]; arc_sha1_hex(files[f].bytes,files[f].size,after); assert(!strcmp(after,digests[f])); }
   }
   free(snapshot); clear_files();
   /* A real base-only file-data fallback must disappear beside visible saved
    * metadata, then return with Off. Both saved record kinds remain readable. */
   image=file_data_fixture(root,"guard-terminal.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) && image.overlay->guarded);
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   assert(image.overlay->commit(&image,0u,base));
   memset(merged,0x77,1024u);
   assert(image.overlay->commit(&image,1u,merged) &&
          image.overlay->commit_created(&image,1u,1u,merged));
   arc_jfd_free(&image);
   active=files[0].bytes[68]>files[1].bytes[68]?0u:1u;
   uint8_t *bad=files[active].bytes+89u;
   guard_u24(bad,40u); guard_u24(bad+256u,3140u);
   bad[510]=6u; guard_u24(bad+3u,3200u); guard_u24(bad+259u,63u);
   guard_checksums(bad); fix_checksum(&files[active]);
   for (unsigned f=0u;f<3u;++f) arc_sha1_hex(files[f].bytes,files[f].size,saved_hash[f]);
   image=file_data_fixture(root,"guard-terminal.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          !strcmp(why,"guard-violated"));
   arc_jfd_file_data_prepare(&image,true,false,"guard-violated");
   assert(!image.file_data.presented && !image.file_data.mask[1]);
   ArcJfdView visible;
   assert(arc_jfd_effective_sector(&image,0u,1u,false,&visible) && visible.data[0]==0x77u);
   assert(arc_jfd_effective_sector(&image,1u,image.tracks[1].sector_count+1u,false,&visible) &&
          visible.data[0]==0x77u && !visible.write_eligible);
   arc_jfd_free(&image);
   image=file_data_fixture(root,"guard-terminal.jfd");
   assert(arc_jfd_store_load(&image,"save",false,false,1u,&why));
   arc_jfd_file_data_prepare(&image,true,false,"disk-writes-off");
   assert(image.file_data.presented==1u && image.file_data.mask[1]==2u);
   assert(arc_jfd_effective_sector(&image,1u,image.tracks[1].sector_count+1u,false,&visible) &&
          visible.data[0]==0u && !visible.write_eligible);
   arc_jfd_free(&image);
   for (unsigned f=0u;f<3u;++f)
   { char after[41]; arc_sha1_hex(files[f].bytes,files[f].size,after); assert(!strcmp(after,saved_hash[f])); }
   clear_files();
   /* D-121: exact units, not allocation counts or sector masks. */
   image=allocation_fixture(root,"guard-occupied.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          image.overlay->guarded && image.overlay->writable &&
          image.overlay->unavailable_slots==10u &&
          image.overlay->allocation_mask[159]==31u);
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u); guard_u24(pending+256u,3170u);
   guard_checksums(pending);
   assert(image.overlay->commit(&image,0u,pending));
   uint8_t current[1024]; memcpy(current,pending,1024u);
   for (unsigned kind=0u;kind<3u;++kind)
   {
      memcpy(pending,current,1024u);
      if (kind<2u) guard_u24(pending+256u,kind?3168u:3169u);
      else
      {
         guard_u24(pending+256u,3169u); guard_u24(pending+3u,3182u);
         guard_u24(pending+259u,1u); guard_u24(pending+6u,3200u);
         guard_u24(pending+262u,63u); pending[510]=9u;
      }
      guard_checksums(pending);
      assert(arc_jfd_old_declared(pending,mask,&declared,&tail) &&
             !memcmp(mask,image.overlay->allocation_mask,sizeof(mask)));
      for (unsigned f=0u;f<3u;++f) arc_sha1_hex(files[f].bytes,files[f].size,saved_hash[f]);
      assert(!image.overlay->commit(&image,0u,pending) &&
             image.overlay->generation==1u && image.overlay->records==1u &&
             !image.overlay->failed && image.overlay->map_valid &&
             !memcmp(image.overlay->data[0],current,1024u));
      for (unsigned f=0u;f<3u;++f)
      { char after[41]; arc_sha1_hex(files[f].bytes,files[f].size,after); assert(!strcmp(after,saved_hash[f])); }
   }
   arc_jfd_free(&image);
   image=allocation_fixture(root,"guard-occupied.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why) &&
          image.overlay->writable && image.overlay->generation==1u &&
          !image.overlay->commit(&image,0u,base));
   arc_jfd_free(&image); clear_files();
   /* Another allocation unit within a partially allocated U slot. */
   image=allocation_fixture(root,"guard-partial-u.jfd");
   assert(arc_jfd_store_load(&image,"save",true,false,1u,&why));
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u); guard_u24(pending,14u);
   guard_u24(pending+256u,3249u); guard_checksums(pending);
   assert(arc_jfd_old_declared(pending,mask,&declared,&tail) &&
          !memcmp(mask,image.overlay->allocation_mask,sizeof(mask)) &&
          !image.overlay->commit(&image,0u,pending) && !image.overlay->generation);
   arc_jfd_free(&image); clear_files();
   puts("jfd-guard-store: admission, declared parser, non-guarded regressions, veto, persistence, violating stores and Off passed");
}
