/* Windows-only locator lifecycle/continue-search oracle. GPL-2.0-only. */
#include "rpc_crash_locator.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned passed,logged;
static void log_line(enum retro_log_level level,const char *format,...)
{(void)level;(void)format;++logged;}
static LONG CALLBACK next(EXCEPTION_POINTERS *e)
{if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION||e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){++passed;return EXCEPTION_CONTINUE_EXECUTION;}return EXCEPTION_CONTINUE_SEARCH;}
int main(int argc,char **argv)
{
    assert(argc==2);CreateDirectoryA(argv[1],NULL);
    PVOID last=AddVectoredExceptionHandler(0,next);assert(last);
    assert(arc_test_crash_locator_start(argv[1],log_line));
    RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION,0,0,NULL);
    ULONG_PTR access[2]={1,0xdeadcafe};
    RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,access);
    assert(passed==2&&logged>=4);arc_test_crash_locator_stop();
    char path[4096],bytes[65536];snprintf(path,sizeof(path),"%s/rpc-hd-exception.txt",argv[1]);
    FILE *file=fopen(path,"rb");assert(file);size_t n=fread(bytes,1,sizeof(bytes)-1,file);bytes[n]=0;assert(!ferror(file)&&!fclose(file));
    assert(strstr(bytes,"RPC first-chance exception code=0x00000000c000001d")&&strstr(bytes,"RPC stack frame=")&&strstr(bytes,".exe+0x"));
    assert(strstr(bytes,"code=0x00000000c0000005")&&strstr(bytes,"access=0x0000000000000001 address=0x00000000deadcafe"));
    unsigned before=logged;RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION,0,0,NULL);
    assert(passed==3&&logged==before);RemoveVectoredExceptionHandler(last);
    puts("Windows locator: flushed module/RVA stack, continue-search and unregister pass");return 0;
}
