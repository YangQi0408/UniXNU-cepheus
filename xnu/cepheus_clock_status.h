/* SM8150 clk-branch.c enable predicate. S_AHB can be hardware gated; an
 * idle clock then has CLK_OFF set without an enable failure. */
#ifndef CEPHEUS_CLOCK_STATUS_H
#define CEPHEUS_CLOCK_STATUS_H
static int
cepheus_branch_enabled(uint32_t cbcr, int has_hwcg)
{
	if (has_hwcg && (cbcr & (1u << 1))) { return 1; }
	uint32_t status = cbcr & 0xf0000000u;
	return !(status & 0x80000000u) || status == 0x20000000u;
}
#endif
