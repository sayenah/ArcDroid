/* ArcDroid bounded scratch IDE, GPL-2.0-only. */
#ifndef PROBE_IDE_PROBE_H
#define PROBE_IDE_PROBE_H
#include <stdio.h>
#include <stdbool.h>
extern int probe_ide_enabled;
extern unsigned long long probe_ide_reads,probe_ide_writes,probe_ide_io_failures;
void probe_ide_open(const char *directory,const char *baseline,const char *sha1,FILE *custody);
bool probe_ide_close(void);
#endif
