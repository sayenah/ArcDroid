/* D-120 veto completions through both real controller state machines.
 * Included by the controller golden driver. GPL-2.0-only. */
static void guard_controller_u24(uint8_t *p,unsigned value)
{ p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8u); p[2]=(uint8_t)(value>>16u); }
static bool guard_extra_write_density(ArcJfdImage *image,bool newio,
                              unsigned track_side,uint8_t id,unsigned density)
{
   if (newio)
   {
      uint8_t result[7];
      newio_reset(image,false);
      c82c711_fdc_write(7u,density==4u?0u:1u,NULL);
      c82c711_fdc_write(5u,0x0fu,NULL);
      c82c711_fdc_write(5u,0u,NULL);
      c82c711_fdc_write(5u,(uint8_t)(track_side/2u),NULL);
      newio_wait_irq(1100000u);
      c82c711_fdc_write(5u,0x08u,NULL);
      assert(c82c711_fdc_read(5u,NULL)==0x20u);
      assert(c82c711_fdc_read(5u,NULL)==track_side/2u);
      newio_service_dma=newio_dma_write=true;
      newio_transfer_limit=1024u; transfer_count=0u;
      newio_write_value=0x5au; newio_write_buffer=NULL;
      newio_creation_command(0x05u,(uint8_t)((track_side%2u)*4u),
         (uint8_t)(track_side/2u),(uint8_t)(track_side%2u),id,3u);
      newio_wait_irq(1100000u); newio_result(result);
      c82c711_fdc_shutdown();
      return !(result[0]&0xc0u);
   }
   controller_reset(image);
   wd1770_writelatch_b(density==1u?0x02u:0x00u);
   wd1770_write(0x0cu,(uint8_t)(track_side/2u));
   assert(finish_command(0x10u,1100000u)==0x80u);
   wd1770_writelatch_a(track_side%2u?0x8eu:0x9eu);
   wd1770_write(0x08u,id);
   wd_write_buffer=NULL; wd_write_mode=true; wd_write_value=0x5au;
   bool okay=finish_command(0xa0u,1100000u)==0x80u;
   wd_write_mode=false;
   return okay;
}
static bool guard_extra_write(ArcJfdImage *image,bool newio,
                              unsigned track_side,uint8_t id)
{ return guard_extra_write_density(image,newio,track_side,id,2u); }

static void test_guard_qa_controller(const char *root,bool newio)
{
   ArcJfdImage image=load_creation_image(root,"irregular-bad-crc-free.jfd");
   prepare_allocation_image(&image);
   assert(image.overlay->guarded && image.overlay->unavailable_slots==1u &&
          image.overlay->unavailable_mask[159]==0x10u);
   uint8_t base[1024],pending[1024];
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u); guard_controller_u24(pending+256u,3176u);
   pending[255]=allocation_old_checksum(pending);
   pending[511]=allocation_old_checksum(pending+256u);
   jfd_commits=0u;
   assert(!allocation_write_sector(&image,newio,0u,pending) && !jfd_commits &&
          !strcmp(refused_reason,"guard-unavailable") && !image.overlay->data[0]);
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);

   image=load_allocation_image(root,"guard-occupied-bad-crc.jfd");
   prepare_allocation_image(&image);
   assert(image.overlay->guarded && image.overlay->unavailable_slots==10u &&
          image.overlay->unavailable_mask[157]==31u &&
          image.overlay->unavailable_mask[159]==31u);
   for (unsigned id=0u;id<3u;++id)
      assert(!image.overlay->eligible[image.tracks[159].first_sector+id]);
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u); guard_controller_u24(pending+256u,3172u);
   pending[255]=allocation_old_checksum(pending);
   pending[511]=allocation_old_checksum(pending+256u);
   assert(allocation_write_sector(&image,newio,0u,pending));
   unsigned before=jfd_commits;
   assert(!allocation_write_sector(&image,newio,0u,base) && jfd_commits==before &&
          !strcmp(refused_reason,"guard-unavailable") &&
          !memcmp(image.overlay->data[0],pending,1024u));
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);

   image=load_allocation_image(root,"guard-captured-density.jfd");
   prepare_allocation_image(&image);
   assert(image.overlay->guarded && image.overlay->unavailable_slots==2u &&
          image.overlay->unavailable_mask[158]==0x10u &&
          image.overlay->unavailable_mask[159]==0x10u);
   unsigned side=newio?158u:159u;
   size_t index=image.tracks[side].first_sector+4u;
   assert(image.overlay->eligible[index]);
   before=jfd_commits;
   /* Select a density each controller represents: SD for WD, HD for 82C711.
    * Both targets are captured P-DD5 IDs in U, despite raw eligibility. */
   assert(!guard_extra_write_density(&image,newio,side,4u,newio?4u:1u) &&
          jfd_commits==before && !strcmp(refused_reason,"guard-unavailable") &&
          !image.overlay->data[index]);
   assert(allocation_write_sector(&image,newio,2u,base) && jfd_commits==before+1u);
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);

   image=load_allocation_image(root,"guard-occupied.jfd");
   prepare_allocation_image(&image);
   for (unsigned id=7u;id<=9u;++id)
      assert(guard_extra_write(&image,newio,159u,(uint8_t)id));
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);

   static const struct { const char *name; unsigned side; } tails[]={
      {"guard-extra-odd.jfd",160u},{"guard-extra-even.jfd",163u}
   };
   for (unsigned c=0u;c<2u;++c)
   {
      image=load_allocation_image(root,tails[c].name);
      prepare_allocation_image(&image); before=jfd_commits;
      assert(!guard_extra_write(&image,newio,tails[c].side,0u) &&
             jfd_commits==before && !strcmp(refused_reason,"guard-tail"));
      assert(guard_extra_write(&image,newio,tails[c].side,1u) &&
             jfd_commits==before+1u);
      if (newio) c82c711_fdc_shutdown();
      disc_close(0); arc_jfd_free(&image);
   }
}
static void test_guard_controller(const char *root,bool newio)
{
   test_guard_qa_controller(root,newio);
   ArcJfdImage image=load_allocation_image(root,"guard-crossing.jfd");
   prepare_allocation_image(&image);
   assert(image.overlay->guarded && image.overlay->map_valid);
   uint8_t base[1024],pending[1024];
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   jfd_commits=0u; jfd_refusals=0u;
   /* A data-first save stops at U with RNF, without publishing metadata. */
   assert(!allocation_write_sector(&image,newio,3u,NULL) && !jfd_commits &&
          !strcmp(refused_reason,"guard-unavailable") &&
          !image.overlay->data[0] && !image.overlay->creation_mask[0]);
   static const char *reasons[]={"guard-unavailable","guard-tail","guard-invalid-map","guard-size"};
   for (unsigned kind=0u;kind<4u;++kind)
   {
      memcpy(pending,base,1024u);
      if (kind==0u) { guard_controller_u24(pending,16u); guard_controller_u24(pending+256u,3247u); }
      if (kind==1u) guard_controller_u24(pending+256u,3250u);
      if (kind==3u) { guard_controller_u24(pending+252u,3264u); guard_controller_u24(pending+256u,3252u); }
      pending[255]=allocation_old_checksum(pending);
      pending[511]=allocation_old_checksum(pending+256u);
      if (kind==2u) pending[255]^=1u;
      unsigned before=jfd_commits;
      uint8_t allocation[ARC_JFD_PDD5_TRACKS];
      memcpy(allocation,image.overlay->allocation_mask,sizeof(allocation));
      assert(!allocation_write_sector(&image,newio,0u,pending) &&
             jfd_commits==before && !strcmp(refused_reason,reasons[kind]) &&
             !image.overlay->data[0] && image.overlay->map_valid &&
             !memcmp(allocation,image.overlay->allocation_mask,sizeof(allocation)));
      assert(allocation_write_sector(&image,newio,1u,base) && jfd_commits==before+1u);
   }
   image.overlay->map_valid=false;
   assert(!allocation_write_sector(&image,newio,0u,base) &&
          !strcmp(refused_reason,"guard-invalid-map"));
   arc_jfd_map_refresh(&image);
   assert(allocation_write_sector(&image,newio,0u,base));
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);
   image=load_allocation_image(root,"guard-extra.jfd");
   prepare_allocation_image(&image);
   jfd_commits=0u;
   assert(!guard_extra_write(&image,newio,160u,0u) && !jfd_commits &&
          !strcmp(refused_reason,"guard-tail"));
   assert(!guard_extra_write(&image,newio,163u,0u) && !jfd_commits &&
          !strcmp(refused_reason,"guard-tail"));
   assert(guard_extra_write(&image,newio,163u,1u) && jfd_commits==1u &&
          image.overlay->data[image.tracks[163].first_sector+1u][0]==0x5au);
   assert(guard_extra_write(&image,newio,164u,0u) && jfd_commits==2u &&
          image.overlay->data[image.tracks[164].first_sector][0]==0x5au);
   disc_close(0); arc_jfd_free(&image);
   /* D-121: allocated U in the base is admitted; comparisons must retain
    * exact units even when every candidate has the same C-11c slot mask. */
   image=load_allocation_image(root,"guard-occupied.jfd");
   prepare_allocation_image(&image);
   assert(image.overlay->guarded && image.overlay->unavailable_slots==10u &&
          image.overlay->unavailable_mask[157]==31u &&
          image.overlay->unavailable_mask[159]==31u);
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u);
   guard_controller_u24(pending+256u,3170u); /* Free units 3180 and 3181. */
   pending[255]=allocation_old_checksum(pending);
   pending[511]=allocation_old_checksum(pending+256u);
   assert(allocation_write_sector(&image,newio,0u,pending));
   uint8_t current[1024]; memcpy(current,pending,1024u);
   for (unsigned kind=0u;kind<3u;++kind)
   {
      memcpy(pending,current,1024u);
      if (kind<2u) guard_controller_u24(pending+256u,kind?3168u:3169u);
      else
      {
         /* Exchange allocated 3182 for free 3181, without changing count. */
         guard_controller_u24(pending+256u,3169u);
         guard_controller_u24(pending+3u,3182u);
         guard_controller_u24(pending+259u,1u);
         guard_controller_u24(pending+6u,3200u);
         guard_controller_u24(pending+262u,63u); pending[510]=9u;
      }
      pending[255]=allocation_old_checksum(pending);
      pending[511]=allocation_old_checksum(pending+256u);
      uint8_t mask[ARC_JFD_PDD5_TRACKS]; uint32_t declared; bool tail;
      assert(arc_jfd_old_declared(pending,mask,&declared,&tail) &&
             !memcmp(mask,image.overlay->allocation_mask,sizeof(mask)));
      unsigned before=jfd_commits;
      assert(!allocation_write_sector(&image,newio,0u,pending) &&
             jfd_commits==before && !strcmp(refused_reason,"guard-unavailable") &&
             !memcmp(image.overlay->data[0],current,1024u));
      assert(allocation_write_sector(&image,newio,2u,base) && jfd_commits==before+1u);
   }
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);
   image=load_allocation_image(root,"guard-partial-u.jfd");
   prepare_allocation_image(&image);
   memcpy(base,image.decoded+image.sectors[0].data_offset,1024u);
   memcpy(pending,base,1024u); guard_controller_u24(pending,14u);
   guard_controller_u24(pending+256u,3249u);
   pending[255]=allocation_old_checksum(pending);
   pending[511]=allocation_old_checksum(pending+256u);
   uint8_t mask[ARC_JFD_PDD5_TRACKS]; uint32_t declared; bool tail;
   assert(arc_jfd_old_declared(pending,mask,&declared,&tail) &&
          !memcmp(mask,image.overlay->allocation_mask,sizeof(mask)));
   unsigned before=jfd_commits;
   assert(!allocation_write_sector(&image,newio,0u,pending) &&
          jfd_commits==before && !strcmp(refused_reason,"guard-unavailable") &&
          !image.overlay->data[0]);
   assert(allocation_write_sector(&image,newio,2u,base) && jfd_commits==before+1u);
   if (newio) c82c711_fdc_shutdown();
   disc_close(0); arc_jfd_free(&image);
   puts(newio?"jfd-guard-newio: RNF, all veto errors, preserved map and continued writes passed":
              "jfd-guard-wd: RNF, all veto errors, preserved map and continued writes passed");
}
