/* Read-only TSENS24xx decode plus conservative interaction policy.
 * CPU TSENS0 sensors 7..14 per cepheus live FDT. No threshold/IRQ writes.
 */
#ifndef CEPHEUS_CPU_POLICY_H
#define CEPHEUS_CPU_POLICY_H
static int ceph_tsens_mc(uint32_t status, int *mc)
{
	if (!mc || status == 0xffffffffu || !(status & (1u << 21))) { return 0; }
	int code = (int)(status & 0xfffu);
	if (code & 0x800) { code -= 4096; }
	*mc = code * 100;
	return *mc >= -40000 && *mc <= 125000;
}
/* Thermal hysteresis: disengage at 65C, permit re-entry below 55C.
 * Policy invalid/stale sensor is a fail-closed result, never 'cold'. */
static int ceph_cpu_should_raise(int valid, int mc, int *hot,
    uint64_t now, uint64_t last_activity, uint64_t sample_age_ns)
{
	if (!hot) { return 0; }
	if (!valid || mc >= 65000) { *hot = 1; return 0; }
	if (*hot && mc < 55000) { *hot = 0; }
	if (*hot || sample_age_ns > 1000000000ULL || !last_activity || now < last_activity) { return 0; }
	return now - last_activity < 3000000000ULL;
}
#endif
