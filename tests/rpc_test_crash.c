/* Windows-only RPC executable startup; GPL-2.0-only.
 * CMake links this into every RPC test/runner, never a shipping target.
 * Install before main, including its first allocation-failure longjmp. */
#include "rpc_crash_locator.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void __attribute__((constructor)) arc_test_crash_startup(void)
{
    SetErrorMode(GetErrorMode()|SEM_NOGPFAULTERRORBOX);
    if(!arc_test_crash_locator_start_stderr()||atexit(arc_test_crash_locator_stop)){
        fputs("RPC test crash locator initialization failed\n",stderr);
        exit(EXIT_FAILURE);
    }
}
