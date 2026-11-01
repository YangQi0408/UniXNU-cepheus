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
static int cepheus_gpu_clock_on(uint32_t cbcr, int hwcg)
{
	if (cbcr == 0xffffffffu) { return 0; }
	return !(cbcr & (1u << 31)) || (hwcg && (cbcr & 2u));
}
#endif
