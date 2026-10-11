/* Test-only ownership oracles after every public store call, including failed
 * open/read/reconstruction/population/import and close. GPL-2.0-only.
 * Only an empty inactive ROTATE writer may survive a successful call. Real process kills intentionally
 * never return; the OS owns their handle cleanup. */
#include "rpc_hd_store.h"
static inline ArcRpcHdStore *checked_hd_open(const char *save,const char *id,bool blank,char *error,size_t size)
{ArcRpcHdStore *s=arc_riscpc_hd_open(save,id,blank,error,size);rpc_hd_test_no_open_handles();return s;}
static inline void checked_hd_close(ArcRpcHdStore *s)
{arc_riscpc_hd_close(s);rpc_hd_test_no_open_handles();}
static inline void checked_hd_handles(ArcRpcHdStore *s)
{
 if(!s){rpc_hd_test_no_open_handles();return;}
 ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);
 if(!a.read_only&&a.state==ARC_RPC_HD_ROTATE&&a.phase_step==1)rpc_hd_test_rotation_writer();
 else rpc_hd_test_no_open_handles();
}
static inline void checked_hd_tick(ArcRpcHdStore *s)
{
 uint64_t before=rpc_hd_test_metadata_calls(),namespace_before=rpc_hd_test_namespace_calls();arc_riscpc_hd_tick(s);
 ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);
 assert(rpc_hd_test_metadata_calls()-before<=4);
 assert(a.tick_metadata_operations==rpc_hd_test_metadata_calls()-before);
 assert(rpc_hd_test_namespace_calls()-namespace_before<=1);
 assert(a.tick_namespace_operations==rpc_hd_test_namespace_calls()-namespace_before);
 checked_hd_handles(s);
}
#define CHECKED_HD_TRANSFER(name,type) \
static inline bool checked_hd_##name(ArcRpcHdStore *s,uint32_t lba,uint32_t count,type p) \
{bool ok=arc_riscpc_hd_##name(s,lba,count,p);checked_hd_handles(s);return ok;}
CHECKED_HD_TRANSFER(read,uint8_t *)
CHECKED_HD_TRANSFER(write,const uint8_t *)
CHECKED_HD_TRANSFER(population_read,uint8_t *)
CHECKED_HD_TRANSFER(population_write,const uint8_t *)
#undef CHECKED_HD_TRANSFER
#define CHECKED_HD_POPULATION(name) \
static inline bool checked_hd_population_##name(ArcRpcHdStore *s) \
{bool ok=arc_riscpc_hd_population_##name(s);checked_hd_handles(s);return ok;}
CHECKED_HD_POPULATION(begin)
CHECKED_HD_POPULATION(seal)
CHECKED_HD_POPULATION(publish)
#undef CHECKED_HD_POPULATION
static inline void checked_hd_population_abort(ArcRpcHdStore *s)
{arc_riscpc_hd_population_abort(s);checked_hd_handles(s);}
static inline bool checked_hd_import(ArcRpcHdStore *s,const char *source)
{bool ok=arc_riscpc_hd_import(s,source);checked_hd_handles(s);return ok;}
#define arc_riscpc_hd_open checked_hd_open
#define arc_riscpc_hd_close checked_hd_close
#define arc_riscpc_hd_tick checked_hd_tick
#define arc_riscpc_hd_read checked_hd_read
#define arc_riscpc_hd_write checked_hd_write
#define arc_riscpc_hd_population_read checked_hd_population_read
#define arc_riscpc_hd_population_write checked_hd_population_write
#define arc_riscpc_hd_population_begin checked_hd_population_begin
#define arc_riscpc_hd_population_seal checked_hd_population_seal
#define arc_riscpc_hd_population_publish checked_hd_population_publish
#define arc_riscpc_hd_population_abort checked_hd_population_abort
#define arc_riscpc_hd_import checked_hd_import
