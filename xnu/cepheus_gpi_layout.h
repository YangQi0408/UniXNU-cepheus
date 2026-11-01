/* GPI2 register layout, from SM8150 msm_gpi_mmio.h and live FDT.
 * Pure offsets/ownership checks only; this header issues no MMIO or DMA.
 */
#ifndef CEPHEUS_GPI_LAYOUT_H
#define CEPHEUS_GPI_LAYOUT_H
#define CEPHEUS_GPI2_PA 0x00c00000u
#define CEPHEUS_GPI2_SIZE 0x00060000u
#define CEPHEUS_GPII_MASK 0xfau
#define CEPHEUS_GPII_MAX 13u
#define CEPHEUS_SMMU_PA 0x15000000u
#define CEPHEUS_GPI_SID 0x07b6u

/* Legacy 15-bit stream match; mask bits are don't-care. Reject unsupported
 * extended-ID modes in the caller rather than assuming VALID location. */
static int
cepheus_smr_matches(uint32_t smr, unsigned sid)
{
	unsigned mask = (smr >> 16) & 0x7fffu;
	return (smr & 0x80000000u) && !((sid ^ smr) & ~mask & 0x7fffu);
}

static int
cepheus_gpii_allowed(unsigned instance)
{
	return instance < CEPHEUS_GPII_MAX && ((CEPHEUS_GPII_MASK >> instance) & 1u);
}

static uint32_t
cepheus_gpi_channel_context(unsigned instance, unsigned channel)
{
	return 0x20000u + 0x4000u * instance + 0x80u * channel;
}

static uint32_t
cepheus_gpi_event_context(unsigned instance)
{
	return 0x21000u + 0x4000u * instance;
}

static int
cepheus_gpi_offset_valid(uint32_t offset)
{
	return !(offset & 3u) && offset <= CEPHEUS_GPI2_SIZE - sizeof(uint32_t);
}
#endif
