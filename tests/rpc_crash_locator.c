/* Test-only Windows x64 first-chance crash locator. GPL-2.0-only.
 * Registered only by the plan core and RPC tests. No context modification.
 * Sources: Microsoft AddVectoredExceptionHandler / RtlVirtualUnwind docs.
 * Uses fixed storage and an already-open file; no malloc/stdio/symbol loading
 * after a fault. The frontend's log callback may itself allocate internally. */
#include "rpc_crash_locator.h"
#include <windows.h>
#include <stdint.h>
static PVOID registration;
static HANDLE output=INVALID_HANDLE_VALUE;
/* Borrowed from the CRT: never close it. CTest/Python capture this pipe. */
static HANDLE diagnostic_stderr=INVALID_HANDLE_VALUE;
static retro_log_printf_t logger;
static volatile LONG recording;
static char line[2048];
static CONTEXT walk;
static unsigned used;
static void text(const char *p) {while(*p&&used+1<sizeof(line))line[used++]=*p++;}
static void hex(uint64_t n)
{static const char digits[]="0123456789abcdef";text("0x");for(int i=15;i>=0;--i){char b[2]={digits[(n>>(i*4))&15],0};text(b);}}
static void emit(void)
{
    text("\n");line[used]=0;DWORD written;
    /* Preserve location even if logging or unwinding faults subsequently. */
    if(output!=INVALID_HANDLE_VALUE){WriteFile(output,line,used,&written,NULL);FlushFileBuffers(output);}
    if(diagnostic_stderr!=INVALID_HANDLE_VALUE){WriteFile(diagnostic_stderr,line,used,&written,NULL);FlushFileBuffers(diagnostic_stderr);}
    if(logger)logger(RETRO_LOG_ERROR,"%s",line);
    used=0;
}
static void module(uint64_t pc)
{
    MEMORY_BASIC_INFORMATION memory;char path[1024];
    if(VirtualQuery((void *)(uintptr_t)pc,&memory,sizeof(memory))&&memory.Type==MEM_IMAGE&&
       GetModuleFileNameA((HMODULE)memory.AllocationBase,path,sizeof(path))){
        path[sizeof(path)-1]=0;text(path);text("+");hex(pc-(uintptr_t)memory.AllocationBase);
        text(" base=");hex((uintptr_t)memory.AllocationBase);
    }else{text("unknown absolute=");hex(pc);}
}
static bool fatal(DWORD code)
{
    return code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR||
        code==EXCEPTION_ILLEGAL_INSTRUCTION||code==EXCEPTION_PRIV_INSTRUCTION||
        code==EXCEPTION_INT_DIVIDE_BY_ZERO||code==EXCEPTION_INT_OVERFLOW||code==EXCEPTION_STACK_OVERFLOW||
        code==EXCEPTION_DATATYPE_MISALIGNMENT||code==EXCEPTION_ARRAY_BOUNDS_EXCEEDED||
        code==EXCEPTION_FLT_DIVIDE_BY_ZERO||code==EXCEPTION_FLT_INVALID_OPERATION||
        code==EXCEPTION_FLT_OVERFLOW||code==EXCEPTION_FLT_UNDERFLOW||
        code==EXCEPTION_FLT_DENORMAL_OPERAND||code==EXCEPTION_FLT_INEXACT_RESULT||
        code==EXCEPTION_FLT_STACK_CHECK||
        code==0xc0000409u||code==0xc0000374u;
}
static LONG CALLBACK locate(EXCEPTION_POINTERS *e)
{
    if(!fatal(e->ExceptionRecord->ExceptionCode)||InterlockedCompareExchange(&recording,1,0))
        return EXCEPTION_CONTINUE_SEARCH;
    used=0;text("RPC first-chance exception code=");hex(e->ExceptionRecord->ExceptionCode);
    text(" thread=");hex(GetCurrentThreadId());
    if((e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION||e->ExceptionRecord->ExceptionCode==EXCEPTION_IN_PAGE_ERROR)&&e->ExceptionRecord->NumberParameters>=2){
        text(" access=");hex(e->ExceptionRecord->ExceptionInformation[0]);
        text(" address=");hex(e->ExceptionRecord->ExceptionInformation[1]);}
    text(" fault=");module((uintptr_t)e->ExceptionRecord->ExceptionAddress);emit();
#if defined(_WIN64) && defined(__x86_64__)
    walk=*e->ContextRecord;
    const NT_TIB *tib=(const NT_TIB *)NtCurrentTeb();
    uint64_t low=(uintptr_t)tib->StackLimit,high=(uintptr_t)tib->StackBase;
    /* Stack overflow is logged above without adding unwind pressure. */
    for(unsigned i=0;i<30&&walk.Rip&&e->ExceptionRecord->ExceptionCode!=EXCEPTION_STACK_OVERFLOW;++i){
        text("RPC stack frame=");hex(i);text(" pc=");module(walk.Rip);text(" sp=");hex(walk.Rsp);emit();
        if(walk.Rsp<low||walk.Rsp>high-sizeof(uint64_t))break;
        uint64_t previous=walk.Rsp,pc=walk.Rip,base=0;
        PRUNTIME_FUNCTION function=RtlLookupFunctionEntry(pc,&base,NULL);
        if(function){PVOID data;DWORD64 frame;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,pc,function,&walk,&data,&frame,NULL);
        }else{SIZE_T n;uint64_t caller;
            if(!ReadProcessMemory(GetCurrentProcess(),(void *)(uintptr_t)walk.Rsp,&caller,sizeof(caller),&n)||n!=sizeof(caller))break;
            walk.Rip=caller;walk.Rsp+=sizeof(caller);}
        if(walk.Rsp<=previous||walk.Rsp>high||walk.Rip==pc)break;
    }
#endif
    InterlockedExchange(&recording,0);
    return EXCEPTION_CONTINUE_SEARCH;
}
void arc_test_crash_locator_stop(void)
{
    if(registration){RemoveVectoredExceptionHandler(registration);registration=NULL;}
    if(output!=INVALID_HANDLE_VALUE){CloseHandle(output);output=INVALID_HANDLE_VALUE;}
    logger=NULL;
    diagnostic_stderr=INVALID_HANDLE_VALUE;
}
bool arc_test_crash_locator_start_stderr(void)
{
    arc_test_crash_locator_stop();
    HANDLE handle=GetStdHandle(STD_ERROR_HANDLE);
    if(!handle||handle==INVALID_HANDLE_VALUE)return false;
    diagnostic_stderr=handle;recording=0;
    registration=AddVectoredExceptionHandler(1,locate);
    if(!registration){arc_test_crash_locator_stop();return false;}
    return true;
}
bool arc_test_crash_locator_start(const char *save,retro_log_printf_t log)
{
    arc_test_crash_locator_stop();logger=log;
    if(!save)return false;
    wchar_t path[4096];int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,save,-1,path,4000);
    if(!n)return false;
    static const wchar_t suffix[]=L"/rpc-hd-exception.txt";
    for(unsigned i=0;i<sizeof(suffix)/sizeof(*suffix);++i)path[n-1+i]=suffix[i];
    output=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(output==INVALID_HANDLE_VALUE)return false;
    recording=0;registration=AddVectoredExceptionHandler(1,locate);
    if(!registration){arc_test_crash_locator_stop();return false;}
    if(logger)logger(RETRO_LOG_INFO,"RPC test crash locator active: rpc-hd-exception.txt\n");
    return true;
}
