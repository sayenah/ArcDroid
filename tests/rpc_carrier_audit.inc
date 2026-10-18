/* Authored descriptor/payload bounds, without guest execution. GPL-2.0-only. */
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
static void audit_put(uint8_t *p,unsigned value)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));}
static unsigned audit_get24(const uint8_t *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16;}
static size_t audit_l_sector(unsigned s)
{unsigned cylinder=(s/16)%80,side=s/(16*80);return (2*cylinder+side)*4096+(s%16)*256;}
static void arc_carrier_audit(void)
{
 uint8_t *disc=malloc(ARC_RPC_HELPER_SIZE);assert(disc);
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);size_t page=info.dwPageSize;
 size_t capacity=(ARC_RPC_HELPER_MAX+page-1)/page*page;
 uint8_t *memory=VirtualAlloc(NULL,capacity+page,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD previous;
 assert(memory && VirtualProtect(memory+capacity,page,PAGE_NOACCESS,&previous));
#else
 size_t page=(size_t)sysconf(_SC_PAGESIZE),capacity=(ARC_RPC_HELPER_MAX+page-1)/page*page;
 uint8_t *memory=mmap(NULL,capacity+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(memory!=MAP_FAILED && !mprotect(memory+capacity,page,PROT_NONE));
#endif
 const unsigned fields[]={1,64,0,1,1,64,1,1,1,68,0,0,0,0};
 for(unsigned n=68;n<=ARC_RPC_HELPER_MAX;n+=4){
  uint8_t *image=memory+capacity-n;
  for(unsigned i=0;i<n;++i)image[i]=(uint8_t)(i*17+7);
  memcpy(image,"ARCRHLP1",8);for(unsigned i=0;i<14;++i)audit_put(image+8+4*i,fields[i]);audit_put(image+16,n);
  assert(arc_riscpc_helper_carrier(disc,ARC_RPC_HELPER_SIZE,image,n));
  unsigned end=7+(n+255)/256;
  assert(audit_get24(disc)==end && audit_get24(disc+256)==2560-end);
  assert(audit_get24(disc+audit_l_sector(2)+5+18)==n);
  for(unsigned i=0;i<n;++i)assert(disc[audit_l_sector(7+i/256)+i%256]==image[i]);
 }
 free(disc);
#ifdef _WIN32
 assert(VirtualFree(memory,0,MEM_RELEASE));
#else
 assert(!munmap(memory,capacity+page));
#endif
}
