#include <assert.h>
#include <stdio.h>
#include "cepheus_reboot.c"

static int resets;
static int reset(void *context) { (void)context; resets++; return 0; }

static int hook(void *context) { (void)context; return 0; }
static void ready(struct cepheus_reboot_gate *g)
{
	struct cepheus_reboot_hooks hooks = {hook, hook, hook, hook, NULL};
	cepheus_reboot_init(g, true);
	cepheus_reboot_set_entropy(g, true);
	cepheus_reboot_set_storage(g, true);
	assert(cepheus_reboot_register_reset(g, reset, &resets) == 0);
	assert(cepheus_reboot_prepare(g, &hooks) == 0);
}

int main(void)
{
	struct cepheus_reboot_gate g;
	cepheus_reboot_init(&g, false);
	assert(cepheus_reboot_authorize(&g, CEPHEUS_STATUS) == 0);
	assert(cepheus_reboot_authorize(&g, CEPHEUS_DRY_RUN) == 1);
	assert(cepheus_reboot_register_reset(&g, reset, &resets) == 0);
	assert(cepheus_reboot_register_reset(&g, reset, &resets) == 16);

	ready(&g);
	/* A worker already entered before prepare makes preparation refuse. */
	assert(cepheus_reboot_worker_enter(&g) == 16); /* prepared gate */
	assert(cepheus_reboot_quiesce(&g) == 0);
	assert(cepheus_reboot_execute(&g) == 0);
	assert(g.commit_used && g.shutdown_started && resets == 1);
	assert(cepheus_reboot_execute(&g) == 16); /* duplicate execute */
	assert(cepheus_reboot_worker_enter(&g) == 16); /* shutdown race */

	cepheus_reboot_init(&g, true);
	cepheus_reboot_set_entropy(&g, true);
	cepheus_reboot_set_storage(&g, true);
	assert(cepheus_reboot_worker_enter(&g) == 0);
	assert(cepheus_reboot_worker_leave(&g, 5) == 0);
	assert(cepheus_reboot_register_reset(&g, reset, &resets) == 0);
	{
		struct cepheus_reboot_hooks hooks = {hook, hook, hook, hook, NULL};
		assert(cepheus_reboot_prepare(&g, &hooks) == 16);
	}
	assert(cepheus_reboot_authorize(&g, CEPHEUS_COMMIT) == 16);

	ready(&g);
	cepheus_reboot_set_storage(&g, false);
	assert(cepheus_reboot_authorize(&g, CEPHEUS_DRY_RUN) == 16);
	cepheus_reboot_set_storage(&g, true);
	assert(cepheus_reboot_authorize(&g, CEPHEUS_DRY_RUN) == 0);
	puts("reboot gates: disabled, dirty, worker failure, duplicate and race: PASS");
	return 0;
}
