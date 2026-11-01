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
	puts("OSM LUT, counter wrap and GPU access gates: PASS");
}
