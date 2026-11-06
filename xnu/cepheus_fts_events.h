/* ST-FTS event state; protocol layout from Xiaomi fts_521/fts.c.
 * Hardware-independent so exact enter/motion/leave behavior is testable.
 * Caller supplies uint8_t/uint32_t and never shares this state across threads.
 */
#ifndef CEPHEUS_FTS_EVENTS_H
#define CEPHEUS_FTS_EVENTS_H
struct cepheus_fts_state {
	uint32_t active;
	int x[10], y[10];
	unsigned skip_next; /* Xiaomi 53:02:18 continuation, persists across reads */
};

static int
cepheus_fts_event(struct cepheus_fts_state *state, const uint8_t e[8])
{
	unsigned type = e[0];
	unsigned finger = e[1] >> 4;
	/* The next FIFO slot after this vendor user report is payload, not an
	 * independent event (fts_event_handler's static pre_id in fts_521/fts.c).
	 * It may resemble enter/motion/reset; consume it before dispatch. */
	if (state->skip_next) {
		state->skip_next = 0;
		return 0;
	}
	if (type == 0x53 && e[1] == 0x02 && e[2] == 0x18) {
		state->skip_next = 1;
		return 0;
	}
	if (type == 0x03 || type == 0xf3) {
		state->active = 0; /* controller reset/error: release stale contacts */
		return 0;
	}
	if (type != 0x13 && type != 0x23 && type != 0x33) { return 0; }
	if (finger >= 10) { return -1; }
	if (type == 0x33) {
		state->active &= ~(1u << finger);
		return 0;
	}
	unsigned touch_type = e[1] & 0x0f;
	if (touch_type != 1 && touch_type != 2 && touch_type != 4) { return 0; }
	unsigned x = ((unsigned)(e[3] & 0x0f) << 8) | e[2];
	unsigned y = ((unsigned)e[4] << 4) | (e[3] >> 4);
	state->x[finger] = x > 1080 ? 1080 : (int)x;
	state->y[finger] = y > 2340 ? 2340 : (int)y;
	state->active |= 1u << finger;
	return 0;
}
#endif
