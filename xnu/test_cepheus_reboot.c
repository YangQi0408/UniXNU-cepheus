#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_reboot.c"

static int resets;
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock_gate(void *context) { pthread_mutex_lock(context); }
static void unlock_gate(void *context) { pthread_mutex_unlock(context); }
static int reset(void *context) { (void)context; resets++; return 0; }
static int ok(void *context) { (void)context; return 0; }
static int fail(void *context) { (void)context; return CEPHEUS_EIO; }

struct blocked_sync {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool entered;
    bool release;
};
static int blocked_sync_storage(void *context)
{
    struct blocked_sync *sync = context;
    pthread_mutex_lock(&sync->mutex);
    sync->entered = true;
    pthread_cond_broadcast(&sync->condition);
    while (!sync->release) pthread_cond_wait(&sync->condition, &sync->mutex);
    pthread_mutex_unlock(&sync->mutex);
    return 0;
}
static int clean_storage(void *context) { (void)context; return 0; }
struct prepare_args { struct cepheus_reboot_gate *gate; struct cepheus_reboot_hooks hooks; int result; };
static void *prepare_thread(void *context)
{
    struct prepare_args *args = context;
    args->result = cepheus_reboot_prepare(args->gate, &args->hooks);
    return NULL;
}
static int init_gate(struct cepheus_reboot_gate *gate)
{
    return cepheus_reboot_init(gate, true, lock_gate, unlock_gate, &gate_mutex);
}
static void ready(struct cepheus_reboot_gate *gate)
{
    struct cepheus_reboot_hooks hooks = { ok, ok, ok, clean_storage, NULL };
    assert(init_gate(gate) == CEPHEUS_OK);
    cepheus_reboot_set_entropy(gate, true);
    assert(cepheus_reboot_register_reset(gate, reset, &resets) == CEPHEUS_OK);
    assert(cepheus_reboot_prepare(gate, &hooks) == CEPHEUS_OK);
}

int main(void)
{
    struct cepheus_reboot_gate gate;
    memset(&gate, 0, sizeof(gate));
    assert(cepheus_reboot_init(&gate, true, NULL, unlock_gate, &gate_mutex) == CEPHEUS_EINVAL);
    assert(cepheus_reboot_init(&gate, true, lock_gate, NULL, &gate_mutex) == CEPHEUS_EINVAL);
    memset(&gate, 0, sizeof(gate));
    assert(cepheus_reboot_worker_enter(&gate) == CEPHEUS_EINVAL);
    assert(init_gate(&gate) == CEPHEUS_OK);
    assert(cepheus_reboot_authorize(&gate, CEPHEUS_STATUS) == CEPHEUS_OK);
    assert(cepheus_reboot_authorize(&gate, CEPHEUS_DRY_RUN) == CEPHEUS_ENOTSUP);

    ready(&gate);
    assert(cepheus_reboot_authorize(&gate, CEPHEUS_DRY_RUN) == CEPHEUS_OK);
    assert(cepheus_reboot_authorize(&gate, CEPHEUS_COMMIT) == CEPHEUS_ENOTSUP);
    assert(cepheus_reboot_execute(&gate) == CEPHEUS_ENOTSUP);
    assert(resets == 0 && !gate.shutdown_started && !gate.commit_used);

    assert(init_gate(&gate) == CEPHEUS_OK);
    cepheus_reboot_set_entropy(&gate, true);
    assert(cepheus_reboot_register_reset(&gate, reset, &resets) == CEPHEUS_OK);
    struct cepheus_reboot_hooks failed = { fail, ok, ok, clean_storage, NULL };
    assert(cepheus_reboot_prepare(&gate, &failed) == CEPHEUS_EIO);
    assert(gate.shutdown_started && gate.admission_closed && gate.failed_worker);
    assert(cepheus_reboot_worker_enter(&gate) == CEPHEUS_EBUSY);
    assert(cepheus_reboot_execute(&gate) == CEPHEUS_ENOTSUP && resets == 0);

    assert(init_gate(&gate) == CEPHEUS_OK);
    cepheus_reboot_set_entropy(&gate, true);
    assert(cepheus_reboot_register_reset(&gate, reset, &resets) == CEPHEUS_OK);
    struct blocked_sync sync = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false, false };
    struct prepare_args args = {
        .gate = &gate,
        .hooks = { ok, ok, blocked_sync_storage, clean_storage, &sync },
        .result = CEPHEUS_OK,
    };
    pthread_t thread;
    assert(pthread_create(&thread, NULL, prepare_thread, &args) == 0);
    pthread_mutex_lock(&sync.mutex);
    while (!sync.entered) pthread_cond_wait(&sync.condition, &sync.mutex);
    pthread_mutex_unlock(&sync.mutex);
    /* This setter must acquire the gate while sync I/O is blocked outside it. */
    cepheus_reboot_set_storage(&gate, false);
    pthread_mutex_lock(&sync.mutex);
    sync.release = true;
    pthread_cond_broadcast(&sync.condition);
    pthread_mutex_unlock(&sync.mutex);
    assert(pthread_join(thread, NULL) == 0);
    assert(args.result == CEPHEUS_EBUSY);
    assert(gate.shutdown_started && gate.failed_worker && !gate.prepared);
    assert(cepheus_reboot_worker_enter(&gate) == CEPHEUS_EBUSY);
    assert(cepheus_reboot_authorize(&gate, CEPHEUS_COMMIT) == CEPHEUS_ENOTSUP);
    assert(cepheus_reboot_execute(&gate) == CEPHEUS_ENOTSUP && resets == 0);
    pthread_cond_destroy(&sync.condition);
    pthread_mutex_destroy(&sync.mutex);

    /* Preparation may establish clean storage without a pre-set clean flag. */
    assert(init_gate(&gate) == CEPHEUS_OK);
    cepheus_reboot_set_entropy(&gate, true);
    assert(cepheus_reboot_register_reset(&gate, reset, &resets) == CEPHEUS_OK);
    struct cepheus_reboot_hooks hooks = { ok, ok, ok, clean_storage, NULL };
    assert(cepheus_reboot_prepare(&gate, &hooks) == CEPHEUS_OK);
    assert(gate.storage_clean && gate.prepared && resets == 0);
    puts("reboot gates: lock validation, dirty transition, parked failure, and unconditional COMMIT refusal: PASS");
    return 0;
}
