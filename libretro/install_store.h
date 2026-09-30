/* ArcDroid persisted native hard-disc store. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_STORE_H
#define ARCDROID_INSTALL_STORE_H
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
#define ARC_HD_STORE_REVISION 1u
typedef void (*ArcInstallStoreLog)(void *, const char *);
/* Opens a per-content native disc. A created disc is READY before return. */
ArcInstallStore *arc_hd_store_open(const char *save_dir, const char *content_id,
   bool writable, ArcInstallStoreLog log, void *log_opaque,
   char *error, size_t error_size);
uint64_t arc_install_store_generation(const ArcInstallStore *store);
bool arc_install_store_created(const ArcInstallStore *store);
bool arc_install_store_wrote(const ArcInstallStore *store);
void arc_install_store_clear_wrote(ArcInstallStore *store);
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
/* Accept local VFS paths, including frontend-supplied relative save paths;
 * URI schemes are not native filesystem locations. */
bool arc_install_store_native_path(const char *path);
bool arc_install_store_probe_writable(ArcInstallStore *store);
#if defined(ARCDROID_TEST_FAULTS)
void arc_install_store_test_corrupt_native_format(bool enabled);
#endif
#endif
