/* Private-leg driver: production inventory/locator, no firmware paths/bytes
 * in output. Never installed or shipped. GPL-2.0-only. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "rpc_inventory.h"
#include "arc_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "../tools/rpc_runner_vfs.h"
static double milliseconds(void)
{
#if defined(_WIN32)
   LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
   return 1000.0*(double)now.QuadPart/(double)freq.QuadPart;
#else
   struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return -1;
   return 1000.0*(double)now.tv_sec+(double)now.tv_nsec/1000000.0;
#endif
}
int main(int argc,char **argv)
{
   unsigned iterations=5;
   if(argc<2||argc>3)return 2;
   if(argc==3){char *end;unsigned long n=strtoul(argv[2],&end,10);if(!*argv[2]||*end||!n||n>101)return 2;iterations=(unsigned)n;}
   arc_io_set_vfs(&vfs);size_t count;
   const ArcRpcFirmwareFingerprint *table=arc_riscpc_firmware_catalogue(&count);
   for(unsigned i=0;i<iterations;++i){
      double begin=milliseconds();arc_io_measure_begin();
      ArcRpcFirmwareInventory inventory=arc_riscpc_inventory_scan(argv[1],table,count);
      double scanned=milliseconds();bool ok=inventory.installed==30&&!inventory.bounded&&!inventory.vfs_failure&&!inventory.resource_failure;
      double family_ms[5]={0};bool family_ok[5]={0};
      for(unsigned os=ARC_RPC_OS_350;os<=ARC_RPC_OS_371;++os){
         void *data=NULL;size_t size=0;
         double read_begin=milliseconds();
         bool verified=arc_riscpc_firmware_read(&inventory,(ArcRpcSelectionOs)os,&data,&size);
         free(data);family_ms[os]=milliseconds()-read_begin;family_ok[os]=verified;
         if(!verified)ok=false;
      }
      ArcIoMeasurement cost=arc_io_measure_end();double end=milliseconds();vfs_assert_no_open_handles();
      printf("sample=%u result=%s scan_ms=%.3f locator_ms=%.3f total_ms=%.3f operations=%llu bytes=%llu\n",
         i,ok?"verified":"refused",scanned-begin,end-scanned,end-begin,
         (unsigned long long)cost.operations,(unsigned long long)cost.bytes);
      for(unsigned os=ARC_RPC_OS_350;os<=ARC_RPC_OS_371;++os)
         printf("sample=%u family=%s result=%s locator_ms=%.3f scan_plus_selected_ms=%.3f\n",
            i,arc_riscpc_selection_os_name((ArcRpcSelectionOs)os),family_ok[os]?"verified":"refused",
            family_ms[os],scanned-begin+family_ms[os]);
      if(!ok)return 1;
   }return 0;
}
