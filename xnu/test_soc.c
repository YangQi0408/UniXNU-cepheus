#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_soc.h"
int main(void)
{
	assert(cepheus_osm_khz(0) == 300000);
	assert(cepheus_osm_khz(0x4004005d) == 1785600);
	assert(cepheus_osm_khz(0x40010094) == 2841600);
	assert(cepheus_osm_core_count(0x4004005d) == 4);
	assert(cepheus_osm_core_count(0x40010094) == 1);
	assert(cepheus_counter_delta(0xfffffff0u, 0x10u) == 32);
	assert(cepheus_gpu_clock_on(0x80000002u, 1));
	assert(!cepheus_gpu_clock_on(0x80000002u, 0));
	assert(!cepheus_gpu_clock_on(0xffffffffu, 1));
	uint32_t freq[] = {0x40040025, 0x4004012b, 0x40040231, 0x40040337, 0x4004043d, 0x40040543, 0x40040649, 0x4004074e};
	uint32_t volt[] = {0x10002f4, 0x10102f4, 0x10202f4, 0x20302f4, 0x20402f4, 0x20502f4, 0x20602f4, 0x20702fc};
	/* Real measured Gold rows: corner changes, open-loop mV remains 756. */
	assert(cepheus_osm_same_voltage(freq, volt, 8, 0) == 6);
	freq[6] = 0x40010649; // single-core boost
	assert(cepheus_osm_same_voltage(freq, volt, 8, 0) == 5);
	freq[3] = 0xffffffffu;
	assert(cepheus_osm_same_voltage(freq, volt, 8, 0) == 0);
	assert(cepheus_osm_same_voltage(freq, volt, 8, 8) == 8);
	puts("OSM LUT, counter wrap and GPU access gates: PASS");
}
