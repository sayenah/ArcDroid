/* Authored save-state harness interchange, not a core format. GPL-2.0-only. */
static void ss1_write_u64(FILE *f,uint64_t value)
{ uint8_t b[8];for(unsigned i=0;i<8u;++i)b[i]=(uint8_t)(value>>(8u*i));assert(fwrite(b,1,8,f)==8); }
static uint64_t ss1_read_u64(FILE *f)
{ uint8_t b[8];assert(fread(b,1,8,f)==8);uint64_t value=0;for(unsigned i=0;i<8u;++i)value|=(uint64_t)b[i]<<(8u*i);return value; }
static void ss1_write_state(const char *path,const uint8_t *data,size_t bytes)
{
   FILE *f=fopen(path,"wb");assert(f);ss1_write_u64(f,bytes);
   size_t words=bytes/4u,at=0;const uint32_t *p=(const uint32_t*)data;
   while(at<words) {
      size_t end=at+1u;while(end<words&&p[end]==p[at]&&end-at<UINT32_MAX)++end;
      if(end-at>=4u) {ss1_write_u64(f,((uint64_t)(end-at)<<1u)|1u);assert(fwrite(&p[at],4,1,f)==1);at=end;}
      else {
         end=at;
         while(end<words&&end-at<1048576u) {
            if(end+3u<words&&p[end]==p[end+1]&&p[end]==p[end+2]&&p[end]==p[end+3])break;
            ++end;
         }
         ss1_write_u64(f,(uint64_t)(end-at)<<1u);assert(fwrite(p+at,4,end-at,f)==end-at);at=end;
      }
   }
   assert(fwrite(data+words*4u,1,bytes%4u,f)==bytes%4u);assert(!fclose(f));
}
static uint8_t *ss1_read_state(const char *path,size_t *bytes)
{
   FILE *f=fopen(path,"rb");assert(f);uint64_t n=ss1_read_u64(f);assert(n&&n<=1024u*1024u*1024u);
   *bytes=(size_t)n;uint8_t *data=malloc(*bytes);assert(data);uint32_t *p=(uint32_t*)data;size_t words=*bytes/4u,at=0;
   while(at<words) {
      uint64_t record=ss1_read_u64(f),count=record>>1u;assert(count&&count<=words-at);
      if(record&1u){uint32_t value;assert(fread(&value,4,1,f)==1);for(size_t i=0;i<count;++i)p[at+i]=value;}
      else assert(fread(p+at,4,(size_t)count,f)==count);
      at+=(size_t)count;
   }
   assert(fread(data+words*4u,1,*bytes%4u,f)==*bytes%4u);assert(fgetc(f)==EOF);assert(!fclose(f));return data;
}
static void ss1_set_input(unsigned frame)
{
   keyboard_levels[RETROK_a]=(frame%5u)<3u;
   keyboard_levels[RETROK_LSHIFT]=(frame%9u)<2u;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A]=(frame%7u)==4u;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3]=frame==5u||frame==104u||(frame==90u&&!strcmp(option_machine,"A3010"));
   mouse_x=(int)(frame%5u)-2;mouse_y=(int)(frame%3u)-1;mouse_buttons=frame%6u<2u?1u:0u;
}
static uint64_t ss1_elapsed_ns(struct timespec start)
{ struct timespec end;assert(!clock_gettime(CLOCK_MONOTONIC,&end));return (uint64_t)(end.tv_sec-start.tv_sec)*1000000000u+(uint64_t)(end.tv_nsec-start.tv_nsec); }
static void ss1_copy_tree(const char *source,const char *destination)
{
   make_directory(destination);DIR *directory=opendir(source);assert(directory);
   struct dirent *entry;
   while((entry=readdir(directory))!=NULL) {
      if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
      char from[4096],to[4096];assert(snprintf(from,sizeof(from),"%s/%s",source,entry->d_name)<(int)sizeof(from));
      assert(snprintf(to,sizeof(to),"%s/%s",destination,entry->d_name)<(int)sizeof(to));
      struct stat metadata;assert(!lstat(from,&metadata));
      if(S_ISDIR(metadata.st_mode))ss1_copy_tree(from,to);
      else {assert(S_ISREG(metadata.st_mode));copy_file(from,to);}
   }
   assert(!closedir(directory));
}
/* Independent wire inspection for refusal cases: repair CRC after mutation so
 * fingerprint/schema checks, rather than accidental checksum errors, reject. */
static uint64_t ss1_get64(const uint8_t *p)
{ uint64_t v=0;for(unsigned i=0;i<8u;++i)v|=(uint64_t)p[i]<<(8u*i);return v; }
static void ss1_put64(uint8_t *p,uint64_t v)
{ for(unsigned i=0;i<8u;++i)p[i]=(uint8_t)(v>>(8u*i)); }
static uint64_t ss1_key(const char *name)
{ uint64_t h=UINT64_C(14695981039346656037);while(*name)h=(h^(uint8_t)*name++)*UINT64_C(1099511628211);return h; }
static void ss1_checksum(uint8_t *p)
{
   uint32_t crc=UINT32_MAX;size_t end=(size_t)ss1_get64(p+16);
   for(size_t i=40;i<end;++i){crc^=p[i];for(unsigned b=0;b<8u;++b)crc=(crc>>1u)^(UINT32_C(0xedb88320)&(uint32_t)-(int32_t)(crc&1u));}
   ss1_put64(p+32,(uint64_t)~crc);
}
static size_t ss1_record(uint8_t *p,const char *name,unsigned kind)
{
   for(size_t at=40;at<(size_t)ss1_get64(p+16);at+=24u+(size_t)ss1_get64(p+at+16))
      if(name?ss1_get64(p+at)==ss1_key(name):(unsigned)ss1_get64(p+at+8)==kind)return at;
   return 0;
}

static int16_t ss1_last_input[2][7][2][RETROK_LAST];
static int16_t ss1_logged_input(unsigned p,unsigned d,unsigned i,unsigned id)
{
   int16_t value=input(p,d,i,id);
   if(p<2u&&d<7u&&i<2u&&id<RETROK_LAST)ss1_last_input[p][d][i][id]=value;
   return value;
}
static void ss1_fake_poll(void) {}
static int16_t ss1_fake_input(unsigned p,unsigned d,unsigned i,unsigned id)
{ return p<2u&&d<7u&&i<2u&&id<RETROK_LAST?ss1_last_input[p][d][i][id]:0; }
static int16_t ss1_preempt_input(unsigned p,unsigned d,unsigned i,unsigned id)
{ return input(p,d,i,id); }
static size_t ss1_compressed_record(uint8_t *p)
{
   for(size_t at=40;at<(size_t)ss1_get64(p+16);at+=24u+(size_t)ss1_get64(p+at+16))
      if(ss1_get64(p+at+8)>>32u)return at;
   return 0;
}

typedef struct {
   void *file;
   int64_t (*seek)(void *,int64_t,int);
   size_t (*write)(void *,const void *,size_t);
   bool (*flush)(void *);
} Ss1WritePath;
static uint64_t ss1_write_batch(const Ss1WritePath *path)
{
   uint8_t sector[512];struct timespec start;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
   for(unsigned i=0;i<256u;++i){
      memset(sector,(int)(i^0x5au),sizeof(sector));
      assert(path->seek(path->file,(int64_t)(100u*1024u+(i%16u)*512u),RETRO_VFS_SEEK_POSITION_START)>=0);
      assert(path->write(path->file,sector,sizeof(sector))==sizeof(sector)&&path->flush(path->file));
   }
   return ss1_elapsed_ns(start)/256u;
}
static int ss1_ns_order(const void *a,const void *b)
{uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);}

/* Independently authored size/accounting model of RetroArch v1.22.2
 * state_manager.c: raw_compress/find_same and push_do. No upstream code is
 * linked or copied. Two-byte reverse patches stop at an equal 32-bit pair;
 * odd-address handling follows NO_UNALIGNED_MEM on the arm64 host.
 * Input has state_manager_raw_alloc's zero/sentinel padding. */
static size_t ss1_rewind_patch_size(const uint16_t *a,const uint16_t *b,size_t bytes)
{
   size_t words=(bytes+1u)/2u,at=0,out=6u;
   while(at<words) {
      size_t start=at;while(at<words&&a[at]==b[at])++at;
      if(at==words)break;
      size_t skip=at-start;
      if(skip>65535u){out+=6u;continue;}
      size_t end=at;
#if !defined(__x86_64__) && !defined(__i386__)
      if(end&1u)++end;
      if(a[end]!=b[end]) {
#endif
      while(a[end]!=b[end]||a[end+1u]!=b[end+1u])end+=2u;
      if(end>at&&a[end-1u]==b[end-1u])--end;
#if !defined(__x86_64__) && !defined(__i386__)
      }
#endif
      size_t count=end-at;if(count>65535u)count=65535u;
      assert(count&&count<=words-at);out+=4u+count*2u;at+=count;
   }
   return out;
}

static void ss1_rewind_model_check(void)
{
   uint16_t a[32]={0},b[32]={0};assert(ss1_rewind_patch_size(a,b,16u)==6u);
   a[1]=1u;assert(ss1_rewind_patch_size(a,b,16u)==12u);a[3]=2u;
#if !defined(__x86_64__) && !defined(__i386__)
   assert(ss1_rewind_patch_size(a,b,16u)==18u); /* Two odd, one-word edits. */
#else
   assert(ss1_rewind_patch_size(a,b,16u)==16u); /* Unaligned pair scanning. */
#endif
   memset(a,0,sizeof(a));a[0]=1u;a[2]=2u;
   assert(ss1_rewind_patch_size(a,b,16u)==16u);
   for(unsigned i=0;i<8u;++i)a[i]=(uint16_t)(i+1u);
   assert(ss1_rewind_patch_size(a,b,16u)==26u);
   uint16_t *long_a=calloc(65548u,sizeof(*long_a)),*long_b=calloc(65548u,sizeof(*long_b));
   assert(long_a&&long_b);long_a[65536u]=1u;
   assert(ss1_rewind_patch_size(long_a,long_b,131080u)==18u);
   free(long_a);free(long_b);
}
