/* Windows-only executable crash/continue-search oracle. GPL-2.0-only. */
#include <windows.h>
#include <assert.h>
#include <string.h>
static unsigned passed;
static LONG CALLBACK next(EXCEPTION_POINTERS *e)
{
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION||
       e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
        ++passed;return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* A real fault in a named function makes the emitted RVA independently
 * mappable with this unstripped executable's DWARF. */
__attribute__((noinline)) void arc_test_crash_fault(volatile unsigned *address)
{*address=1;}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"handled")){
        PVOID last=AddVectoredExceptionHandler(0,next);assert(last);
        ULONG_PTR access[2]={1,0xdeadcafe};
        RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,access);
        RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION,0,0,NULL);
        assert(passed==2);RemoveVectoredExceptionHandler(last);return 0;
    }
    assert(!strcmp(argv[1],"crash"));
    arc_test_crash_fault(NULL);return 1;
}
