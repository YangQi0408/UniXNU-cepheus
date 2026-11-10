#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_rpmh_pool.h"

static uint8_t ram[32768];
static struct ceph_rpmh_snapshot snapshot;
static struct ceph_rpmh_snapshot_scratch scratch;
static unsigned reads, mutate, fail_read;
static uint64_t mutate_address;
static void w32(unsigned p,uint32_t v) { memcpy(ram+p,&v,4); }
static void w64(unsigned p,uint64_t v) { memcpy(ram+p,&v,8); }
static int read_ram(void *c,uint64_t p,void *out,uint32_t n)
{
    (void)c;
    ++reads;
    if(reads==fail_read || p>sizeof(ram) || n>sizeof(ram)-p)return -1;
    if(reads==mutate)ram[mutate_address]^=1;
    memcpy(out,ram+p,n);return 0;
}
static int pool(void *c,uint64_t p,uint32_t n)
{ struct ceph_rpmh_snapshot_ops o={c,read_ram,NULL};return ceph_rpmh_pool_check(&o,p,n); }
static void allocation(unsigned p,unsigned n)
{
    unsigned total=((n+11u)&~7u)+40u;
    w32(p-28,0x30646870);w32(p-20,4);w64(p-12,total);
    w32(p-4,n);w32(p-28+total-16,0x6c617470);w64(p-28+total-8,total);
}
static void fixture(void)
{
    memset(ram,0,sizeof(ram));memset(&snapshot,0,sizeof(snapshot));memset(&scratch,0,sizeof(scratch));
    reads=mutate=fail_read=0;mutate_address=0;
    w32(64,2);w64(68,1028);allocation(1028,48);
    w32(1028,0x30000);w64(1036,2052);
    w32(1052,0x30004);w64(1060,3076);
    allocation(2052,72);allocation(3076,72);
    w32(2052,0);w32(2064,5);ram[2068]=1;
    w32(2076,5);ram[2080]=1;
    w32(3076,0);w32(3088,6);ram[3092]=1;
    w32(3100,6);ram[3104]=1;
}
static int capture(void)
{
    struct ceph_rpmh_snapshot_ops o={NULL,read_ram,pool};
    const uint32_t addresses[2]={0x30000,0x30004};
    return ceph_rpmh_snapshot_read(&snapshot,&scratch,&o,64,addresses);
}
static void seal(int error)
{
    snapshot.magic=CE_RPMH_SNAPSHOT_MAGIC;snapshot.version=1;snapshot.bytes=sizeof(snapshot);
    snapshot.complete=1;snapshot.error=error;snapshot.identity_mask=15;
    snapshot.ebs_attempts=1;snapshot.ebs_done=1;snapshot.current_el=4;
    snapshot.phase=1;
    if(error)memset(snapshot.resource,0,sizeof(snapshot.resource));
    snapshot.checksum=ceph_rpmh_snapshot_checksum(&snapshot);
}
int main(void)
{
    fixture();assert(!capture());unsigned baseline_reads=reads;
    assert(snapshot.resource[0].drv[0].local[0].data==5);
    assert(snapshot.resource[1].drv[0].local[0].data==6);
    seal(0);assert(!ceph_rpmh_snapshot_valid(&snapshot));
    struct ceph_rpmh_snapshot good=snapshot;
    for(unsigned i=0;i<sizeof(snapshot);++i) {
        snapshot=good;((uint8_t *)&snapshot)[i]^=1;assert(ceph_rpmh_snapshot_valid(&snapshot));
    }
    fixture();w32(64,513);assert(capture()==CE_RPMH_SNAP_ARRAY);
    fixture();w64(68,UINT64_MAX-4);assert(capture()==CE_RPMH_SNAP_ARRAY);
    fixture();w32(1028,0x30004);assert(capture()==CE_RPMH_SNAP_ARRAY);
    fixture();w32(1052,0x30008);assert(capture()==CE_RPMH_SNAP_MISSING);
    fixture();w32(1000,0x30726670);assert(capture()==CE_RPMH_SNAP_POOL);
    fixture();w32(1024,47);assert(capture()==CE_RPMH_SNAP_POOL);
    fixture();w32(1080,0);assert(capture()==CE_RPMH_SNAP_POOL);
    fixture();w64(2040,0);assert(capture()==CE_RPMH_SNAP_POOL);
    fixture();w64(2116,2052);assert(capture()==CE_RPMH_SNAP_CHAIN);
    fixture();w64(1060,2052);assert(capture()==CE_RPMH_SNAP_CHAIN);
    fixture();w64(1036,0);assert(capture()==CE_RPMH_SNAP_MISSING);
    fixture();w64(1036,60000);assert(capture()==CE_RPMH_SNAP_POOL);
    fixture();ram[2080]=2;assert(capture()==CE_RPMH_SNAP_CHAIN);
    fixture();w32(2084,5);assert(capture()==CE_RPMH_SNAP_CHAIN);
    for(unsigned i=1;i<=baseline_reads;++i) {
        fixture();fail_read=i;assert(capture()!=0);
    }
    unsigned changes=0;
    for(unsigned i=1;i<=baseline_reads;++i) {
        fixture();mutate=i;mutate_address=2076;
        if(capture()==CE_RPMH_SNAP_CHANGED)++changes;
    }
    assert(changes>0);
    fixture();w64(68,8196);allocation(8196,48);memcpy(ram+8196,ram+1028,48);
    w32(1000,0x30726670);assert(!capture()); /* Final pointer, not the old array. */
    fixture();int error=capture();assert(!error);seal(CE_RPMH_SNAP_POOL);
    assert(!ceph_rpmh_snapshot_valid(&snapshot));
    assert(ceph_rpmh_snapshot_profile(NULL)==0);
    assert(ceph_rpmh_snapshot_profile("cephrpmhsnap=1")==1);
    const char *bad[]={"cephrpmhsnap=0","cephrpmhsnap=01","cephrpmhsnap=1 cephscmpair=1",
        "cephrpmhsnap-extra=1","cephrpmhsnap=1 cephrpmhsnap=1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(ceph_rpmh_snapshot_profile(bad[i])==-1);
    printf("RPMh diagnostic: moved array, freed/corrupt pools, bounds, cycles, mutation, %u read failures and ABI checksum PASS; not ACK/ownership\n",baseline_reads);
    return 0;
}
