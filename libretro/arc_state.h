/* ArcDroid save-state graph codec. GPL-2.0-only. */
#ifndef ARC_STATE_H
#define ARC_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct ArcState ArcState;
typedef void (*ArcStateVisit)(ArcState *);
/* Registration precedes field traversal. Native pointers are encoded as
 * region identities and bounded offsets, never as process addresses. */
ArcState *arc_state_begin(const void *input, size_t length);
ArcState *arc_state_begin_limited(const void *input, size_t length, size_t decoded_limit);
void arc_state_end(ArcState *state);
/* Callback form of free(p), preserving the census allocator interception. */
void arc_state_free(void *pointer);
void arc_state_clear_cache(void);
void *arc_state_scratch(ArcState *state,size_t bytes);
void arc_state_stage(ArcState *state,void *destination,const void *source,size_t bytes);
const void *arc_state_delta(ArcState *state,const char *name,const void *current,const void *base,size_t bytes);
const void *arc_state_view(ArcState *state,const char *name,const void *data,size_t bytes,size_t *view_bytes);

bool arc_state_loading(const ArcState *state);
unsigned arc_state_pass(const ArcState *state);
size_t arc_state_decoded_limit(const ArcState *state);
bool arc_state_registering(const ArcState *state);
void arc_state_fail(ArcState *state, const char *reason);
const char *arc_state_error(const ArcState *state);
void arc_state_region(ArcState *state, const char *name, void *base, size_t bytes);
void arc_state_object(ArcState *state, const char *name);
void arc_state_value(ArcState *state, void *value, size_t bytes);
void arc_state_boolean(ArcState *state, void *value, size_t bytes);
void arc_state_boolean_array(ArcState *state, void *value, size_t bytes);
#define arc_state_value(s,p,n) _Generic((p), _Bool *: arc_state_boolean, \
 _Bool (*)[]: arc_state_boolean_array, _Bool (*)[][10]: arc_state_boolean_array, \
 _Bool (*)[][512]: arc_state_boolean_array, default: arc_state_value)(s,p,n)
#define ARC_STATE_BOUND(s,value,low,high) do { \
 __typeof__(value) arc_state_checked_value; \
 arc_state_project(s,&arc_state_checked_value,&(value),sizeof(value)); \
 if((long double)arc_state_checked_value<(long double)(low)|| \
    (long double)arc_state_checked_value>(long double)(high))arc_state_fail(s,#value " range"); \
 } while(0)
#define ARC_STATE_MATCH(s,value) do { \
 __typeof__(value) arc_state_checked_value; \
 arc_state_project(s,&arc_state_checked_value,&(value),sizeof(value)); \
 if(arc_state_checked_value!=(value))arc_state_fail(s,#value " configuration"); \
 } while(0)
typedef enum { ARC_STATE_REFERENCE_ANY, ARC_STATE_REFERENCE_TIMER,
 ARC_STATE_REFERENCE_REGISTER, ARC_STATE_REFERENCE_MEMORY,
 ARC_STATE_REFERENCE_DISC_FUNCTIONS, ARC_STATE_REFERENCE_FDC_FUNCTIONS,
 ARC_STATE_REFERENCE_CONTROLLER, ARC_STATE_REFERENCE_INPUT,
 ARC_STATE_REFERENCE_JFD_IMAGE, ARC_STATE_REFERENCE_JFD_DATA, ARC_STATE_REFERENCE_MEDIA_DATA,
 ARC_STATE_REFERENCE_FILE, ARC_STATE_REFERENCE_BITMAP,
 ARC_STATE_REFERENCE_NULL } ArcStateReferenceKind;
void arc_state_target_range(ArcState *state, void *address, size_t bytes, ArcStateReferenceKind kind);
void arc_state_target(ArcState *state, void *address, ArcStateReferenceKind kind);
void arc_state_typed_reference(ArcState *state, void *slot, intptr_t bias,
                                size_t extent, size_t alignment,
                                ArcStateReferenceKind kind);
void arc_state_reference_array(ArcState *state,void *slots,size_t count,intptr_t stride,size_t extent,size_t alignment,ArcStateReferenceKind kind);
static inline void arc_state_raster_mark(uint16_t rows[2048][2],unsigned y,unsigned x,unsigned width)
{
   if(y>=2048u||x>=4096u||!width)return;
   if(width>4096u-x)width=4096u-x;
   if(!rows[y][1]||x<rows[y][0])rows[y][0]=(uint16_t)x;
   if(x+width>rows[y][1])rows[y][1]=(uint16_t)(x+width);
}

void arc_state_raster(ArcState *state,const char *name,uint32_t *pixels,size_t pitch,uint16_t rows[2048][2]);
void arc_state_reference(ArcState *state, void *slot, intptr_t bias,
                         size_t extent, size_t alignment);
void arc_state_expect(ArcState *state, const char *name, const void *value, size_t bytes);
void arc_state_buffer(ArcState *state, const char *name, void *value, size_t bytes);
void arc_state_string(ArcState *state, const char **slot);
void *arc_state_heap(ArcState *state, const char *name, void *slot,
                     size_t bytes, size_t maximum);
size_t arc_state_heap_size(ArcState *state, const char *name);
void arc_state_project(ArcState *state, void *output, const void *value, size_t bytes);
const void *arc_state_blob(ArcState *state, const char *name, const void *data,
                            size_t bytes, size_t maximum, size_t *loaded_bytes);
void arc_state_action(ArcState *state, void (*commit)(void *),
                       void (*rollback)(void *), void *opaque);
bool arc_state_run(ArcState *state, ArcStateVisit visit);
bool arc_state_output(ArcState *state, void *output, size_t size);
size_t arc_state_size(const ArcState *state);
size_t arc_state_static_bound(ArcStateVisit visit);
bool arc_state_measuring(const ArcState *state);
bool arc_state_measure(ArcState *state,size_t bytes);
bool arc_state_commit(ArcState *state);
#endif
