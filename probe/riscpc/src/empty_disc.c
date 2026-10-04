/* ArcDroid empty-drive controller backend for the initial firmware gate.
 * GPL-2.0-only. Real JFD media must replace this module for AC-6. */
#include "probe.h"
#include "disc.h"
#include "fdc.h"
static unsigned selected;
static int tracks[2];
static unsigned poll_count;
static int seeking;
disc_funcs *drive_funcs[2];
void disc_set_drivesel(int d) {if(d>=0&&d<2)selected=(unsigned)d;}
int disc_get_current_track(int d) {return d>=0&&d<2?tracks[d]:0;}
void disc_seek(int d,int t) {if(d>=0&&d<2)tracks[d]=t;seeking=0;}
void disc_readsector(int d,int r,int c,int h,int den) {(void)d;(void)r;(void)c;(void)h;(void)den;seeking=1;poll_count=0;}
void disc_writesector(int d,int r,int c,int h,int den) {(void)d;(void)r;(void)c;(void)h;(void)den;fdc_writeprotect();}
void disc_readaddress(int d,int c,int h,int den) {disc_readsector(d,0,c,h,den);}
void disc_format(int d,int c,int h,int den) {(void)d;(void)c;(void)h;(void)den;fdc_writeprotect();}
void disc_stop(int d) {(void)d;seeking=0;}
void disc_poll(void) {
 (void)selected;
 if(seeking&&++poll_count>=12500) {seeking=0;fdc_notfound();} /* two 200ms index periods */
}
