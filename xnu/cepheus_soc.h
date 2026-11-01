/* SM8150 OSM fields per clk-cpu-osm.c; no MMIO in these pure decoders. */
#ifndef CEPHEUS_SOC_H
#define CEPHEUS_SOC_H
#define CEPHEUS_OSM_ROWS 40u
static uint32_t cepheus_osm_khz(uint32_t row)
{
	return (row >> 30) ? 19200u * (row & 255u) : 300000u;
}
static unsigned cepheus_osm_core_count(uint32_t row) { return (row >> 16) & 7u; }
static uint32_t cepheus_counter_delta(uint32_t before, uint32_t after) { return after - before; }
/* Select only same open-loop mV as current LUT row, not boost/single-core
 * entries, capped at 1.4016GHz. Returns current on invalid/nonascending LUT.
 * Virtual corner may change; OSM owns the regulator/limit policy. This is
 * not proof of actual rail voltage; no software voltage register is written.
 */
static unsigned cepheus_osm_same_voltage(const uint32_t *freq, const uint32_t *volt,
    unsigned count, unsigned current)
{
	if (current >= count || count > CEPHEUS_OSM_ROWS || !count) { return current; }
	unsigned best = current;
	uint32_t previous = 0;
	for (unsigned i = 0; i < count; ++i) {
		uint32_t khz = cepheus_osm_khz(freq[i]);
		if (freq[i] == 0xffffffffu || volt[i] == 0xffffffffu || (i && khz < previous)) { return current; }
		if (i && khz == previous) { break; }
		previous = khz;
		/* low12=mV, bits21:16=virtual corner. */
		if ((volt[i] & 0xfffu) != 0 && (volt[i] & 0xfffu) == (volt[current] & 0xfffu) &&
		    cepheus_osm_core_count(freq[i]) != 1 && khz <= 1401600u &&
		    khz > cepheus_osm_khz(freq[best])) { best = i; }
	}
	return best;
}
static int cepheus_gpu_clock_on(uint32_t cbcr, int hwcg)
{
	if (cbcr == 0xffffffffu) { return 0; }
	return !(cbcr & (1u << 31)) || (hwcg && (cbcr & 2u));
}
#endif
