/* Non-shipping libretro plan runner. GPL-2.0-only.
 * Measurement surrounds dispatcher/backend execution inside the test core;
 * frontend callbacks, hashes, output, sleep and input are outside that interval. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "rpc_session.h"
#include <inttypes.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static HMODULE library;
static void *symbol(const char *s) { return (void *)GetProcAddress(library, s); }
static void pause_ns(uint64_t n) { Sleep((DWORD)((n+999999)/1000000)); }
#else
#include <dlfcn.h>
#include <time.h>
static void *library;
static void *symbol(const char *s) { return dlsym(library, s); }
static void pause_ns(uint64_t n) { struct timespec t = {(time_t)(n/1000000000), (long)(n%1000000000)}; while (nanosleep(&t,&t) && errno == EINTR) {} }
#endif
uint64_t arc_test_clock_ns(void);
static const char *system_directory;
static bool fail_jfd_store,variable_updated;
static struct { uint64_t frame;const char *value;bool policy; } option_events[2];
static unsigned option_count;
static const char *disk_writes="shadow",*jfd_policy="as-imaged",*jfd_omitted="file-data";
static uint64_t pcm_hash = UINT64_C(14695981039346656037), raster_hash, pcm_frames;
static uint64_t pcm_prefix_hash = UINT64_C(14695981039346656037), first_raster_hash;
static unsigned raster_width, raster_height, video_calls;
static double fps = 60;
/* Predeclared frame captures belong to the frontend, after engine timing. */
#define DUMP_LIMIT 64u
#define DUMP_SIDE 2048u
static struct { uint64_t frame; const char *path; bool written; } dumps[DUMP_LIMIT];
static unsigned dump_count, dump_width, dump_height;
static uint64_t runner_frame;
static uint32_t *dump_pixels;
static bool dump_failed;
static void dump_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!dump_count) return;
    if (data) {
        if (!width || !height || width > DUMP_SIDE || height > DUMP_SIDE) {
            dump_failed = true;
            return;
        }
        for (unsigned y = 0; y < height; ++y)
            memcpy(dump_pixels + y * DUMP_SIDE, (const uint8_t *)data + y * pitch, width * sizeof(uint32_t));
        dump_width = width; dump_height = height;
    }
    for (unsigned i = 0; i < dump_count; ++i) {
        if (dumps[i].frame != runner_frame) continue;
        FILE *file = dump_width && dump_height ? fopen(dumps[i].path, "wb") : NULL;
        bool ok = file && fprintf(file, "P6\n%u %u\n255\n", dump_width, dump_height) > 0;
        uint8_t row[DUMP_SIDE * 3];
        for (unsigned y = 0; ok && y < dump_height; ++y) {
            for (unsigned x = 0; x < dump_width; ++x) {
                uint32_t pixel = dump_pixels[y * DUMP_SIDE + x];
                row[x * 3] = (uint8_t)(pixel >> 16);
                row[x * 3 + 1] = (uint8_t)(pixel >> 8);
                row[x * 3 + 2] = (uint8_t)pixel;
            }
            ok = fwrite(row, 3, dump_width, file) == dump_width;
        }
        if (file && fclose(file)) ok = false;
        dumps[i].written = ok;
        if (!ok) {
            fprintf(stderr, "Frame dump failed at frame %" PRIu64 "\n", runner_frame);
            dump_failed = true;
        }
    }
}

#define ARC_RUNNER_VFS_WRITE_REFUSE(path) (fail_jfd_store && (strstr(path,"/arcdroid/jfd/") || strstr(path,"\\arcdroid\\jfd\\")))
#include "rpc_runner_vfs.h"
static void hash_byte(uint64_t *hash, unsigned byte)
{ *hash = (*hash ^ (byte & 255)) * UINT64_C(1099511628211); }
static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
        struct retro_vfs_interface_info *request=data;
        if(request->required_interface_version>3)return false;
        request->iface=&vfs;return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = *save_directory ? save_directory : NULL; return *save_directory != 0;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = system_directory; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: *(uint64_t *)data = UINT64_C(1) << RETRO_DEVICE_KEYBOARD; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{
        struct retro_variable *v=data;
        v->value=!strcmp(v->key,"arcdroid_disk_writes")?disk_writes:
            !strcmp(v->key,"arcdroid_jfd_write_protect")?jfd_policy:
            !strcmp(v->key,"arcdroid_jfd_omitted")?jfd_omitted:NULL;
        return v->value!=NULL;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = variable_updated;variable_updated=false;return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: fps = ((struct retro_system_av_info *)data)->timing.fps; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        fprintf(stderr, "%s\n", ((struct retro_message_ext *)data)->msg); return true;
    default: return false;
    }
}
static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    dump_video(data, width, height, pitch);
    if (!data) return;
    raster_hash = UINT64_C(14695981039346656037);
    for (unsigned y = 0; y < height; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y*pitch);
        for (unsigned x = 0; x < width; ++x)
            for (unsigned b = 0; b < 4; ++b) hash_byte(&raster_hash, row[x] >> (b*8));
    }
    raster_width = width; raster_height = height; ++video_calls;
    if(video_calls==1)first_raster_hash=raster_hash;
}
static size_t audio(const int16_t *data, size_t frames)
{
    for (size_t i = 0; i < frames*2; ++i) {
        hash_byte(&pcm_hash, (uint16_t)data[i]); hash_byte(&pcm_hash, (uint16_t)data[i] >> 8);
        if(pcm_frames+i/2<480) {
            hash_byte(&pcm_prefix_hash,(uint16_t)data[i]);hash_byte(&pcm_prefix_hash,(uint16_t)data[i]>>8);
        }
    }
    pcm_frames += frames; return frames;
}
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }
static bool unsigned_number(const char *s, uint64_t max, uint64_t *out)
{
    uint64_t n = 0;
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || n > max/10 || (n == max/10 && (unsigned)(*s-'0') > max%10)) return false;
        n = n*10 + (unsigned)(*s-'0');
    }
    *out = n; return true;
}
static int compare(const void *a, const void *b)
{ uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x>y)-(x<y); }
static uint64_t quantile(const uint64_t *a, size_t n, unsigned numerator, unsigned denominator)
{ return a[(n*numerator + denominator-1)/denominator - 1]; }
#define LOAD(variable, name) do { void *p = symbol(name); if (!p) { fprintf(stderr,"Missing required symbol %s\n",name); return 2; } memcpy(&(variable), &p, sizeof(variable)); } while (0)
int main(int argc, char **argv)
{
#if defined(_WIN32)
    /* Canonical traces use the same LF bytes on every platform. */
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    const char *core = NULL, *plan = NULL, *pacing = "flat", *timing_path = NULL, *capture_path = NULL;
    uint64_t frames = 1, warmup = 0, measure = 0, ram_offset = 0, ram_count = 0, reset_at = UINT64_MAX;
    bool reject = false, print_cmos = false;
    uint64_t fail_alloc = 0, fault_at = UINT64_MAX;
    for (int i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (!strcmp(key,"--fail-jfd-store")) { fail_jfd_store=true;continue; }
        if (!strcmp(key,"--reject")) { reject = true; continue; }
        if (!strcmp(key,"--cmos")) { print_cmos = true; continue; }
        if (!strcmp(key,"--jfd-policy-at") || !strcmp(key,"--disk-writes-at")){
            if(i+2>=argc || option_count==2 || !unsigned_number(argv[i+1],999999,&option_events[option_count].frame))goto usage;
            bool policy=!strcmp(key,"--jfd-policy-at");const char *value=argv[i+2];
            if(policy ? (strcmp(value,"off")&&strcmp(value,"as-imaged")) :
                (strcmp(value,"off")&&strcmp(value,"shadow")&&strcmp(value,"original")))goto usage;
            option_events[option_count].value=value;option_events[option_count++].policy=policy;i+=2;continue;
        }
        if (!strcmp(key,"--dump-frame")) {
            if (i+2 >= argc || dump_count == DUMP_LIMIT ||
                !unsigned_number(argv[i+1],999999,&dumps[dump_count].frame) || !*argv[i+2]) goto usage;
            dumps[dump_count++].path = argv[i+2];
            i += 2;
            continue;
        }
        if (i+1 == argc) goto usage;
        const char *v = argv[++i];
        if (!strcmp(key,"--core")) core = v;
        else if (!strcmp(key,"--plan")) plan = v;
        else if (!strcmp(key,"--save")) { if(!canonical(v,save_directory) || !(vfs_stat(save_directory,NULL)&RETRO_VFS_STAT_IS_DIRECTORY)){fprintf(stderr,"--save must name an existing directory\n");return 2;} }
        else if (!strcmp(key,"--system")) system_directory = v;
        else if (!strcmp(key,"--disk-writes")) {if(strcmp(v,"off")&&strcmp(v,"shadow")&&strcmp(v,"original"))goto usage;disk_writes=v;}
        else if (!strcmp(key,"--jfd-policy")) {if(strcmp(v,"off")&&strcmp(v,"as-imaged"))goto usage;jfd_policy=v;}
        else if (!strcmp(key,"--jfd-omitted")) {if(strcmp(v,"strict")&&strcmp(v,"file-data"))goto usage;jfd_omitted=v;}
        else if (!strcmp(key,"--pacing")) pacing = v;
        else if (!strcmp(key,"--timing")) timing_path = v;
        else if (!strcmp(key,"--capture-seed")) capture_path = v;
        else if (!strcmp(key,"--frames")) { if (!unsigned_number(v,1000000,&frames) || !frames) goto usage; }
        else if (!strcmp(key,"--warmup-seconds")) { if (!unsigned_number(v,3600,&warmup)) goto usage; }
        else if (!strcmp(key,"--measure-seconds")) { if (!unsigned_number(v,3600,&measure) || !measure) goto usage; }
        else if (!strcmp(key,"--ram-offset")) { if (!unsigned_number(v,33554428,&ram_offset) || ram_offset%4) goto usage; }
        else if (!strcmp(key,"--ram-words")) { if (!unsigned_number(v,1024,&ram_count)) goto usage; }
        else if (!strcmp(key,"--reset-at")) { if (!unsigned_number(v,1000000,&reset_at)) goto usage; }
        else if (!strcmp(key,"--fault-at")) { if (!unsigned_number(v,1000000,&fault_at)) goto usage; }
        else if (!strcmp(key,"--fail-alloc")) { if (!unsigned_number(v,5,&fail_alloc) || !fail_alloc) goto usage; }
        else goto usage;
    }
    if (!core || !plan || (!measure && warmup) || (strcmp(pacing,"flat") && strcmp(pacing,"burst") && strcmp(pacing,"realtime"))) goto usage;
#if defined(_WIN32)
    library = LoadLibraryA(core);
#else
    library = dlopen(core, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!library) { fprintf(stderr,"Cannot load core\n"); return 2; }
    void (*set_environment)(retro_environment_t), (*set_video)(retro_video_refresh_t), (*set_audio)(retro_audio_sample_batch_t);
    void (*set_poll)(retro_input_poll_t), (*set_input)(retro_input_state_t), (*init)(void), (*deinit)(void), (*run)(void), (*unload)(void), (*reset)(void);
    bool (*load)(const struct retro_game_info *);
    void (*info)(struct retro_system_info *);
    LOAD(set_environment,"retro_set_environment"); LOAD(set_video,"retro_set_video_refresh"); LOAD(set_audio,"retro_set_audio_sample_batch");
    LOAD(set_poll,"retro_set_input_poll"); LOAD(set_input,"retro_set_input_state"); LOAD(init,"retro_init"); LOAD(deinit,"retro_deinit");
    LOAD(load,"retro_load_game"); LOAD(unload,"retro_unload_game"); LOAD(run,"retro_run"); LOAD(reset,"retro_reset"); LOAD(info,"retro_get_system_info");
    set_environment(environment); init(); set_video(video); set_audio(audio); set_poll(poll); set_input(input);
    struct retro_game_info game = {plan, NULL, 0, NULL};
    struct retro_system_info core_info; info(&core_info);
    if (reject) {
        bool loaded = load(&game);
        bool passed = !loaded && !symbol("arc_rpc_test_start") && !strstr(core_info.valid_extensions,"arcplan");
        unload(); deinit(); puts(passed ? "shipping-plan rejected" : "shipping-plan rejection FAILED"); return passed ? 0 : 1;
    }
    if (fail_alloc) {
        void (*inject)(unsigned); LOAD(inject,"arc_rpc_test_fail_alloc"); inject((unsigned)fail_alloc);
    }
    if (!load(&game)) { deinit(); return 1; }
    void (*snapshot)(ArcRpcTestSnapshot *), (*counts)(ArcRpcAudioCounts *), (*status)(ArcRpcVideoStatus *), (*capture)(ArcRpcCapture *);
    bool (*read_ram)(unsigned, uint32_t *); uint64_t (*frame_ns)(void);
    LOAD(snapshot,"arc_rpc_test_snapshot"); LOAD(counts,"arc_rpc_test_audio_counts"); LOAD(status,"arc_rpc_test_video_status");
    LOAD(capture,"arc_rpc_test_capture_status"); LOAD(read_ram,"arc_rpc_test_read_ram"); LOAD(frame_ns,"arc_engine_test_frame_ns");
    void (*cmos_snapshot)(uint8_t *), (*fault_next)(void);
    LOAD(cmos_snapshot,"arc_rpc_test_cmos_snapshot"); LOAD(fault_next,"arc_rpc_test_fault_next");
    unsigned (*plan_phase)(void)=NULL;
    void *phase_symbol=symbol("arc_test_plan_cmos_phase");if(phase_symbol)memcpy(&plan_phase,&phase_symbol,sizeof(plan_phase));
    if(plan_phase && plan_phase() && (print_cmos || capture_path)) {fprintf(stderr,"Persistent CMOS is never exported as bytes\n");unload();deinit();return 2;}
    if (dump_count) {
        dump_pixels = malloc(DUMP_SIDE * DUMP_SIDE * sizeof(uint32_t));
        if (!dump_pixels) { unload(); deinit(); return 2; }
    }
    FILE *timing = timing_path ? fopen(timing_path,"w") : NULL;
    if (timing_path && !timing) { free(dump_pixels); unload(); deinit(); return 2; }
    if (timing) fputs("frame,core_ns,T_ns,measured,guest_instructions,guest_ns\n", timing);
    uint64_t *times = calloc(1000000, sizeof(*times));
    if (!times) { free(dump_pixels); if(timing)fclose(timing); unload(); deinit(); return 2; }
    size_t sample_count = 0; unsigned result = 0; uint64_t period_ns = 0;
    uint64_t benchmark_start = arc_test_clock_ns(), benchmark_elapsed = 0;
    uint64_t prior_instructions=0,prior_guest_ns=0,measured_instructions=0,measured_guest_ns=0,measured_core_ns=0;
    for (uint64_t f = 0; f < (measure ? 1000000 : frames); ++f) {
        for(unsigned i=0;i<option_count;++i)if(f==option_events[i].frame){
            if(option_events[i].policy)jfd_policy=option_events[i].value;else disk_writes=option_events[i].value;
            variable_updated=true;
        }
        if (f == reset_at) reset();
        if (f == fault_at) fault_next();
        uint64_t frontend_start = arc_test_clock_ns();
        runner_frame = f;
        run();
        ArcRpcTestSnapshot s; ArcRpcAudioCounts a; ArcRpcVideoStatus v; ArcRpcCapture c;
        snapshot(&s); counts(&a); status(&v); capture(&c);
        if(plan_phase && plan_phase()==3){result=1;break;}
        uint64_t elapsed = frame_ns();
        period_ns = (uint64_t)llround(1000000000.0 / fps);
        /* Frontend wall-clock windows measure sustained device conditions.
         * Host time controls only when the frontend stops requesting frames,
         * never guest execution, clocks, input or per-frame engine duration. */
        bool measured = !measure || frontend_start-benchmark_start >= warmup*UINT64_C(1000000000);
        bool restarted=s.executed<prior_instructions || s.now_ns<prior_guest_ns;
        uint64_t guest_instructions=restarted?s.executed:s.executed-prior_instructions;
        uint64_t guest_ns=restarted?s.now_ns:s.now_ns-prior_guest_ns;
        prior_instructions=s.executed;prior_guest_ns=s.now_ns;
        if (measured) {
            times[sample_count++] = elapsed; measured_instructions+=guest_instructions;
            measured_guest_ns+=guest_ns; measured_core_ns+=elapsed;
        }
        if (timing) fprintf(timing,"%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%" PRIu64 ",%" PRIu64 "\n",f,elapsed,period_ns,measured,guest_instructions,guest_ns);
        printf("frame=%" PRIu64 " ns=%" PRIu64 " cpu=%" PRIu64 " idle=%" PRIu64 " rate=%u irq=%08x fiq=%08x fault=%u\n",
            f,s.now_ns,s.executed,s.idle_ns,s.rate_mips,s.irq,s.fiq,s.faulted);
        fputs("regs=",stdout); for(unsigned r=0;r<17;++r)printf("%s%08x",r?",":"",s.regs[r]);
        fputs(" events=",stdout);for(unsigned e=0;e<9;++e)printf("%s%" PRIu64,e?",":"",s.events[e]); puts("");
        printf("video=%ux%u hash=%016" PRIx64 " calls=%u fields=%" PRIu64 " lines=%" PRIu64 " pixels=%" PRIu64 " bytes=%" PRIu64 " wraps=%" PRIu64 " vidcur=%08x flyback=%" PRIu64 "/%" PRIu64 "\n",
            raster_width,raster_height,raster_hash,video_calls,v.fields,v.lines,v.pixels,v.bytes,v.wraps,v.vidcur,v.flyback_rises,v.flyback_falls);
        printf("pcm=%016" PRIx64 " frames=%" PRIu64 " generated=%" PRIu64 " accepted=%" PRIu64 " discarded=%" PRIu64 " queued=%zu dma=%" PRIu64 "/%" PRIu64 "\n",
            pcm_hash,pcm_frames,a.generated,a.accepted,a.discarded,a.queued,a.dma_frames,a.dma_blocks);
        printf("first-raster=%016" PRIx64 " pcm-prefix=%016" PRIx64 " prefix-frames=%" PRIu64 "\n",
            first_raster_hash,pcm_prefix_hash,pcm_frames<480?pcm_frames:UINT64_C(480));
        if (ram_count) {
            fputs("ram=",stdout);
            for(uint64_t i=0;i<ram_count;++i) {
                uint32_t value;
                if (!read_ram((unsigned)(ram_offset+i*4),&value)) { result=1; break; }
                printf("%s%08x",i?",":"",value);
            }
            puts("");
        }
        if (print_cmos) {
            uint8_t bytes[256]; cmos_snapshot(bytes);
            fputs("cmos=", stdout); for(unsigned i=0;i<256;++i)printf("%02x",bytes[i]);puts("");
        }
        benchmark_elapsed=arc_test_clock_ns()-benchmark_start;
        if (s.faulted) { result=1; break; }
        if (capture_path && c.state==ARC_RPC_CAPTURE_DONE) {
            FILE *file=fopen(capture_path,"wb");
            if(!file) result=2;
            else { bool ok=fwrite(c.bytes,1,256,file)==256; if(fclose(file))ok=false; if(!ok)result=2; }
            printf("capture writes=%" PRIu64 " mask=",c.writes); for(unsigned i=0;i<32;++i)printf("%02x",c.written[i]);puts(""); break;
        }
        if (measure && benchmark_elapsed >= (warmup+measure)*UINT64_C(1000000000)) break;
        uint64_t pause=0;
        if(!strcmp(pacing,"burst")) pause=f%3==0?3000000:0;
        if(!strcmp(pacing,"realtime")) {
            uint64_t spent=arc_test_clock_ns()-frontend_start;
            if(spent<period_ns)pause=period_ns-spent;
        }
        if(pause)pause_ns(pause);
    }
    if(capture_path) { ArcRpcCapture c;capture(&c);if(c.state!=ARC_RPC_CAPTURE_DONE)result=1; }
    if(measure && (benchmark_elapsed<(warmup+measure)*UINT64_C(1000000000) || !sample_count))result=1;
    if(sample_count) {
        qsort(times,sample_count,sizeof(*times),compare);
        uint64_t p50=quantile(times,sample_count,50,100),p95=quantile(times,sample_count,95,100),p99=quantile(times,sample_count,99,100),p999=quantile(times,sample_count,999,1000);
        fprintf(stderr,"timing frames=%zu T_ns=%" PRIu64 " p50=%" PRIu64 " p95=%" PRIu64 " p99=%" PRIu64 " p99.9=%" PRIu64 " p95/T=%.6f p99.9/T=%.6f budget=%s\n",
            sample_count,period_ns,p50,p95,p99,p999,(double)p95/period_ns,(double)p999/period_ns,
            p95<=period_ns*8/10 && times[sample_count-1]<=period_ns?"pass":"fail");
        fprintf(stderr,"throughput guest_instructions=%" PRIu64 " guest_ns=%" PRIu64 " guest_instructions/s=%.3f host_ns/instruction=%.6f max/T=%.6f core/guest=%.6f\n",
            measured_instructions,measured_guest_ns,measured_guest_ns?(double)measured_instructions*1e9/measured_guest_ns:0,
            measured_instructions?(double)measured_core_ns/measured_instructions:0,
            (double)times[sample_count-1]/period_ns,measured_guest_ns?(double)measured_core_ns/measured_guest_ns:0);
    }
    for (unsigned i = 0; i < dump_count; ++i) {
        if (!dumps[i].written) {
            fprintf(stderr,"No frame dump completed for frame %" PRIu64 "\n",dumps[i].frame);
            dump_failed = true;
        }
    }
    if (dump_failed) result = 2;
    free(dump_pixels);
    free(times); if(timing && fclose(timing))result=2; if(ferror(stdout))result=2;
    unload(); deinit();
#if defined(_WIN32)
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return (int)result;
usage:
    fputs("rpc_runner --core LIB --plan FILE [--system DIR] [--save DIR] [--frames N | --warmup-seconds 120 --measure-seconds 600] [--pacing flat|burst|realtime] [--disk-writes off|shadow|original] [--jfd-policy as-imaged|off] [--jfd-policy-at FRAME VALUE] [--disk-writes-at FRAME VALUE] [--jfd-omitted strict|file-data] [--timing CSV] [--dump-frame N FILE.ppm ...] [--capture-seed FILE] [--ram-offset N --ram-words N] [--reset-at N] [--cmos] [--fail-alloc 1..5] [--fault-at N] [--reject]\n",stderr);
    return 2;
}
