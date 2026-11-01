/*
 * Refusal-first reset contract for Cepheus.
 *
 * This is an interface, not a PMIC/MMIO implementation. A platform owner must
 * register a source-supported reset callback before COMMIT can succeed.
 */
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
	CEPHEUS_OK = 0,
	CEPHEUS_EINVAL = 22,
	CEPHEUS_EBUSY = 16,
	CEPHEUS_EPERM = 1,
	CEPHEUS_ENOTSUP = 45,
	CEPHEUS_EIO = 5,
};

typedef int (*cepheus_reset_fn)(void *context);
typedef int (*cepheus_gate_fn)(void *context);
struct cepheus_reboot_hooks {
	cepheus_gate_fn stop_workers;
	cepheus_gate_fn join_workers;
	cepheus_gate_fn sync_storage;
	/* Return zero only after the storage layer reports clean/unmounted. */
	cepheus_gate_fn storage_clean;
	void *context;
};

struct cepheus_reboot_gate {
	volatile unsigned lock;
	unsigned workers;
	bool enabled;
	bool entropy_ready;
	bool storage_clean;
	bool shutdown_started;
	bool failed_worker;
	bool reset_registered;
	bool commit_used;
	bool preparing;
	bool prepared;
	cepheus_reset_fn reset;
	void *reset_context;
};

void cepheus_reboot_init(struct cepheus_reboot_gate *gate, bool enabled);
int cepheus_reboot_worker_enter(struct cepheus_reboot_gate *gate);
int cepheus_reboot_worker_leave(struct cepheus_reboot_gate *gate, int worker_error);
void cepheus_reboot_set_entropy(struct cepheus_reboot_gate *gate, bool ready);
void cepheus_reboot_set_storage(struct cepheus_reboot_gate *gate, bool clean);
int cepheus_reboot_register_reset(struct cepheus_reboot_gate *gate,
    cepheus_reset_fn reset, void *context);
int cepheus_reboot_prepare(struct cepheus_reboot_gate *gate,
    const struct cepheus_reboot_hooks *hooks);
int cepheus_reboot_quiesce(struct cepheus_reboot_gate *gate);
int cepheus_reboot_authorize(const struct cepheus_reboot_gate *gate,
    enum cepheus_reboot_action action);
int cepheus_reboot_execute(struct cepheus_reboot_gate *gate);

#endif
