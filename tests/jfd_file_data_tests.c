/* Project-authored R1' proof fixtures. GPL-2.0-only. */
#include "jfd.h"
#include "jfd_file_data_limits.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ArcJfdImage load(const char *root,const char *name)
{
   char path[4096],error[256];
   int n=snprintf(path,sizeof(path),"%s/jfd-file-data/%s",root,name);
   assert(n>0 && (size_t)n<sizeof(path));
   FILE *f=fopen(path,"rb"); assert(f);
   assert(!fseek(f,0,SEEK_END)); long size=ftell(f); assert(size>0);
   assert(!fseek(f,0,SEEK_SET));
   uint8_t *raw=malloc((size_t)size); assert(raw);
   assert(fread(raw,1u,(size_t)size,f)==(size_t)size); assert(!fclose(f));
   ArcJfdImage image;
   bool gzip=(size_t)size>=2u && raw[0]==0x1fu && raw[1]==0x8bu;
   bool okay=gzip ? arc_jfd_parse(raw,(size_t)size,&image,error,sizeof(error)) :
                     arc_jfd_parse_owned(raw,(size_t)size,&image,error,sizeof(error));
   if (gzip) free(raw);
   if (!okay)
   { fprintf(stderr,"%s: %s\n",name,error); abort(); }
   return image;
}
static void expect(const ArcJfdImage *image,unsigned side,unsigned id,bool yes)
{
   ArcJfdView view;
   bool got=arc_jfd_effective_sector(image,side,
      image->tracks[side].sector_count+id,false,&view);
   assert(got==yes);
   if (yes)
   {
      assert(view.id_visible && view.data && !view.write_eligible &&
             !view.sector.flags && view.sector.size==1024u &&
             view.sector.density==2u && view.sector.id==id);
      for (unsigned i=0u;i<1024u;++i) assert(view.data[i]==0u);
   }
}
int main(int argc,char **argv)
{
   assert(argc==2);
   assert(arc_jfd_file_data_limits_ok(16u,512u,8192u,65536u));
   assert(!arc_jfd_file_data_limits_ok(17u,0u,0u,0u));
   assert(!arc_jfd_file_data_limits_ok(0u,513u,0u,0u));
   assert(!arc_jfd_file_data_limits_ok(0u,0u,8193u,0u));
   assert(!arc_jfd_file_data_limits_ok(0u,0u,0u,65537u));
   /* Directory and resolution-call boundaries cannot be reached by ordinary
    * 800 KiB discs; keep their arithmetic guards explicit. */
   ArcJfdImage d=load(argv[1],"d-terminal.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") &&
          d.file_data.format=='D' && d.file_data.valid_copies==1u &&
          d.file_data.mask[1]==2u);
   expect(&d,1u,1u,true); expect(&d,2u,0u,false);
   unsigned mask=d.file_data.mask[1];
   d.game_id=0x12345678u; strcpy(d.title,"Another invented identity");
   arc_jfd_file_data_prepare(&d,true,false,"no-flag");
   assert(d.file_data.mask[1]==mask);
   d.tracks[1].quarantine_reason="invented-quarantine";
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.presented && d.file_data.skipped_sides>=1u);
   d.tracks[1].quarantine_reason=NULL;
   uint32_t saved_times[4];
   for (unsigned i=0u;i<4u;++i)
   {
      saved_times[i]=d.sectors[d.tracks[1].first_sector+i].time_us;
      d.sectors[d.tracks[1].first_sector+i].time_us=180000u+i*1000u;
   }
   d.tracks[1].timed=false;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"unplaceable") &&
          d.file_data.format=='D' && d.file_data.valid_copies==1u &&
          d.file_data.agreeing_copies==1u && !d.file_data.presented &&
          d.file_data.skipped_sides==1u && !d.file_data.incomplete_dirs);
   for (unsigned i=0u;i<4u;++i)
      d.sectors[d.tracks[1].first_sector+i].time_us=saved_times[i];
   arc_jfd_file_data_prepare(&d,true,true,"wp-flag");
   assert(!strcmp(d.file_data.reason,"option-strict")); expect(&d,1u,1u,false);
   arc_jfd_file_data_prepare(&d,true,false,"writable");
   assert(!strcmp(d.file_data.reason,"writable-c11c")); expect(&d,1u,1u,false);
   arc_jfd_file_data_prepare(&d,true,false,"damaged-store");
   assert(!strcmp(d.file_data.reason,"damaged-store") &&
          d.file_data.format=='D' && !d.file_data.valid_copies &&
          !d.file_data.agreeing_copies && !d.file_data.presented &&
          !d.file_data.skipped_sides && !d.file_data.incomplete_dirs);
   expect(&d,1u,1u,false);
   arc_jfd_file_data_prepare(&d,true,false,"disk-writes-off");
   assert(!strcmp(d.file_data.reason,"presented")); expect(&d,1u,1u,true);
   d.overlay=calloc(1u,sizeof(*d.overlay)); assert(d.overlay);
   d.overlay->data=calloc(d.sector_count,sizeof(*d.overlay->data));
   assert(d.overlay->data);
   d.overlay->data[d.tracks[0].first_sector]=calloc(1024u,1u);
   assert(d.overlay->data[d.tracks[0].first_sector]);
   d.overlay->visible=true;
   d.overlay->creation_mask[1u]=2u;
   d.overlay->created[1u][1u]=malloc(1024u);
   assert(d.overlay->created[1u][1u]);
   memset(d.overlay->created[1u][1u],0x5au,1024u);
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1u]==2u); /* Saved map replacement was ignored. */
   ArcJfdView created_view;
   assert(arc_jfd_effective_sector(&d,1u,
      d.tracks[1u].sector_count+1u,false,&created_view));
   assert(created_view.data==d.overlay->created[1u][1u] &&
          created_view.data[0]==0x5au); /* Existing created data wins. */
   uint32_t created_time=created_view.sector.time_us;
   arc_jfd_file_data_prepare(&d,true,true,"wp-flag");
   assert(arc_jfd_effective_sector(&d,1u,
          d.tracks[1u].sector_count+1u,false,&created_view) &&
          created_view.data==d.overlay->created[1u][1u] &&
          created_view.data[0]==0x5au &&
          created_view.sector.time_us==created_time);
   arc_jfd_free(&d);
   d=load(argv[1],"d-recovery.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1u]==2u &&
          !strcmp(d.file_data.reason,"presented"));
   arc_jfd_free(&d);
   d=load(argv[1],"d-terminal-gzip.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==2u);
   arc_jfd_free(&d);
   d=load(argv[1],"d-terminal-166.jfd");
   assert(d.track_count==166u);
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==2u && !strcmp(d.file_data.reason,"presented"));
   arc_jfd_free(&d);
   d=load(argv[1],"d-terminal.jfd");
   unsigned side_first=d.tracks[1u].first_sector;
   d.sectors[side_first].flags=ARC_JFD_DATA_CRC_BAD;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.mask[1u] && d.file_data.skipped_sides==1u);
   d.sectors[side_first].flags=0u;
   uint32_t time=d.sectors[side_first+1u].time_us;
   d.sectors[side_first+1u].time_us=d.sectors[side_first].time_us;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.mask[1u] && d.file_data.skipped_sides==1u);
   d.sectors[side_first+1u].time_us=time;
   uint32_t offset=d.sectors[side_first+1u].data_offset;
   d.sectors[side_first+1u].data_offset=d.sectors[side_first].data_offset;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.mask[1u] && d.file_data.skipped_sides==1u);
   d.sectors[side_first+1u].data_offset=offset;
   d.sectors[d.tracks[0u].first_sector].flags=ARC_JFD_DATA_CRC_BAD;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"format") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-terminal.jfd");
   arc_jfd_file_data_prepare(&d,false,false,"wp-flag");
   assert(!d.file_data.presented && !d.file_data.mask[1u] &&
          !d.file_data.valid_copies);
   arc_jfd_file_data_prepare(&d,true,false,"store-unavailable");
   assert(d.file_data.mask[1u]==2u && d.file_data.presented==1u);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"d-captured-missing-data.jfd":
                       "d-captured-data-crc.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(d.file_data.mask[1u]==2u &&
             d.sectors[d.tracks[0u].first_sector+4u].id==4u);
      assert(i ? d.sectors[d.tracks[0u].first_sector+4u].data_offset==UINT32_MAX :
                 (d.sectors[d.tracks[0u].first_sector+4u].flags &
                  ARC_JFD_DATA_CRC_BAD)!=0u);
      arc_jfd_free(&d);
   }
   d=load(argv[1],"d-slack.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==2u);
   expect(&d,1u,1u,true); expect(&d,1u,3u,false);
   arc_jfd_free(&d);
   d=load(argv[1],"d-no-valid-map.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"no-valid-map") && !d.file_data.presented);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"d-map-second-check.jfd":"d-map-first-check.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(!strcmp(d.file_data.reason,"no-valid-map") &&
             d.file_data.valid_copies==0u && !d.file_data.presented);
      arc_jfd_free(&d);
   }

   d=load(argv[1],"d-root-invalid.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"root-invalid") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-sequence-invalid.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"root-invalid") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-marker-invalid.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"root-invalid") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-nick-valid.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") && d.file_data.mask[1u]==2u);
   arc_jfd_free(&d);
   d=load(argv[1],"d-nick-invalid.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"root-invalid") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-zero-length.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"none-eligible")); expect(&d,1u,1u,false);
   arc_jfd_free(&d);
   d=load(argv[1],"d-two-empty.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"none-eligible")); expect(&d,1u,1u,false);
   arc_jfd_free(&d);
   d=load(argv[1],"d-boundary.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.presented); expect(&d,1u,1u,false);
   arc_jfd_free(&d);
   d=load(argv[1],"d-one-byte.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[0]==8u); expect(&d,0u,3u,true);
   /* D-089 ID-only slots are excluded from the R1' mask itself. */
   d.identification_schedule=5u;
   d.identification_mask=(uint16_t)(1u<<3u);
   d.identification_time[3u]=12345u;
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!(d.file_data.mask[0] & 8u) && !d.file_data.presented);
   ArcJfdView priority;
   assert(arc_jfd_effective_sector(&d,0u,
          d.tracks[0u].sector_count+3u,false,&priority) &&
          priority.sector.time_us==12345u && priority.id_visible &&
          !priority.data);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<6u;++i)
   {
      static const char *invalid[] = {"d-invalid-object.jfd", "d-cycle.jfd",
         "d-alias.jfd", "d-overlap.jfd", "d-outside-declared.jfd",
         "d-free-overlap.jfd"};
      d=load(argv[1],invalid[i]);
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(!strcmp(d.file_data.reason,"object-invalid") &&
             !d.file_data.presented);
      arc_jfd_free(&d);
   }
   d=load(argv[1],"d-child-incomplete.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==2u && d.file_data.incomplete_dirs==1u);
   arc_jfd_free(&d);
   d=load(argv[1],"d-child-bad-check.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"none-eligible") &&
          d.file_data.incomplete_dirs==1u && !d.file_data.mask[1u] &&
          !d.file_data.mask[2u]);
   arc_jfd_free(&d);
   d=load(argv[1],"d-child-bad-check-omitted.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") &&
          d.file_data.incomplete_dirs==1u && d.file_data.mask[1u]==2u &&
          !d.file_data.mask[2u]);
   expect(&d,1u,1u,true);
   arc_jfd_free(&d);
   d=load(argv[1],"d-valid-child-desc-omitted.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1u]==8u && !d.file_data.incomplete_dirs);
   expect(&d,1u,3u,true);
   arc_jfd_free(&d);
   d=load(argv[1],"d-bad-child-desc-omitted.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.mask[1u] && d.file_data.incomplete_dirs==1u);
   expect(&d,1u,3u,false);
   arc_jfd_free(&d);
   d=load(argv[1],"d-object-in-map-sector.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"object-invalid") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"d-track0-file-free.jfd");
   arc_jfd_prepare_identification(&d,true);
   assert(!d.identification_mask); /* D-089 sees allocated, not-free ID 3. */
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.identification_mask && d.file_data.mask[0u]==8u &&
          d.file_data.presented==1u);
   expect(&d,0u,3u,true); expect(&d,0u,4u,false);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"d-clamped-declaration.jfd":"d-small-declaration.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(d.file_data.mask[1]==2u);
      arc_jfd_free(&d);
   }
   d=load(argv[1],"d-irregular.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!d.file_data.presented && d.file_data.skipped_sides>=1u);
   arc_jfd_free(&d);
   d=load(argv[1],"d-depth-16.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") &&
          d.file_data.mask[6]==8u);
   arc_jfd_free(&d);
   d=load(argv[1],"d-depth-17.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"limit") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"e-terminal.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") &&
          d.file_data.format=='E' && d.file_data.valid_copies==2u &&
          d.file_data.agreeing_copies==2u && d.file_data.mask[1]==4u);
   expect(&d,1u,2u,true);
   arc_jfd_free(&d);
   d=load(argv[1],"e-fragmented.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==4u && d.file_data.mask[2]==1u);
   expect(&d,1u,2u,true); expect(&d,2u,0u,true);
   arc_jfd_free(&d);
   d=load(argv[1],"e-shared.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[1]==4u);
   arc_jfd_free(&d);
   d=load(argv[1],"e-system-shared.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(d.file_data.mask[0]==16u);
   expect(&d,0u,4u,true);
   arc_jfd_free(&d);
   d=load(argv[1],"e-conflict.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"map-conflict") && !d.file_data.presented);
   arc_jfd_free(&d);
   d=load(argv[1],"e-agree-length-control.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") &&
          d.file_data.valid_copies==2u && d.file_data.agreeing_copies==2u);
   arc_jfd_free(&d);
   d=load(argv[1],"e-agree-length-conflict.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"map-conflict") &&
          d.file_data.valid_copies==2u &&
          d.file_data.agreeing_copies==0u && !d.file_data.presented);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"e-first-walk-fails.jfd":"e-second-walk-fails.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(!strcmp(d.file_data.reason,"map-conflict") &&
             d.file_data.valid_copies==2u &&
             d.file_data.agreeing_copies==0u && !d.file_data.presented);
      arc_jfd_free(&d);
   }
   d=load(argv[1],"e-many-190.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"presented") && d.file_data.presented==1u &&
          d.file_data.valid_copies==2u && d.file_data.agreeing_copies==2u);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"d-objects-8193.jfd":"d-objects-8192.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(!strcmp(d.file_data.reason,i?"limit":"presented") &&
             d.file_data.presented==(i?0u:1u));
      arc_jfd_free(&d);
   }
   d=load(argv[1],"e-object-conflict.jfd");
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   assert(!strcmp(d.file_data.reason,"map-conflict") &&
          d.file_data.valid_copies==2u &&
          d.file_data.agreeing_copies==0u && !d.file_data.presented);
   arc_jfd_free(&d);
   for (unsigned i=0u;i<4u;++i)
   {
      static const char *one_copy[] = {"e-one-copy.jfd",
         "e-backup-only.jfd", "e-zone-check-backup.jfd",
         "e-zone-check-primary.jfd"};
      d=load(argv[1],one_copy[i]);
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(d.file_data.mask[1]==4u && d.file_data.valid_copies==1u &&
             d.file_data.agreeing_copies==1u);
      arc_jfd_free(&d);
   }
   for (unsigned i=0u;i<2u;++i)
   {
      d=load(argv[1],i?"../launch-family/single-l-extended.jfd":
                       "../jfd-identification/sparse-f-map-primary.jfd");
      arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
      assert(!strcmp(d.file_data.reason,"format") && !d.file_data.presented);
      arc_jfd_free(&d);
   }
   d=load(argv[1],"../jfd-identification/sparse-d-timed.jfd");
   arc_jfd_prepare_identification(&d,true);
   assert(d.identification_mask & (1u<<3u));
   arc_jfd_file_data_prepare(&d,true,false,"wp-flag");
   ArcJfdView id_view;
   assert(arc_jfd_effective_sector(&d,0u,
          d.tracks[0u].sector_count+3u,false,&id_view) &&
          id_view.id_visible && !id_view.data);
   uint32_t id_time=id_view.sector.time_us;
   arc_jfd_file_data_prepare(&d,true,true,"wp-flag");
   assert(arc_jfd_effective_sector(&d,0u,
          d.tracks[0u].sector_count+3u,false,&id_view) &&
          id_view.id_visible && !id_view.data &&
          id_view.sector.time_us==id_time);
   arc_jfd_free(&d);
   char manifest_path[4096],name[256];
   int n=snprintf(manifest_path,sizeof(manifest_path),
                  "%s/jfd-file-data/all-proof-fixtures.txt",argv[1]);
   assert(n>0 && (size_t)n<sizeof(manifest_path));
   FILE *manifest=fopen(manifest_path,"r"); assert(manifest);
   unsigned strict_count=0u;
   while (fscanf(manifest,"%255s",name)==1)
   {
      d=load(argv[1],name);
      arc_jfd_file_data_prepare(&d,true,true,"wp-flag");
      assert(!strcmp(d.file_data.reason,"option-strict") &&
             !d.file_data.presented && !d.file_data.valid_copies);
      for (unsigned side=0u;side<ARC_JFD_PDD5_TRACKS;++side)
         assert(!d.file_data.mask[side]);
      arc_jfd_free(&d);
      ++strict_count;
   }
   assert(strict_count>=35u && !ferror(manifest) && !fclose(manifest));
   puts("jfd file-data: pass");
   return 0;
}
