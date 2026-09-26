/* Resolve the deterministic synthetic support-disc doorbell. GPL-2.0-only. */
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
int main(int argc, char **argv)
{
   assert(argc==3);
   size_t size=0u;
   uint8_t *bytes=read_file(argv[1],&size);
   ArcAdffsPackage package={0}; char error[160]={0};
   assert(arc_adffs_package_parse(bytes,size,&package,error,sizeof(error)));
   uint8_t *script=NULL; size_t script_size=0u;
   assert(arc_adffs_builtin_script(&package,123u,1u,&script,&script_size,
                                   error,sizeof(error)));
   ArcAdffsSelection selected={0}; char missing[128]={0};
   assert(arc_adffs_select(&package,script,script_size,&selected,missing));
   const ArcInstallProfile *profile=arc_install_profile(123u,4u);
   assert(profile);
   ArcInstallPlan plan={0};
   plan.profile=profile; plan.variant=&profile->variants[0];
   strcpy(plan.storage_id,"fffbd5a69f9b43c4ea8cc69f1fe9234f393e3d45");
   uint8_t *image=malloc(ARC_ADFFS_DISC_SIZE); assert(image);
   ArcInstallDoorbell target={0};
   assert(arc_install_hd_support_build(&package,&selected,&plan,false,
                 image,&target,error,sizeof(error)));
   assert(target.track<80u && target.side<2u && target.sector<5u);
   size_t track_base=((size_t)target.track*2u+target.side)*5120u;
   assert(!memcmp(image+track_base,"ArcDroid install doorbell\r",26u));
   for (unsigned sector=1u;sector<5u;++sector)
      for (unsigned byte=0u;byte<1024u;++byte)
         assert(image[track_base+sector*1024u+byte]==0u);
   arc_install_doorbell_configure(&target);
   arc_install_doorbell_read(0u,target.track,target.side,target.sector,1u);
   arc_install_doorbell_read(1u,target.track,target.side,
                             (target.sector+1u)%5u,1u);
   arc_install_doorbell_read(1u,target.track,1u-target.side,
                             target.sector,1u);
   arc_install_doorbell_read(1u,target.track,target.side,target.sector,0u);
   assert(!arc_install_doorbell_latched());
   arc_install_doorbell_read(1u,target.track,target.side,target.sector,1u);
   assert(arc_install_doorbell_latched());
   arc_install_doorbell_read(1u,target.track,target.side,target.sector,1u);
   assert(arc_install_doorbell_latched());
   arc_install_doorbell_configure(NULL);
   assert(!arc_install_doorbell_latched());
   FILE *out=fopen(argv[2],"wb"); assert(out);
   assert(fprintf(out,"%u %u %u\n",target.track,target.side,
                  target.sector)>0 && !fclose(out));
   free(image); free(script);
   arc_adffs_package_free(&package); free(bytes);
   return 0;
}
