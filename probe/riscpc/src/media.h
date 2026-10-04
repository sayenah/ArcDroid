/* ArcDroid disposable media session. GPL-2.0-only. */
#ifndef PROBE_MEDIA_H
#define PROBE_MEDIA_H
#include "jfd.h"
#include <stdio.h>
typedef struct {unsigned cylinder,head,size_code;bool deleted,fm;} ProbeDiscRequest;
void probe_disc_request(ProbeDiscRequest request);
unsigned probe_disc_notfound_status(void);
extern unsigned probe_disc_period_us;
extern int probe_disc_changed[2];
extern uint64_t probe_media_reads,probe_media_writes,probe_media_refusals;
void probe_media_open(const char *list,FILE *custody);
void probe_media_close(void);
void probe_media_reset(void);
unsigned probe_media_count(void);unsigned probe_media_index(void);
bool probe_media_ejected(void);bool probe_media_eject(bool);
bool probe_media_select(unsigned);
void probe_media_swap(unsigned);
void probe_media_test_attach(ArcJfdImage *image);
extern int probe_helper_enabled;
void probe_helper_init(void);void probe_helper_stop(void);void probe_helper_poll(int);
void probe_helper_request(int,unsigned,unsigned,unsigned,int,ProbeDiscRequest);
#endif
