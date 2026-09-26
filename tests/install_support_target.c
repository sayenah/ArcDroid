/* Resolve and isolate the deterministic install-carrier doorbell. GPL-2.0-only. */
#include "adffs.h"
#include "arc_install_doorbell.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *read_file(const char *path, size_t *size)
{
   FILE *f=fopen(path,"rb"); assert(f && !fseek(f,0,SEEK_END));
   long n=ftell(f); assert(n>0 && !fseek(f,0,SEEK_SET));
   uint8_t *p=malloc((size_t)n); assert(p);
   assert(fread(p,1u,(size_t)n,f)==(size_t)n && !fclose(f));
   *size=(size_t)n; return p;
}
static unsigned le24(const uint8_t *p)
{ return p[0] | ((unsigned)p[1]<<8u) | ((unsigned)p[2]<<16u); }
static unsigned le32(const uint8_t *p)
{ return le24(p) | ((unsigned)p[3]<<24u); }
static const uint8_t *find_entry(const uint8_t *root, const char *name)
{
   for (unsigned i=0u;i<77u;++i) {
      const uint8_t *p=root+5u+i*26u;
      if (!p[0]) break;
      size_t n=strlen(name);
      if (!memcmp(p,name,n) && (n==10u || p[n]==13u)) return p;
   }
   return NULL;
}
static void check_carrier(const uint8_t *image, const ArcInstallPlan *plan,
                          const ArcInstallDoorbell *target)
{
   size_t track_base=((size_t)target->track*2u+target->side)*5120u;
   assert(target->track<80u && target->side<2u && target->sector==0u);
   assert(track_base>=3072u && track_base+5120u<=ARC_ADFFS_DISC_SIZE);
   const uint8_t *root=image+1024u;
   const uint8_t *door=find_entry(root,"ArcDoor");
   const uint8_t *ready=find_entry(root,"ArcReady");
   const uint8_t *run=find_entry(root,"Run");
   assert(door && ready && run &&
          (size_t)le24(door+22u)*256u==track_base &&
          le32(door+18u)==26u &&
          !memcmp(image+track_base,"ArcDroid install doorbell\r",26u));
   uint8_t marker[96]; size_t marker_size=0u;
   assert(arc_install_marker(plan,marker,sizeof(marker),&marker_size) &&
          le32(ready+18u)==marker_size &&
          !memcmp(image+(size_t)le24(ready+22u)*256u,marker,marker_size));
   for (unsigned sector=1u;sector<5u;++sector)
      for (unsigned byte=0u;byte<1024u;++byte)
         assert(image[track_base+sector*1024u+byte]==0u);
   /* Directory and every pre-doorbell file read avoid ArcDoor's track-side. */
   assert(1024u+2048u<=track_base);
   for (unsigned i=0u;i<77u;++i) {
      const uint8_t *p=root+5u+i*26u;
      if (!p[0]) break;
      size_t start=(size_t)le24(p+22u)*256u, length=le32(p+18u);
      assert(start<=ARC_ADFFS_DISC_SIZE &&
             length<=ARC_ADFFS_DISC_SIZE-start);
      if (p!=door)
         assert(start+length<=track_base || start>=track_base+5120u);
   }
   for (unsigned d=0u;d<plan->variant->disc_count;++d) {
      char name[11];
      assert(snprintf(name,sizeof(name),"D%u",
             plan->variant->discs[d].number)>0 && find_entry(root,name));
   }
}
int main(int argc, char **argv)
{
   assert(argc==4);
   size_t sizes[ARC_INSTALL_MAX_DISCS]={0};
   uint8_t *first=read_file(argv[1],&sizes[0]);
   uint8_t *second=read_file(argv[2],&sizes[1]);
   const uint8_t *raw[ARC_INSTALL_MAX_DISCS]={first,second,NULL,NULL};
   char error[160]={0};
   const ArcInstallProfile *profile=arc_install_profile(123u,4u);
   assert(profile);
   ArcInstallPlan plan={0};
   plan.profile=profile; plan.variant=&profile->variants[0];
   strcpy(plan.storage_id,"fffbd5a69f9b43c4ea8cc69f1fe9234f393e3d45");
   uint8_t *image=malloc(ARC_ADFFS_DISC_SIZE); assert(image);
   ArcInstallDoorbell target={0};
   assert(arc_install_hd_carrier_build(&plan,raw,sizes,image,&target,
                                       error,sizeof(error)));
   check_carrier(image,&plan,&target);
   arc_install_doorbell_configure(&target);
   arc_install_doorbell_read(1u,target.track,target.side,
                             target.sector,1u);
   arc_install_doorbell_read(0u,target.track,target.side,
                             (target.sector+1u)%5u,1u);
   arc_install_doorbell_read(0u,target.track,1u-target.side,
                             target.sector,1u);
   arc_install_doorbell_read(0u,target.track,target.side,target.sector,0u);
   assert(!arc_install_doorbell_latched());
   arc_install_doorbell_read(0u,target.track,target.side,target.sector,1u);
   assert(arc_install_doorbell_latched());
   arc_install_doorbell_read(0u,target.track,target.side,target.sector,1u);
   assert(arc_install_doorbell_latched());
   arc_install_doorbell_configure(NULL);
   assert(!arc_install_doorbell_latched());
   FILE *out=fopen(argv[3],"wb"); assert(out);
   assert(fprintf(out,"%u %u %u\n",target.track,target.side,
                  target.sector)>0 && !fclose(out));
   static const struct { unsigned id, discs; const char *key; } cases[]={
      {10140u,2u,"etype"},{10609u,4u,"etype"},
      {10609u,4u,"xtra"},{10587u,1u,"premier"}
   };
   for (unsigned c=0u;c<sizeof(cases)/sizeof(cases[0]);++c) {
      const ArcInstallProfile *shipping=arc_install_profile(cases[c].id,
                                                            cases[c].discs);
      assert(shipping);
      const ArcInstallVariant *variant=NULL;
      for (unsigned v=0u;v<shipping->variant_count;++v)
         if (!strcmp(shipping->variants[v].key,cases[c].key))
            variant=&shipping->variants[v];
      assert(variant);
      plan.profile=shipping; plan.variant=variant;
      uint8_t *dummy=calloc(1u,ARC_ADFFS_DISC_SIZE); assert(dummy);
      for (unsigned d=0u;d<variant->disc_count;++d) {
         raw[d]=dummy;
         /* Largest aligned payload that still leaves a free FileCore block. */
         sizes[d]=c==3u ? 805888u : d==0u ? 1024u : 804864u;
      }
      assert(arc_install_hd_carrier_build(&plan,raw,sizes,image,&target,
                                          error,sizeof(error)));
      check_carrier(image,&plan,&target);
      ++sizes[variant->disc_count-1u];
      assert(!arc_install_hd_carrier_build(&plan,raw,sizes,image,&target,
                                           error,sizeof(error)) &&
             strstr(error,"does not fit"));
      free(dummy);
   }
   free(image); free(first); free(second);
   return 0;
}
