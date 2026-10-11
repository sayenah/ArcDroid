/* ArcDroid C fault boundaries. GPL-2.0-only. */
#ifndef ARC_FAULT_JUMP_H
#define ARC_FAULT_JUMP_H
/* MinGW-w64 setjmp.h's __USE_MINGW_SETJMP_NON_SEH form. Keep the CRT
 * jmp_buf/register ABI and longjmp, but save no SEH unwind target. These C
 * boundaries own explicit cleanup and have no language unwind handlers.
 * The header re-evaluates this switch outside its declaration include guard,
 * so an earlier or later setjmp.h inclusion preserves the selected form.
 * See installed setjmp.h:216,250-254,262 and D-155. */
#if defined(_WIN32) && defined(__MINGW32__) && defined(__x86_64__)
#define __USE_MINGW_SETJMP_NON_SEH 1
#endif
#include <setjmp.h>
#endif
