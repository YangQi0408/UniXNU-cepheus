#include "cepheus_reboot.h"

static void
gate_lock(struct cepheus_reboot_gate *gate)
{
	while (__atomic_exchange_n(&gate->lock, 1u, __ATOMIC_ACQUIRE)) {
		/* State transitions are bounded; no callback runs while held. */
	}
}

static void
gate_unlock(struct cepheus_reboot_gate *gate)
{
	__atomic_store_n(&gate->lock, 0u, __ATOMIC_RELEASE);
}

void
cepheus_reboot_init(struct cepheus_reboot_gate *gate, bool enabled)
{
	if (!gate) return;
	*gate = (struct cepheus_reboot_gate){ .enabled = enabled, .lock = 0 };
}

int
cepheus_reboot_worker_enter(struct cepheus_reboot_gate *gate)
{
	if (!gate) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (gate->shutdown_started || gate->preparing || gate->prepared) {
		gate_unlock(gate);
		return CEPHEUS_EBUSY;
	}
	if (gate->workers == UINT32_MAX) {
		gate_unlock(gate);
		return CEPHEUS_EBUSY;
	}
	gate->workers++;
	gate_unlock(gate);
	return CEPHEUS_OK;
}

int
cepheus_reboot_worker_leave(struct cepheus_reboot_gate *gate, int worker_error)
{
	if (!gate) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (gate->workers == 0) {
		gate_unlock(gate);
		return CEPHEUS_EINVAL;
	}
	gate->workers--;
	if (worker_error) gate->failed_worker = true;
	gate_unlock(gate);
	return CEPHEUS_OK;
}

void
cepheus_reboot_set_entropy(struct cepheus_reboot_gate *gate, bool ready)
{
	if (!gate) return;
	gate_lock(gate);
	gate->entropy_ready = ready;
	gate_unlock(gate);
}

void
cepheus_reboot_set_storage(struct cepheus_reboot_gate *gate, bool clean)
{
	if (!gate) return;
	gate_lock(gate);
	gate->storage_clean = clean;
	if (!clean) gate->prepared = false;
	gate_unlock(gate);
}

int
cepheus_reboot_register_reset(struct cepheus_reboot_gate *gate,
    cepheus_reset_fn reset, void *context)
{
	if (!gate || !reset) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (gate->shutdown_started || gate->commit_used || gate->preparing ||
	    gate->prepared) {
		gate_unlock(gate);
		return CEPHEUS_EINVAL;
	}
	if (gate->reset_registered) {
		gate_unlock(gate);
		return CEPHEUS_EBUSY;
	}
	gate->reset = reset;
	gate->reset_context = context;
	gate->reset_registered = true;
	gate_unlock(gate);
	return CEPHEUS_OK;
}

int
cepheus_reboot_prepare(struct cepheus_reboot_gate *gate,
    const struct cepheus_reboot_hooks *hooks)
{
	if (!gate || !hooks || !hooks->stop_workers || !hooks->join_workers ||
	    !hooks->sync_storage || !hooks->storage_clean) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (!gate->enabled) {
		gate_unlock(gate);
		return CEPHEUS_EPERM;
	}
	if (!gate->entropy_ready || !gate->reset_registered ||
	    gate->shutdown_started || gate->preparing || gate->commit_used) {
		gate_unlock(gate);
		return CEPHEUS_ENOTSUP;
	}
	gate->preparing = true;
	gate->prepared = false;
	gate_unlock(gate);

	int error = hooks->stop_workers(hooks->context);
	if (!error) error = hooks->join_workers(hooks->context);
	gate_lock(gate);
	if (!error && (gate->workers || gate->failed_worker)) error = CEPHEUS_EBUSY;
	if (error) gate->failed_worker = true;
	gate_unlock(gate);
	if (!error) error = hooks->sync_storage(hooks->context);
	if (!error && hooks->storage_clean(hooks->context)) error = CEPHEUS_EBUSY;

	gate_lock(gate);
	gate->preparing = false;
	if (error) {
		gate->storage_clean = false;
		gate->prepared = false;
	} else {
		gate->storage_clean = true;
		gate->prepared = true;
	}
	gate_unlock(gate);
	return error;
}

int
cepheus_reboot_quiesce(struct cepheus_reboot_gate *gate)
{
	if (!gate) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (!gate->enabled) {
		gate_unlock(gate);
		return CEPHEUS_EPERM;
	}
	if (!gate->prepared || gate->workers || gate->failed_worker ||
	    !gate->storage_clean || gate->shutdown_started) {
		gate_unlock(gate);
		return CEPHEUS_EBUSY;
	}
	gate_unlock(gate);
	return CEPHEUS_OK;
}

int
cepheus_reboot_authorize(const struct cepheus_reboot_gate *gate,
    enum cepheus_reboot_action action)
{
	if (!gate) return CEPHEUS_EINVAL;
	struct cepheus_reboot_gate *mutable_gate = (struct cepheus_reboot_gate *)gate;
	gate_lock(mutable_gate);
	if (action == CEPHEUS_STATUS) {
		gate_unlock(mutable_gate);
		return CEPHEUS_OK;
	}
	if (action != CEPHEUS_DRY_RUN && action != CEPHEUS_COMMIT) {
		gate_unlock(mutable_gate);
		return CEPHEUS_EINVAL;
	}
	if (!gate->enabled) {
		gate_unlock(mutable_gate);
		return CEPHEUS_EPERM;
	}
	if (!gate->entropy_ready || !gate->reset_registered || !gate->reset) {
		gate_unlock(mutable_gate);
		return CEPHEUS_ENOTSUP;
	}
	if (!gate->storage_clean || gate->workers || gate->failed_worker ||
	    gate->preparing) {
		gate_unlock(mutable_gate);
		return CEPHEUS_EBUSY;
	}
	if (action == CEPHEUS_COMMIT && !gate->prepared) {
		gate_unlock(mutable_gate);
		return CEPHEUS_ENOTSUP;
	}
	if (gate->shutdown_started || gate->commit_used) {
		gate_unlock(mutable_gate);
		return CEPHEUS_EBUSY;
	}
	gate_unlock(mutable_gate);
	return CEPHEUS_OK;
}

int
cepheus_reboot_execute(struct cepheus_reboot_gate *gate)
{
	if (!gate) return CEPHEUS_EINVAL;
	gate_lock(gate);
	if (!gate->enabled || !gate->entropy_ready || !gate->storage_clean ||
	    gate->workers || gate->failed_worker || !gate->reset_registered ||
	    !gate->reset || !gate->prepared || gate->shutdown_started ||
	    gate->commit_used) {
		gate_unlock(gate);
		return CEPHEUS_EBUSY;
	}
	gate->commit_used = true;
	gate->shutdown_started = true;
	cepheus_reset_fn reset = gate->reset;
	void *context = gate->reset_context;
	gate_unlock(gate);
	/* The callback is the only permitted platform reset seam. No fallback. */
	return reset(context);
}
