#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_a640_pm4.h"
int main(void)
{
	uint32_t out[6] = {0,0,0,0,0,0x1234};
	assert(ceph_a640_write_test(out, 5, 0x100001000ULL, 4096, 0x100001020ULL, 0xbeef) == 5);
	assert(out[0] == 0x703d8003 && out[1] == 0x1020 && out[2] == 1);
	assert(out[3] == 0xbeef && out[4] == 0x70268000 && out[5] == 0x1234);
	assert(ceph_pm4_valid_type7(out[0]) && ceph_pm4_valid_type7(out[4]));
	assert(!ceph_pm4_valid_type7(out[0] ^ (1u << 15)));
	assert(!ceph_pm4_valid_type7(out[0] ^ (1u << 23)));
	assert(ceph_a640_write_test(out, 4, 0x1000, 4096, 0x1000, 0) == -1);
	assert(ceph_a640_write_test(NULL, 5, 0x1000, 4096, 0x1000, 0) == -1);
	assert(ceph_a640_write_test(out, 5, 0x1000, 4096, 0x1001, 0) == -1);
	assert(ceph_a640_write_test(out, 5, 0x1000, 4096, 0x2000, 0) == -1);
	assert(ceph_a640_write_test(out, 5, UINT64_MAX - 3, 4096, UINT64_MAX - 3, 0) == -1);
	assert(ceph_a640_write_test(out, 5, 0x1000, 4096, 0x1ffc, 0) == 5);
	puts("A640 PM4 parity, 64-bit address, bounded scratch and capacity: PASS (not GPU execution)");
}
