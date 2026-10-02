/* ArcDroid FileCore disc-request observer. GPL-2.0-only. */
#ifndef ARC_DISC_REQUEST_H
#define ARC_DISC_REQUEST_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { ARC_DISC_SIGNAL_NONE, ARC_DISC_SIGNAL_A, ARC_DISC_SIGNAL_B } ArcDiscSignal;
typedef enum { ARC_DISC_CATALOGUE, ARC_DISC_LOAD, ARC_DISC_OPEN_FILE,
               ARC_DISC_OPEN_DIR } ArcDiscOperation;
#define ARC_DISC_IDENTITY_CAPACITY 8u
typedef struct {
   bool occupied;
   char object[256];
   uint32_t continuation, sp;
   uint64_t observed_us, order;
} ArcDiscIdentity;
typedef struct {
   bool armed, a_active, b_triggered, a_notified;
   char a_name[11], object[256];
   uint32_t continuation, sp;
   unsigned candidate, observations, triggered_slot;
   ArcDiscOperation operation;
   ArcDiscIdentity identities[ARC_DISC_IDENTITY_CAPACITY];
   uint64_t next_order;
} ArcDiscRequest;

typedef struct {
   uint32_t swi, r0, r1, r3, address, mode, user_sp;
   bool in_rom, name_readable;
   const char *name;
   unsigned candidate; /* Unit-test wrapper input; production resolves at continuation. */
   uint64_t now_us;
} ArcDiscSwi;

typedef struct {
   bool trigger, observed, discarded, needs_resolution;
   unsigned candidate, observation;
   const char *reason;
} ArcDiscResult;

void arc_disc_request_clear(ArcDiscRequest *request);
void arc_disc_request_swi(ArcDiscRequest *request, const ArcDiscSwi *event,
                          const char *inserted_name);
ArcDiscResult arc_disc_request_continue(ArcDiscRequest *request,
   uint32_t address, uint32_t mode, uint32_t sp, uint32_t r0,
   bool overflow, bool error_readable, uint32_t error_number,
   uint64_t now_us);
ArcDiscResult arc_disc_request_outcome(ArcDiscRequest *request,
   uint32_t address, uint32_t mode, uint32_t sp, uint32_t r0,
   bool overflow, bool error_readable, uint32_t error_number);
ArcDiscResult arc_disc_request_resolved(ArcDiscRequest *request,
   unsigned candidate, uint64_t now_us);
void arc_disc_request_retry(ArcDiscRequest *request);
bool arc_disc_request_name(const char *name, const char *inserted_name,
                           char canonical[256]);
bool arc_disc_request_operation(uint32_t swi, uint32_t r0,
                                ArcDiscOperation *operation);
#endif
