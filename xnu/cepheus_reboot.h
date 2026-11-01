/* Refusal-first reset contract. Lock callbacks must be thread-context safe. */
#ifndef CEPHEUS_REBOOT_H
#define CEPHEUS_REBOOT_H
#include <stdbool.h>
#include <stdint.h>

enum cepheus_reboot_action {
    CEPHEUS_STATUS = 1,
    CEPHEUS_DRY_RUN = 2,
    CEPHEUS_COMMIT = 3,
};
enum cepheus_reboot_result {
    CEPHEUS_OK = 0, CEPHEUS_EINVAL = 22, CEPHEUS_EBUSY = 16,
    CEPHEUS_EPERM = 1, CEPHEUS_ENOTSUP = 45, CEPHEUS_EIO = 5,
};
typedef int (*cepheus_reset_fn)(void *context);
typedef int (*cepheus_gate_fn)(void *context);
typedef void (*cepheus_lock_fn)(void *context);
struct cepheus_reboot_hooks {
    cepheus_gate_fn stop_workers;
    cepheus_gate_fn join_workers;
    cepheus_gate_fn sync_storage;
    /* Return zero only when storage is clean and unmounted. */
    cepheus_gate_fn storage_clean;
    void *context;
};
struct cepheus_reboot_gate {
    cepheus_lock_fn lock;
    cepheus_lock_fn unlock;
    void *lock_context;
    uint64_t generation;
    unsigned workers;
    bool enabled, entropy_ready, storage_clean, shutdown_started;
    bool failed_worker, reset_registered, commit_used, preparing, prepared;
    bool admission_closed;
    cepheus_reset_fn reset;
    void *reset_context;
};
int cepheus_reboot_init(struct cepheus_reboot_gate *, bool, cepheus_lock_fn,
    cepheus_lock_fn, void *);
int cepheus_reboot_worker_enter(struct cepheus_reboot_gate *);
int cepheus_reboot_worker_leave(struct cepheus_reboot_gate *, int);
void cepheus_reboot_set_entropy(struct cepheus_reboot_gate *, bool);
void cepheus_reboot_set_storage(struct cepheus_reboot_gate *, bool);
int cepheus_reboot_register_reset(struct cepheus_reboot_gate *, cepheus_reset_fn, void *);
int cepheus_reboot_prepare(struct cepheus_reboot_gate *, const struct cepheus_reboot_hooks *);
int cepheus_reboot_quiesce(struct cepheus_reboot_gate *);
int cepheus_reboot_authorize(const struct cepheus_reboot_gate *, enum cepheus_reboot_action);
int cepheus_reboot_execute(struct cepheus_reboot_gate *);
#endif
