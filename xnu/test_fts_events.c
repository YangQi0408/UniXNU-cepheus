#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_fts_events.h"
#include "cepheus_clock_status.h"
#include "cepheus_gpi_layout.h"

int main(void)
{
	struct cepheus_fts_state s = {0};
	const uint8_t enter[] = {0x13, 0x21, 0x34, 0x52, 0x34, 0, 0, 1};
	assert(cepheus_fts_event(&s, enter) == 0);
	assert(s.active == (1u << 2) && s.x[2] == 0x234 && s.y[2] == 0x345);
	const uint8_t status[] = {0x43, 0, 0, 0, 0, 0, 0, 0};
	assert(cepheus_fts_event(&s, status) == 0 && s.active == 4);
	const uint8_t motion[] = {0x23, 0x21, 0x35, 0x62, 0x34, 0, 0, 0};
	assert(cepheus_fts_event(&s, motion) == 0 && s.x[2] == 0x235 && s.y[2] == 0x346);
	const uint8_t other[] = {0x13, 0x11, 0x00, 0x11, 0x20, 0, 0, 0};
	assert(cepheus_fts_event(&s, other) == 0 && s.active == 6 && s.x[2] == 0x235);
	const uint8_t leave[] = {0x33, 0x21, 0, 0, 0, 0, 0, 0};
	assert(cepheus_fts_event(&s, leave) == 0 && s.active == 2);
	const uint8_t invalid[] = {0x13, 0xa1, 0, 0, 0, 0, 0, 0};
	assert(cepheus_fts_event(&s, invalid) == -1 && s.active == 2);
	const uint8_t hover[] = {0x13, 0x35, 0, 0, 0, 0, 0, 0};
	assert(cepheus_fts_event(&s, hover) == 0 && s.active == 2);
	const uint8_t clamp[] = {0x13, 0x41, 0xff, 0xff, 0xff, 0, 0, 0};
	assert(cepheus_fts_event(&s, clamp) == 0 && s.x[4] == 1080 && s.y[4] == 2340);
	const uint8_t reset[] = {0x03, 0, 0, 0, 0, 0, 0, 0};
	assert(cepheus_fts_event(&s, reset) == 0 && s.active == 0);
	/* Original Xiaomi FIFO handler skips precisely ONE continuation, even
	 * across two FIFO batches. The payload is allowed to look like a touch. */
	const uint8_t extended[] = {0x53, 0x02, 0x18, 0, 0, 0, 0, 0};
	assert(!cepheus_fts_event(&s, enter) && s.active == 4);
	assert(!cepheus_fts_event(&s, extended) && s.skip_next == 1 && s.active == 4);
	struct cepheus_fts_state next_batch = s;
	assert(!cepheus_fts_event(&next_batch, other) && !next_batch.skip_next && next_batch.active == 4);
	assert(!cepheus_fts_event(&next_batch, other) && next_batch.active == 6);
	assert(!cepheus_fts_event(&next_batch, extended) && next_batch.skip_next);
	assert(!cepheus_fts_event(&next_batch, reset) && next_batch.active == 6 && !next_batch.skip_next);
	assert(!cepheus_fts_event(&next_batch, reset) && !next_batch.active);
	const uint8_t unrelated[] = {0x53, 0x02, 0x17, 0, 0, 0, 0, 0};
	assert(!cepheus_fts_event(&next_batch, unrelated) && !next_batch.skip_next);
	assert(!cepheus_fts_event(&next_batch, enter) && next_batch.active == 4);
	assert(!cepheus_fts_event(&next_batch, extended));
	assert(!cepheus_fts_event(&next_batch, invalid) && !next_batch.skip_next && next_batch.active == 4);
	assert(cepheus_fts_event(&next_batch, invalid) == -1);
	puts("FTS enter/motion/leave, retained contacts, limits and reset: PASS");
	puts("Xiaomi 53:02:18 continuation: one-slot skip across batches, no fake touch/reset dispatch PASS (synthetic FIFO)");
	assert(cepheus_branch_enabled(0x4220u, 0));
	assert(cepheus_branch_enabled(0x80000002u, 1));
	assert(!cepheus_branch_enabled(0x80000002u, 0));
	assert(!cepheus_branch_enabled(0x80000000u, 1));
	assert(cepheus_branch_enabled(0x20000000u, 0));
	puts("SM8150 clock status: on, hw-gated, halted and NOC FSM: PASS");
	assert(!cepheus_gpii_allowed(0) && cepheus_gpii_allowed(1));
	assert(!cepheus_gpii_allowed(2) && cepheus_gpii_allowed(3));
	assert(cepheus_gpii_allowed(7) && !cepheus_gpii_allowed(8));
	assert(!cepheus_gpii_allowed(13) && !cepheus_gpii_allowed(32));
	assert(cepheus_gpi_channel_context(1, 0) == 0x24000u);
	assert(cepheus_gpi_channel_context(7, 1) == 0x3c080u);
	assert(cepheus_gpi_event_context(7) == 0x3d000u);
	assert(cepheus_gpi_offset_valid(0x5fffcu));
	assert(!cepheus_gpi_offset_valid(0x60000u) && !cepheus_gpi_offset_valid(3));
	puts("GPI2 allowed instance mask and read-only context offsets: PASS");
	assert(cepheus_smr_matches(0x800007b6u, 0x7b6));
	assert(!cepheus_smr_matches(0x000007b6u, 0x7b6));
	assert(!cepheus_smr_matches(0x800007b7u, 0x7b6));
	assert(cepheus_smr_matches(0x800107b7u, 0x7b6));
	assert(cepheus_smr_matches(0x87ff0000u, 0x7b6));
	puts("SMMU stream exact/masked matches and invalid entries: PASS");
	return 0;
}
