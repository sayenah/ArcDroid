/* ArcDroid FileCore disc-request observer. GPL-2.0-only. */
#ifndef ARC_DISC_REQUEST_H
#define ARC_DISC_REQUEST_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { ARC_DISC_SIGNAL_NONE, ARC_DISC_SIGNAL_A, ARC_DISC_SIGNAL_B } ArcDiscSignal;
typedef enum { ARC_DISC_CATALOGUE, ARC_DISC_LOAD, ARC_DISC_OPEN_FILE,
               ARC_DISC_OPEN_DIR } ArcDiscOperation;
typedef struct {
   bool armed, a_active, b_triggered, a_notified;
   char a_name[11], object[256], previous_object[256];
   uint32_t continuation, sp, previous_continuation, previous_sp;
   unsigned candidate, previous_candidate, observations;
   ArcDiscOperation operation;
   uint64_t previous_us;
} ArcDiscRequest;

typedef struct {
   uint32_t swi, r0, r1, r3, address, mode, user_sp;
   bool in_rom, name_readable;
   const char *name;
   unsigned candidate; /* Unique compatible candidate, or UINT_MAX. */
   uint64_t now_us;
} ArcDiscSwi;

typedef struct {
   bool trigger, observed, discarded;
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
bool arc_disc_request_name(const char *name, const char *inserted_name,
                           char canonical[256]);
bool arc_disc_request_operation(uint32_t swi, uint32_t r0,
                                ArcDiscOperation *operation);
#endif
