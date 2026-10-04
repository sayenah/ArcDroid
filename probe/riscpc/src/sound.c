/* ArcDroid serial sound DMA/resampling trial. GPL-2.0-only.
 * DMA semantics follow RPCEmu 0.9.5 sound.c (GPL-2.0-or-later,
 * copyright 2005-2010 Sarah Walker); no thread or host queue feedback. */
#include "probe.h"
#include "sound.h"
#include "iomd.h"
uint32_t soundaddr[4],probe_sound_rate;
int soundinited,soundlatch,soundcount;
uint64_t probe_sound_due,probe_audio_hash,probe_underruns,probe_overruns;
unsigned probe_audio_count;
int16_t probe_audio[16384*2];
static int16_t dma[1024*2];
static unsigned dma_count,dma_at;
static uint64_t next_source,next_output,source_rem,output_rem;
static int16_t held_l,held_r;
static int was_enabled;
void probe_sound_reset(void) {
 memset(soundaddr,0,sizeof(soundaddr));soundinited=0;probe_sound_rate=41666;
 probe_sound_due=UINT64_MAX;next_source=next_output=source_rem=output_rem=0;
 dma_count=dma_at=0;held_l=held_r=0;was_enabled=0;probe_audio_count=0;
 probe_audio_hash=UINT64_C(1469598103934665603);probe_underruns=probe_overruns=0;
}
void sound_init(void) {probe_sound_reset();}
void sound_restart(void) {}
void sound_pause(void) {}
void sound_buffer_update(void) {}
void sound_samplefreq_change(int rate) {
 if(rate<=0||rate>192000)fatal("Invalid sound frequency");
 probe_sound_rate=(uint32_t)rate;source_rem=0;
}
static uint64_t advance_rate(uint64_t *rem,unsigned rate) {
 *rem+=1000000000;uint64_t delta=*rem/rate;*rem%=rate;return delta;
}
void sound_irq_update(void) {
 unsigned offset=(iomd.sndstat&IOMD_DMA_STATUS_BUFFER)<<1;
 uint32_t page=soundaddr[offset]&0xfffff000, start=soundaddr[offset]&0xff0,end=(soundaddr[offset+1]&0xff0)+16;
 if(end<start)fatal("Inverted sound DMA range");
 dma_count=(end-start)/4;dma_at=0;
 if(!dma_count||dma_count>1024)fatal("Sound DMA bound");
 for(unsigned i=0;i<dma_count;i++) {
  uint32_t a=page+start+i*4,v=mem_phys_read32(a);
  dma[i*2]=(int16_t)v;dma[i*2+1]=(int16_t)(v>>16);
 }
 iomd.irqdma.status|=IOMD_IRQDMA_SOUND_0;
 iomd.sndstat|=IOMD_DMA_STATUS_INTERRUPT|IOMD_DMA_STATUS_OVERRUN;
 iomd.sndstat^=IOMD_DMA_STATUS_BUFFER;updateirqs();
}
void probe_sound_step(void) {
 int enabled=probe_sound_enabled();
 if(enabled&&!was_enabled) {next_source=probe_now;sound_irq_update();}
 if(!enabled) {dma_at=dma_count=0;held_l=held_r=0;}
 was_enabled=enabled;
 if(enabled&&probe_now>=next_source) {
  if(dma_at==dma_count)sound_irq_update();
  held_l=dma[dma_at*2];held_r=dma[dma_at*2+1];dma_at++;
  next_source+=advance_rate(&source_rem,probe_sound_rate);
 }
 /* Fixed-rate causal zero-order resampling: held source sample at each output timestamp. */
 while(next_output<=probe_now) {
  if(probe_audio_count>=16384) {probe_overruns++;fatal("Output audio bound");}
  probe_audio[probe_audio_count*2]=held_l;probe_audio[probe_audio_count*2+1]=held_r;probe_audio_count++;
  next_output+=advance_rate(&output_rem,48000);
 }
 probe_sound_due=enabled&&next_source<next_output?next_source:next_output;
}
void probe_sound_flush(uint64_t target) {(void)target;probe_sound_step();}
