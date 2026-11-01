#include "cepheus_reboot.h"

static void
gate_lock(struct cepheus_reboot_gate *gate)
{
    gate->lock(gate->lock_context);
}

static void
gate_unlock(struct cepheus_reboot_gate *gate)
{
    gate->unlock(gate->lock_context);
}

int
cepheus_reboot_init(struct cepheus_reboot_gate *gate, bool enabled,
    cepheus_lock_fn lock, cepheus_lock_fn unlock, void *lock_context)
{
    if (!gate || !lock || !unlock) return CEPHEUS_EINVAL;
    *gate = (struct cepheus_reboot_gate){
        .enabled = enabled,
        .lock = lock,
        .unlock = unlock,
        .lock_context = lock_context,
    };
    return CEPHEUS_OK;
}

int
cepheus_reboot_worker_enter(struct cepheus_reboot_gate *gate)
{
    if (!gate || !gate->lock || !gate->unlock) return CEPHEUS_EINVAL;
    gate_lock(gate);
    if (gate->shutdown_started || gate->admission_closed || gate->preparing ||
        gate->prepared || gate->workers == UINT32_MAX) {
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
    if (!gate || !gate->lock || !gate->unlock) return CEPHEUS_EINVAL;
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
    if (!gate || !gate->lock || !gate->unlock) return;
    gate_lock(gate);
    gate->entropy_ready = ready;
    gate_unlock(gate);
}

void
cepheus_reboot_set_storage(struct cepheus_reboot_gate *gate, bool clean)
{
    if (!gate || !gate->lock || !gate->unlock) return;
    gate_lock(gate);
    gate->storage_clean = clean;
    gate->generation++;
    if (!clean) gate->prepared = false;
    gate_unlock(gate);
}

int
cepheus_reboot_register_reset(struct cepheus_reboot_gate *gate,
    cepheus_reset_fn reset, void *context)
{
    if (!gate || !reset || !gate->lock || !gate->unlock) return CEPHEUS_EINVAL;
    gate_lock(gate);
    if (gate->shutdown_started || gate->admission_closed || gate->commit_used ||
        gate->preparing || gate->prepared) {
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
        !hooks->sync_storage || !hooks->storage_clean || !gate->lock ||
        !gate->unlock) return CEPHEUS_EINVAL;

    gate_lock(gate);
    if (!gate->enabled || !gate->entropy_ready || !gate->reset_registered ||
        gate->shutdown_started || gate->preparing || gate->commit_used) {
        int result = gate->enabled ? CEPHEUS_ENOTSUP : CEPHEUS_EPERM;
        gate_unlock(gate);
        return result;
    }
    gate->preparing = true;
    gate->admission_closed = true;
    gate->prepared = false;
    uint64_t generation = gate->generation;
    gate_unlock(gate);

    int error = hooks->stop_workers(hooks->context);
    if (!error) error = hooks->join_workers(hooks->context);
    gate_lock(gate);
    if (!error && (gate->workers || gate->failed_worker ||
        gate->generation != generation)) error = CEPHEUS_EBUSY;
    gate_unlock(gate);
    if (!error) error = hooks->sync_storage(hooks->context);
    if (!error && hooks->storage_clean(hooks->context) != 0) error = CEPHEUS_EBUSY;

    gate_lock(gate);
    gate->preparing = false;
    if (error || gate->generation != generation) {
        gate->failed_worker = true;
        gate->shutdown_started = true;
        gate->storage_clean = false;
        gate->prepared = false;
        if (!error) error = CEPHEUS_EBUSY;
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
    if (!gate || !gate->lock || !gate->unlock) return CEPHEUS_EINVAL;
    gate_lock(gate);
    int result = !gate->enabled ? CEPHEUS_EPERM :
        ((!gate->prepared || gate->workers || gate->failed_worker ||
        !gate->storage_clean || gate->shutdown_started) ? CEPHEUS_EBUSY : CEPHEUS_OK);
    gate_unlock(gate);
    return result;
}

int
cepheus_reboot_authorize(const struct cepheus_reboot_gate *constant_gate,
    enum cepheus_reboot_action action)
{
    if (!constant_gate || !constant_gate->lock || !constant_gate->unlock)
        return CEPHEUS_EINVAL;
    struct cepheus_reboot_gate *gate = (struct cepheus_reboot_gate *)constant_gate;
    gate_lock(gate);
    if (action == CEPHEUS_STATUS) {
        gate_unlock(gate);
        return CEPHEUS_OK;
    }
    if (action == CEPHEUS_COMMIT) {
        gate_unlock(gate);
        return CEPHEUS_ENOTSUP;
    }
    if (action != CEPHEUS_DRY_RUN) {
        gate_unlock(gate);
        return CEPHEUS_EINVAL;
    }
    if (!gate->enabled) {
        gate_unlock(gate);
        return CEPHEUS_EPERM;
    }
    if (!gate->entropy_ready || !gate->reset_registered || !gate->reset) {
        gate_unlock(gate);
        return CEPHEUS_ENOTSUP;
    }
    if (!gate->storage_clean || gate->workers || gate->failed_worker ||
        gate->preparing || gate->shutdown_started) {
        gate_unlock(gate);
        return CEPHEUS_EBUSY;
    }
    gate_unlock(gate);
    return CEPHEUS_OK;
}

int
cepheus_reboot_execute(struct cepheus_reboot_gate *gate)
{
    (void)gate;
    return CEPHEUS_ENOTSUP;
}
