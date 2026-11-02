#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "cepheus_gpu_vm.h"
static uint64_t tables[64*512];
int main(void)
{
	struct ceph_vm_pool pool={tables,0x110000000ULL,64,0};
	struct ceph_vm_region regions[]={{0x4000,0x120000000ULL,0x3c000,CE_VM_READ|CE_VM_EXEC},
	    {0x44000,0x120040000ULL,0x3c000,CE_VM_READ|CE_VM_WRITE},
	    {0x60000000,0x130000000ULL,0x5000,CE_VM_READ|CE_VM_WRITE}};
	assert(!ceph_vm_build(&pool,regions,3) && pool.used==5);
	uint64_t pa,flags;
	assert(!ceph_vm_lookup(&pool,0x4123,&pa,&flags) && pa==0x120000123ULL);
	assert((flags&(3ULL<<53))==0 && (flags&(3u<<6))==(2u<<6));
	assert(!ceph_vm_lookup(&pool,0x60004000,&pa,&flags) && pa==0x130004000ULL);
	assert((flags&(3ULL<<53))==(3ULL<<53));
	assert(ceph_vm_lookup(&pool,0x80000,&pa,&flags)==-1);
	assert(ceph_vm_build(&pool,regions,3)==-1); // live reuse refused
	pool.used=0;pool.pages=4;tables[0]=0xabc;
	assert(ceph_vm_build(&pool,regions,3)==-2 && tables[0]==0xabc && pool.used==0);
	pool.pages=64;regions[1].va=0x4000;
	assert(ceph_vm_build(&pool,regions,3)==-1);regions[1].va=0x44000;
	regions[1].pa=regions[0].pa;assert(ceph_vm_build(&pool,regions,3)==-1);
	regions[1].pa=0x120040000ULL;regions[0].permissions|=CE_VM_WRITE;
	assert(ceph_vm_build(&pool,regions,3)==-1);
	regions[0].permissions=CE_VM_READ;regions[0].pa=pool.pa;
	assert(ceph_vm_build(&pool,regions,3)==-1);
	assert(!ceph_vm_valid(&(struct ceph_vm_region){0xfffff000,0x12345000,8192,CE_VM_READ}));
	puts("GMU page tables: walk/W^X/aliases/capacity/overflow/preflight: PASS (offline)");
}
