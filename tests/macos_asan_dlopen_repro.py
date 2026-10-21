#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Standalone Apple ASan image-reload diagnostic; contains no ArcDroid code.

Run: python3 tests/macos_asan_dlopen_repro.py /tmp/macos-asan-repro
Keeps sources, commands and logs. Not a CTest: the failing child intentionally
exercises a toolchain defect. A fixed runtime may no longer reproduce it.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

IMAGE = r'''/* SPDX-License-Identifier: GPL-2.0-only */
#include <stddef.h>
static unsigned char one[33554432], two[16777219], three[65539];
unsigned char *address(unsigned which)
{ return which==0 ? one : which==1 ? two : three; }
'''

MAIN = r'''/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sanitizer/asan_interface.h>
/* ASan's error formatter can itself fault traversing unloaded global metadata.
 * Keep the original failing call chain visible in that case; never resume. */
static void crash(int signal_number)
{
    void *frames[40];
    int count=backtrace(frames,40);
    backtrace_symbols_fd(frames,count,STDERR_FILENO);
    _exit(128+signal_number);
}
int main(int argc,char **argv)
{
    assert(argc==4);setbuf(stdout,NULL);
    signal(SIGSEGV,crash);signal(SIGBUS,crash);
    void *primary=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(primary);
    int retain=!strcmp(argv[3],"retain");
    for(unsigned iteration=0;iteration<200;++iteration){
        void *heap[8];
        for(unsigned j=0;j<8;++j){
            size_t bytes=4227136+j*16384;
            heap[j]=malloc(bytes);assert(heap[j]);memset(heap[j],0x5a,bytes);
        }
        for(unsigned j=0;j<8;++j)free(heap[j]);
        printf("loading copy %u\n",iteration);
        void *copy=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL|(retain?RTLD_NODELETE:0));
        assert(copy);
        unsigned char *(*address)(unsigned)=dlsym(copy,"address");assert(address);
        unsigned char *data=address(0),*redzone=data+33554432;
        printf("data=%p poisoned=%d redzone=%d\n",(void *)data,
            __asan_address_is_poisoned(data),__asan_address_is_poisoned(redzone));
        assert(!dlclose(copy));
        printf("after dlclose: redzone=%d\n",__asan_address_is_poisoned(redzone));
    }
    assert(!dlclose(primary));puts("completed 200 copies");return 0;
}
'''


def main():
    if sys.platform != 'darwin' or len(sys.argv) != 2:
        raise SystemExit('Run on macOS with an output-directory argument')
    root = Path(sys.argv[1]).resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root/'image.c').write_text(IMAGE)
    (root/'main.c').write_text(MAIN)
    cc = os.environ.get('CC', 'clang')
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-UNDEBUG']
    commands = [
        [cc, *flags, '-dynamiclib', str(root/'image.c'), '-o', str(root/'image.dylib')],
        [cc, *flags, str(root/'main.c'), '-o', str(root/'loader')],
    ]
    for command in commands:
        subprocess.run(command, check=True)
    shutil.copy2(root/'image.dylib', root/'copy.dylib')
    # Only ODR names are intentionally duplicated. No memory-check suppression.
    # Signal handling is delegated to the diagnostic backtrace above.
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:detect_odr_violation=0:'
               'halt_on_error=1:handle_segv=0:handle_sigbus=0',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    results = []
    for mode in ['reload', 'retain']:
        command = [str(root/'loader'), str(root/'image.dylib'), str(root/'copy.dylib'), mode]
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout+result.stderr
        (root/(mode+'.log')).write_text(output)
        results.append(dict(mode=mode, command=command, returncode=result.returncode))
        if mode == 'reload':
            reproduced = (result.returncode != 0 and 'getsectiondata' in output
                          and '__asan_register_image_globals' in output
                          and 'after dlclose: redzone=1' in output)
        elif result.returncode != 0 or 'completed 200 copies' not in output:
            raise RuntimeError('Retained-image control failed: '+output)
    (root/'commands.json').write_text(json.dumps(dict(build=commands, runs=results, env={
        key: env[key] for key in ['ASAN_OPTIONS', 'UBSAN_OPTIONS']}), indent=2)+'\n')
    if not reproduced:
        raise RuntimeError('Image-registration defect not reproduced; inspect '+str(root/'reload.log'))
    print('Reproduced stale global shadow / getsectiondata failure; retained control passed 200 copies')


if __name__ == '__main__':
    main()
