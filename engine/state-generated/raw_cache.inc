/* ArcDroid logical raw-track cache deltas; GPL-2.0-only. */
#include "arc_state.h"
static void arc_state_raw_cache(ArcState *s,unsigned drive)
{
   if(!arc_state_pass(s)||arc_state_error(s))return;
   ArcIoFile *file;int sides,sectors,size,track;int64_t effective;
   arc_state_project(s,&file,&adf[drive].f,sizeof(file));
   arc_state_project(s,&sides,&adf[drive].dblside,sizeof(sides));
   arc_state_project(s,&sectors,&adf[drive].sectors,sizeof(sectors));
   arc_state_project(s,&size,&adf[drive].size,sizeof(size));
   arc_state_project(s,&track,&adf[drive].track,sizeof(track));
   arc_state_project(s,&effective,&adf[drive].effective_size,sizeof(effective));
   uint8_t *base=arc_state_scratch(s,sizeof(adf[0].track_data));if(!base)return;
   memset(base,0,sizeof(adf[0].track_data));
   char name[64];snprintf(name,sizeof(name),"adf.file.%u.view",drive);size_t bytes=0;
   const uint8_t *view=arc_state_view(s,name,NULL,0,&bytes);
   if(file) {
      int64_t side_bytes=(int64_t)sectors*size;
      if(sides<0||sides>1||side_bytes<=0||side_bytes>(int64_t)sizeof(adf[0].track_data[0])) {arc_state_fail(s,"raw cache geometry");return;}
      int64_t offset=(int64_t)track*side_bytes*(sides?2:1);
      if(track>=0&&offset>=0&&offset<effective&&view) {
         int64_t remain=effective-offset;
         for(unsigned side=0;side<=(unsigned)sides;++side) {
            size_t amount=(size_t)(remain<side_bytes?remain:side_bytes);
            if((uint64_t)offset>bytes||amount>bytes-(size_t)offset){arc_state_fail(s,"raw cache extent");return;}
            memcpy(base+side*sizeof(adf[0].track_data[0]),view+(size_t)offset,amount);offset+=(int64_t)amount;remain-=(int64_t)amount;
         }
      }
   }
   snprintf(name,sizeof(name),"adf.cache.%u",drive);
   const void *next=arc_state_delta(s,name,file?(const void *)adf[drive].track_data:base,base,sizeof(adf[0].track_data));
   if(arc_state_loading(s)&&next)arc_state_stage(s,adf[drive].track_data,next,sizeof(adf[0].track_data));
}
