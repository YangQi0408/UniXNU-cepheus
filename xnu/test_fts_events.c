#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_fts_events.h"

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
	puts("FTS enter/motion/leave, retained contacts, limits and reset: PASS");
	return 0;
}
