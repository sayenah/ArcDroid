/* Dormant RiscPC sentinel ownership. GPL-2.0-only. */
#ifndef ARCDROID_ARC_LOCK_H
#define ARCDROID_ARC_LOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARC_LOCK_HELD, ARC_LOCK_CONTENDED, ARC_LOCK_TRANSIENT,
    ARC_LOCK_UNSUPPORTED, ARC_LOCK_ROOT_REFUSED, ARC_LOCK_CREATE_ERROR,
    ARC_LOCK_ERROR, ARC_LOCK_IDENTITY_LOST
} ArcLockResult;
typedef enum { ARC_LOCK_OFD, ARC_LOCK_FLOCK, ARC_LOCK_WINDOWS } ArcLockPlatform;

/* Callbacks return zero on success or a raw error in the selected domain.
 * Each open creates an independent description. try_lock MUST NOT wait.
 * identity returns false for a mismatch or an unavailable identity check.
 * No callback may read/write data, truncate, extend or remove the sentinel.
 * The table and opaque storage must outlive the token. NULL selects native. */
typedef struct {
    ArcLockPlatform platform;
    unsigned (*open_handle)(void *opaque, const char *path, intptr_t *handle);
    unsigned (*try_lock)(void *opaque, intptr_t handle);
    bool (*identity)(void *opaque, intptr_t handle, const char *path);
    void (*unlock)(void *opaque, intptr_t handle);
    void (*close_handle)(void *opaque, intptr_t handle);
} ArcLockBackend;

/* Owned, non-copyable token. Initialize before use; release before reinitializing.
 * Fields are private to arc_lock.c; exposed only to allow caller-owned storage. */
typedef struct {
    const ArcLockBackend *backend;
    void *opaque;
    intptr_t handle;
    char path[4096];
    size_t directory_length, cursor;
    bool valid, opened, held, lost, parents_done;
} ArcLock;

/* No I/O during initialization. Invalid keys/roots produce ROOT_REFUSED.
 * model is the same zero-based index as ArcRpcCmosKey (0..3). */
ArcLockResult arc_lock_content(ArcLock *, const char *save, const char *content40,
                               const ArcLockBackend *, void *opaque);
ArcLockResult arc_lock_defaults(ArcLock *, const char *save, const char *firmware64,
                                unsigned model, unsigned procedure, unsigned timing,
                                const ArcLockBackend *, void *opaque);
/* One directory namespace step OR one native attempt; never waits or retries
 * internally. Directory steps return TRANSIENT, including the final mkdir.
 * Open handles survive contention/transient lock errors. Identity-race retries
 * reopen. Deadlines and clock injection belong to the future host, not here. */
bool arc_lock_next_namespace(const ArcLock *);
ArcLockResult arc_lock_acquire(ArcLock *);
ArcLockResult arc_lock_verify(ArcLock *);
bool arc_lock_held(const ArcLock *);
void arc_lock_release(ArcLock *);

#endif
