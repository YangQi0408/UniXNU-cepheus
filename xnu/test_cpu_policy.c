#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_cpu_policy.h"
int main(void)
{
	int mc, hot = 0;
	assert(ceph_tsens_mc(0x2001f4, &mc) && mc == 50000);
	assert(ceph_tsens_mc(0x200fff, &mc) && mc == -100);
	assert(!ceph_tsens_mc(0x1f4, &mc));
	assert(!ceph_tsens_mc(0xffffffffu, &mc));
	assert(!ceph_tsens_mc(0x2007ff, &mc));
	assert(ceph_cpu_should_raise(1, 50000, &hot, 4000000000ULL, 3000000000ULL, 100000000));
	assert(!ceph_cpu_should_raise(1, 65000, &hot, 4000000000ULL, 3000000000ULL, 0) && hot);
	assert(!ceph_cpu_should_raise(1, 60000, &hot, 4000000000ULL, 3000000000ULL, 0));
	assert(ceph_cpu_should_raise(1, 54900, &hot, 4000000000ULL, 3000000000ULL, 0) && !hot);
	assert(!ceph_cpu_should_raise(0, 0, &hot, 4000000000ULL, 3000000000ULL, 0));
	assert(!ceph_cpu_should_raise(1, 40000, &hot, 7000000000ULL, 3000000000ULL, 0));
	assert(!ceph_cpu_should_raise(1, 40000, &hot, 4000000000ULL, 3000000000ULL, 1000000001ULL));
	assert(!ceph_cpu_should_raise(1, 40000, &hot, 4000000000ULL, 0, 0));
	puts("CPU thermal validity, hysteresis, activity lease and stale-time policy: PASS");
}
