/* Captured-only FileCore proof for R1' omitted-sector presentation.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "jfd.h"
#include "arcdroid_core.h"
#include "jfd_file_data_limits.h"

#include <stdlib.h>
#include <string.h>

#define DISC_BYTES 819200u
typedef struct { uint32_t first, length, id; bool free; } Fragment;
typedef struct { uint32_t address, length, first_step, steps; uint8_t type, shared; } Object;
typedef struct {
   const ArcJfdImage *image;
   bool new_map;
   uint8_t map[1024];
   unsigned map_copy, declared, id_bits, bit_shift, spare;
   uint32_t root;
   Fragment fragments[512];
   unsigned fragment_count;
   uint32_t free_first[82], free_end[82];
   unsigned free_count;
   Object objects[ARC_JFD_FILE_DATA_MAX_OBJECTS];
   unsigned object_count, dir_count, step_count, resolution_work, incomplete;
   uint32_t steps[ARC_JFD_FILE_DATA_MAX_STEPS];
   uint8_t *coverage;
   const char *failure;
} Proof;

static uint32_t le24(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u); }
static uint32_t le32(const uint8_t *p)
{ return le24(p) | ((uint32_t)p[3] << 24u); }
static uint32_t ror13(uint32_t v) { return (v >> 13u) | (v << 19u); }
static unsigned bits(const uint8_t *p, unsigned at, unsigned count)
{
   unsigned v = 0u;
   for (unsigned i = 0u; i < count; ++i)
      v |= (unsigned)((p[(at+i)>>3u] >> ((at+i)&7u)) & 1u) << i;
   return v;
}
static uint8_t old_check(const uint8_t *p)
{
   unsigned sum = 255u;
   for (unsigned i = 255u; i-- > 0u;)
   { if (sum > 255u) sum = (sum + 1u) & 255u; sum += p[i]; }
   return (uint8_t)sum;
}
static uint8_t zone_check(const uint8_t *p)
{
   unsigned s[4] = {0};
   for (size_t at = 1020u; at > 0u; at -= 4u)
   {
      s[0] += p[at] + (s[3] >> 8u); s[3] &= 255u;
      s[1] += p[at+1u] + (s[0] >> 8u); s[0] &= 255u;
      s[2] += p[at+2u] + (s[1] >> 8u); s[1] &= 255u;
      s[3] += p[at+3u] + (s[2] >> 8u); s[2] &= 255u;
   }
   s[0] += s[3] >> 8u;
   s[1] += p[1] + (s[0] >> 8u);
   s[2] += p[2] + (s[1] >> 8u);
   s[3] += p[3] + (s[2] >> 8u);
   return (uint8_t)((s[0]^s[1]^s[2]^s[3]) & 255u);
}

/* Do not consult the effective view: a valid captured descriptor alone owns
 * these bytes, regardless of saved replacements or virtual sectors. */
static bool base_sector(const ArcJfdImage *im, unsigned logical, uint8_t out[1024])
{
   unsigned side = logical / 5u, id = logical % 5u;
   if (side >= im->track_count || im->tracks[side].quarantine_reason) return false;
   const ArcJfdTrack *track = &im->tracks[side];
   const ArcJfdSector *found = NULL;
   size_t found_index = 0u;
   for (unsigned i = 0u; i < track->sector_count; ++i)
   {
      size_t index=(size_t)track->first_sector+i;
      const ArcJfdSector *s = &im->sectors[index];
      if (s->id != id || s->density != 2u || s->size != 1024u) continue;
      if (found) return false;
      found = s;
      found_index=index;
   }
   if (!found || found->data_offset == UINT32_MAX || found->flags ||
       !arc_jfd_sector_write_eligible(im,found_index) ||
       found->data_offset > im->decoded_size ||
       1024u > im->decoded_size - found->data_offset) return false;
   memcpy(out, im->decoded + found->data_offset, 1024u);
   return true;
}
static bool base_read(const ArcJfdImage *im, uint32_t at, uint8_t *out, size_t n)
{
   uint8_t sector[1024];
   if (at > DISC_BYTES || n > DISC_BYTES - at) return false;
   while (n)
   {
      unsigned within = at & 1023u;
      size_t amount = 1024u - within;
      if (amount > n) amount = n;
      if (!base_sector(im, at / 1024u, sector)) return false;
      memcpy(out, sector + within, amount);
      out += amount; at += (uint32_t)amount; n -= amount;
   }
   return true;
}
static bool fail(Proof *p, const char *why)
{ p->failure = why; return false; }

static bool parse_old(Proof *p)
{
   if (!base_read(p->image, 0u, p->map, 512u) ||
       p->map[255] != old_check(p->map) ||
       p->map[511] != old_check(p->map+256u) ||
       p->map[509] > 3u || p->map[510] > 246u || p->map[510] % 3u)
      return false;
   unsigned declared = le24(p->map+252u);
   if (declared < 12u) return false;
   p->declared = declared > 3200u ? 3200u : declared;
   p->new_map = false; p->root = 1024u;
   unsigned previous = 0u;
   for (unsigned at = 0u; at < p->map[510]; at += 3u)
   {
      unsigned first = le24(p->map+at), count = le24(p->map+256u+at);
      if (!count || first < previous || first > declared || count > declared-first)
         return false;
      previous = first + count;
      p->free_first[p->free_count] = first * 256u;
      p->free_end[p->free_count++] = previous * 256u;
   }
   return true;
}
static bool parse_new(Proof *p, unsigned copy)
{
   p->fragment_count=0u;
   p->free_count=0u;
   if (!base_read(p->image, copy*1024u, p->map, 1024u)) return false;
   const uint8_t *r = p->map+4u;
   if (p->map[0] != zone_check(p->map) || p->map[3] != 255u ||
       r[0] != 10u || r[1] != 5u || r[2] != 2u || r[3] != 2u ||
       r[7] > 3u || r[8] || r[9] != 1u || le32(r+16u) != DISC_BYTES)
      return false;
   for (unsigned i=36u;i<60u;++i) if (r[i]) return false;
   p->id_bits = r[4]; p->bit_shift = r[5];
   p->spare = (unsigned)r[10] | ((unsigned)r[11]<<8u);
   if (!arc_filecore_e_map_layout_valid(p->id_bits,p->bit_shift,p->spare,DISC_BYTES))
      return false;
   p->root = le32(r+12u) & 0xffffffu;
   if ((le32(r+12u) & 0xe0000000u) || ((p->root>>8u)&0x7fffu) < 2u)
      return false;
   p->new_map = true; p->declared = 3200u; p->map_copy = copy;
   unsigned start = 512u, end = 512u + 8192u-p->spare-480u;
   unsigned link = bits(p->map,8u,p->id_bits) & 0x7fffu;
   unsigned free_at = link ? 8u+link : 0u;
   if (free_at && (free_at < start || free_at >= end)) return false;
   while (start < end)
   {
      if (start+p->id_bits >= end || (free_at && free_at < start) ||
          p->fragment_count == 512u) return false;
      unsigned id = bits(p->map,start,p->id_bits);
      unsigned stop = start+p->id_bits;
      while (stop < end && !bits(p->map,stop,1u)) ++stop;
      if (stop == end) return false;
      unsigned offset = (start-512u)<<p->bit_shift;
      unsigned length = (stop+1u-start)<<p->bit_shift;
      if (offset > DISC_BYTES || length > DISC_BYTES-offset) return false;
      bool is_free = start == free_at;
      p->fragments[p->fragment_count++] = (Fragment){offset,length,id,is_free};
      if (is_free)
      {
         free_at = id ? start+(id&0x7fffu) : 0u;
         if (free_at && free_at <= stop) return false;
      }
      start = stop+1u;
   }
   return !free_at;
}

/* Resolve an object in logical byte order. A sharing offset skips 1 KiB
 * units of that fragment before the object's first byte. */
static bool resolve(Proof *p, uint32_t address, uint32_t logical,
                    uint32_t *physical, uint32_t *available)
{
   if (!p->new_map)
   {
      uint64_t at = (uint64_t)address*256u + logical;
      if (at >= (uint64_t)p->declared*256u || at >= DISC_BYTES)
         return fail(p,"object-invalid");
      *physical = (uint32_t)at;
      *available = (uint32_t)((uint64_t)p->declared*256u-at);
      return true;
   }
   if (address & 0xe0000000u) return fail(p,"object-invalid");
   unsigned id = (address>>8u)&0x7fffu, shared=address&255u;
   if (id < 2u) return fail(p,"object-invalid");
   uint64_t wanted = (uint64_t)(shared ? shared-1u : 0u)*1024u + logical;
   for (unsigned i=0u;i<p->fragment_count;++i)
   {
      if (!arc_jfd_file_data_limits_ok(0u,0u,0u,++p->resolution_work))
         return fail(p,"limit");
      const Fragment *f=&p->fragments[i];
      if (f->free || f->id != id) continue;
      if (wanted < f->length)
      {
         *physical = f->first+(uint32_t)wanted;
         *available = f->length-(uint32_t)wanted;
         return true;
      }
      wanted -= f->length;
   }
   return fail(p,"object-invalid");
}

static bool claim(Proof *p, uint32_t address, uint32_t length, bool directory,
                  bool root, uint32_t *object_index)
{
   if (!arc_jfd_file_data_limits_ok(0u,0u,p->object_count+1u,0u))
      return fail(p,"limit");
   if (length > DISC_BYTES) return fail(p,"object-invalid");
   if (directory && length != 2048u) return fail(p,"object-invalid");
   for (unsigned i=0u;i<p->object_count;++i)
      if (length && p->objects[i].length &&
          p->objects[i].address == address)
         return fail(p,"object-invalid");
   Object *o=&p->objects[p->object_count];
   *o=(Object){address,length,p->step_count,0u,directory?1u:0u,
               p->new_map ? (uint8_t)(address&255u) : 0u};
   *object_index=p->object_count++;
   if (!length) return true;
   for (uint32_t offset=0u;offset<length;)
   {
      uint32_t physical, available;
      if (!resolve(p,address,offset,&physical,&available)) return false;
      uint32_t amount=1024u-(offset&1023u);
      if (amount>length-offset) amount=length-offset;
      if (amount>available) amount=available;
      if (!amount) return fail(p,"object-invalid");
      if (physical>DISC_BYTES || amount>DISC_BYTES-physical ||
          physical < (p->new_map?2048u:512u)) return fail(p,"object-invalid");
      for (unsigned i=0u;i<p->free_count;++i)
         if (physical<p->free_end[i] && p->free_first[i]<physical+amount)
            return fail(p,"object-invalid");
      /* A 256-byte ownership unit can contain two non-overlapping files.
       * The byte mask below is authoritative for collisions. */
      for (uint32_t at=physical;at<physical+amount;++at)
      {
         if (p->coverage[at]) return fail(p,"object-invalid");
         p->coverage[at]=1u;
      }
      if (!arc_jfd_file_data_limits_ok(0u,0u,0u,p->step_count+1u))
         return fail(p,"limit");
      p->steps[p->step_count++]=physical;
      ++o->steps;
      offset+=amount;
   }
   (void)root;
   return true;
}

static bool read_object(Proof *p, uint32_t address, uint8_t out[2048])
{
   for (unsigned at=0u;at<2048u;)
   {
      uint32_t physical, available;
      if (!resolve(p,address,at,&physical,&available)) return false;
      uint32_t amount=1024u-(at&1023u);
      if (amount>2048u-at) amount=2048u-at;
      if (amount>available) amount=available;
      if (!amount || !base_read(p->image,physical,out+at,amount)) return false;
      at+=amount;
   }
   return true;
}
static bool directory_valid(const uint8_t d[2048], unsigned *entries)
{
   /* D and E both use the 2048-byte FileCore new-directory check byte.
    * Unlike L's old directory, no zero-check compatibility exception applies.
    * FileCore PRM, "Calculating DirCheckByte". */
   if (d[0]!=d[2042u] ||
       (memcmp(d+1u,"Nick",4u) && memcmp(d+1u,"Hugo",4u)) ||
       memcmp(d+1u,d+2043u,4u) || d[2007] || d[2008] || d[2009]) return false;
   unsigned count=0u;
   for (;count<77u;++count)
      if (!d[5u+count*26u]) break;
   uint32_t check=0u, used=5u+count*26u;
   unsigned at=0u;
   for (;at<(used&~3u);at+=4u) check=le32(d+at)^ror13(check);
   for (;at<used;++at) check=d[at]^ror13(check);
   for (at=2008u;at<2044u;at+=4u) check=le32(d+at)^ror13(check);
   if (d[2047]!=(uint8_t)(check^(check>>8u)^(check>>16u)^(check>>24u)))
      return false;
   *entries=count;
   return true;
}
static bool walk(Proof *p, uint32_t address, unsigned depth, bool root)
{
   if (!arc_jfd_file_data_limits_ok(depth,p->dir_count+1u,0u,0u))
      return fail(p,"limit");
   ++p->dir_count;
   uint8_t d[2048];
   if (root && !p->new_map)
   {
      if (!base_read(p->image,1024u,d,2048u)) return fail(p,"root-invalid");
   }
   else if (!read_object(p,address,d))
   {
      if (root) return fail(p,"root-invalid");
      ++p->incomplete;
      p->failure=NULL;
      return true;
   }
   unsigned count;
   if (!directory_valid(d,&count))
   {
      if (root) return fail(p,"root-invalid");
      ++p->incomplete;
      return true;
   }
   for (unsigned i=0u;i<count;++i)
   {
      const uint8_t *e=d+5u+i*26u;
      if (e[0]<0x20u) return fail(p,"object-invalid");
      uint32_t length=le32(e+18u), child=le24(e+22u);
      bool directory=(e[25u]&8u)!=0u;
      uint32_t index;
      if (directory && length!=2048u) return fail(p,"object-invalid");
      if (!claim(p,child,length,directory,false,&index)) return false;
      if (directory && !walk(p,child,depth+1u,false)) return false;
   }
   return true;
}
static bool prove(Proof *p)
{
   p->coverage=calloc(DISC_BYTES,1u);
   if (!p->coverage) return fail(p,"limit");
   uint32_t root_index;
   if (!p->new_map)
   {
      /* The old-map root is an absolute disc address, unlike entries. */
      if (p->declared*256u<3072u ||
          p->free_count && p->free_first[0]<3072u)
         return fail(p,"root-invalid");
      if (!claim(p,4u,2048u,true,true,&root_index))
         return fail(p,"root-invalid");
      return walk(p,4u,1u,true);
   }
   if (!claim(p,p->root,2048u,true,true,&root_index))
      return fail(p,"root-invalid");
   return walk(p,p->root,1u,true);
}

static bool same_interpretation(const Proof *a,const Proof *b)
{
   if (a->new_map!=b->new_map || a->declared!=b->declared ||
       a->root!=b->root || a->object_count!=b->object_count ||
       a->step_count!=b->step_count) return false;
   for (unsigned i=0u;i<a->object_count;++i)
   {
      const Object *x=&a->objects[i];
      const Object *y=NULL;
      for (unsigned j=0u;j<b->object_count;++j)
         if (b->objects[j].address==x->address) { y=&b->objects[j]; break; }
      if (!y || x->length!=y->length || x->type!=y->type ||
          x->shared!=y->shared || x->steps!=y->steps ||
          memcmp(a->steps+x->first_step,b->steps+y->first_step,
                 x->steps*sizeof(uint32_t))) return false;
   }
   return true;
}

static bool regular_side(const ArcJfdImage *im,unsigned side,uint8_t *captured)
{
   const ArcJfdTrack *t=&im->tracks[side];
   if (t->quarantine_reason) return false;
   *captured=0u;
   for (unsigned i=0u;i<t->sector_count;++i)
   {
      size_t index=(size_t)t->first_sector+i;
      const ArcJfdSector *s=&im->sectors[index];
      if (s->id>=5u || s->size!=1024u || s->density!=2u || s->flags ||
          s->data_offset==UINT32_MAX || (*captured&(1u<<s->id)) ||
          !arc_jfd_sector_write_eligible(im,index)) return false;
      for (unsigned j=0u;j<i;++j)
         if (im->sectors[t->first_sector+j].time_us==s->time_us) return false;
      *captured|=(uint8_t)(1u<<s->id);
   }
   return true;
}
static char detect_shape(const ArcJfdImage *image)
{
   uint8_t sector[1024];
   if (!base_sector(image,0u,sector)) return '-';
   if (sector[4u]==10u && sector[5u]==5u &&
       sector[6u]==2u && sector[7u]==2u) return 'E';
   if (le24(sector+252u)>=12u && sector[509u]<=3u &&
       sector[510u]<=246u) return 'D';
   return '-';
}
void arc_jfd_file_data_prepare(ArcJfdImage *image,bool native,bool strict,
                               const char *image_class)
{
   if (!image) return;
   ArcJfdFileData *plan=&image->file_data;
   memset(plan,0,sizeof(*plan));
   plan->format=native ? detect_shape(image) : '-';
   plan->reason="none-eligible";
   if (!native) return;
   if (strict) { plan->reason="option-strict"; return; }
   if (image_class && !strcmp(image_class,"writable"))
   { plan->reason="writable-c11c"; return; }
   if (image_class && !strcmp(image_class,"damaged-store"))
   { plan->reason="damaged-store"; return; }
   Proof *first=calloc(1u,sizeof(*first)), *second=calloc(1u,sizeof(*second));
   if (!first || !second) { plan->reason="limit"; goto done; }
   first->image=second->image=image;
   char shape=plan->format;
   bool old=parse_old(first);
   bool newer=false;
   if (old) { plan->format='D'; plan->valid_copies=1u; }
   else
   {
      newer=parse_new(first,0u);
      if (!newer) newer=parse_new(first,1u);
      if (!newer)
      {
         plan->format=shape;
         plan->reason=shape=='-' ? "format" : "no-valid-map";
         goto done;
      }
      plan->format='E'; plan->valid_copies=1u;
      if (parse_new(second,first->map_copy^1u)) ++plan->valid_copies;
   }
   if (plan->valid_copies==2u &&
       (first->id_bits!=second->id_bits ||
        first->bit_shift!=second->bit_shift ||
        first->spare!=second->spare || first->root!=second->root))
   { plan->reason="map-conflict"; goto done; }
   if (!prove(first))
   {
      const char *failure=first->failure;
      if (plan->valid_copies==2u && prove(second)) failure="map-conflict";
      plan->reason=failure;
      goto done;
   }
   plan->incomplete_dirs=first->incomplete;
   plan->agreeing_copies=1u;
   if (plan->valid_copies==2u)
   {
      if (first->root!=second->root || !prove(second))
      { plan->reason="map-conflict"; goto done; }
      if (!same_interpretation(first,second))
      { plan->reason="map-conflict"; goto done; }
      plan->agreeing_copies=2u;
   }
   for (unsigned t=0u;t<ARC_JFD_PDD5_TRACKS && t<image->track_count;++t)
   {
      uint8_t captured=0u;
      if (!regular_side(image,t,&captured))
      { ++plan->skipped_sides; continue; }
      uint8_t eligible=0u;
      for (unsigned id=0u;id<5u;++id)
      {
         unsigned sector=t*5u+id;
         uint32_t at=sector*1024u;
         if ((captured&(1u<<id)) || at+1024u>first->declared*256u ||
             at+1024u>DISC_BYTES || at<(old?512u:2048u)) continue;
         for (unsigned byte=at;byte<at+1024u;++byte)
            if (first->coverage[byte]) { eligible|=(uint8_t)(1u<<id); break; }
      }
      if (!eligible) continue;
      uint32_t times[ARC_JFD_MAX_STANDARD_IDS]={0};
      if (!arc_jfd_place_slots(image,t,5u,ARC_JFD_PDD5_SPACING_US,
                               captured,times))
      { ++plan->skipped_sides; plan->reason="unplaceable"; continue; }
      plan->mask[t]=eligible;
      for (unsigned id=0u;id<5u;++id)
         if (eligible&(1u<<id))
         { plan->time[t][id]=times[id]; ++plan->presented; }
   }
   if (plan->presented) plan->reason="presented";
done:
   if (first) { free(first->coverage); free(first); }
   if (second) { free(second->coverage); free(second); }
   if (!plan->presented) memset(plan->mask,0,sizeof(plan->mask));
}
