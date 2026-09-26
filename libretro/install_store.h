/* ArcDroid persisted install-disc store. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_STORE_H
#define ARCDROID_INSTALL_STORE_H
#include "adffs.h"
#include "install_disc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {
   ARC_INSTALL_STORE_NEW,
   ARC_INSTALL_STORE_UNPUBLISHED,
   ARC_INSTALL_STORE_READY,
   ARC_INSTALL_STORE_DAMAGED
} ArcInstallStoreState;
typedef struct ArcInstallStore ArcInstallStore;
typedef void (*ArcInstallStoreLog)(void *, const char *);
/* Caller owns the returned handle. A missing store remains unwritten if
 * create_candidate is false. Damage never mutates the directory. */
ArcInstallStore *arc_install_store_open(const char *save_dir,
   const ArcInstallPlan *plan, bool create_candidate, bool writable,
   ArcInstallStoreLog log, void *log_opaque, char *error, size_t error_size);
void arc_install_store_close(ArcInstallStore *store);
ArcInstallStoreState arc_install_store_state(const ArcInstallStore *store);
const uint8_t *arc_install_store_image(const ArcInstallStore *store);
const char *arc_install_store_id(const ArcInstallStore *store);
bool arc_install_store_read(void *opaque, uint32_t lba, uint32_t count,
                            uint8_t *data);
bool arc_install_store_write(void *opaque, uint32_t lba, uint32_t count,
                             const uint8_t *data);
void arc_install_store_failed(void *opaque, const char *reason);
bool arc_install_store_writable(const ArcInstallStore *store);
bool arc_install_store_probe_writable(ArcInstallStore *store);
bool arc_install_store_publish(ArcInstallStore *store,
   char *error, size_t error_size);
#endif
