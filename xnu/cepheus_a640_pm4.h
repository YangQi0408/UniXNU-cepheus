/* Minimal A6xx PM4 builder for a future owned-scratch write test.
 * Packet fields from msm/adreno_pm4types.h; no MMIO/DMA in this module.
 * This is NOT a GPU driver or Metal implementation. GPU VM, GMU/SQE and
 * completion fences must exist before this buffer may be submitted.
 */
#ifndef CEPHEUS_A640_PM4_H
#define CEPHEUS_A640_PM4_H
static unsigned ceph_pm4_odd(uint32_t value)
{
	unsigned parity = 1;
	while (value) { parity ^= value & 1u; value >>= 1; }
	return parity;
}
static uint32_t ceph_pm4_type7(unsigned opcode, unsigned count)
{
	return 0x70000000u | count | (ceph_pm4_odd(count) << 15) |
	    (opcode << 16) | (ceph_pm4_odd(opcode) << 23);
}
static int ceph_pm4_valid_type7(uint32_t header)
{
	unsigned opcode = (header >> 16) & 0x7fu, count = header & 0x3fffu;
	return (header & 0xff004000u) == 0x70000000u &&
	    ((header >> 15) & 1u) == ceph_pm4_odd(count) &&
	    ((header >> 23) & 1u) == ceph_pm4_odd(opcode);
}
/* Exactly one CP_MEM_WRITE (3 payload words) and WAIT_FOR_IDLE. Restrict
 * destination to a caller-owned, already-GPU-mapped scratch allocation.
 * Passing CPU PA as gpu_address without an established mapping is invalid.
 */
static int ceph_a640_write_test(uint32_t *out, unsigned capacity,
    uint64_t scratch_base, uint64_t scratch_bytes, uint64_t gpu_address, uint32_t value)
{
	if (!out || capacity < 5 || (gpu_address & 3u) || scratch_bytes < 4 ||
	    scratch_base > UINT64_MAX - scratch_bytes || gpu_address < scratch_base ||
	    gpu_address - scratch_base > scratch_bytes - 4) { return -1; }
	out[0] = ceph_pm4_type7(0x3d, 3);
	out[1] = (uint32_t)gpu_address;
	out[2] = (uint32_t)(gpu_address >> 32);
	out[3] = value;
	out[4] = ceph_pm4_type7(0x26, 0);
	return 5;
}
#endif
