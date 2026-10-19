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

static void ss1_fake_poll(void) { poll(); }
static int16_t ss1_fake_input(unsigned p,unsigned d,unsigned i,unsigned id)
{ return input(p,d,i,id); }
static size_t ss1_compressed_record(uint8_t *p)
{
   for(size_t at=40;at<(size_t)ss1_get64(p+16);at+=24u+(size_t)ss1_get64(p+at+16))
      if(ss1_get64(p+at+8)>>32u)return at;
   return 0;
}
