#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CEPHEUS_CP_EXECUTION_ADMISSION_TEST 1
#include "cepheus_cp_execution_admission.h"

static struct ceph_cp_publication cp;
static struct ceph_gx_smmu_plan smmu;
static struct ceph_cp_ring_plan ring;
static struct ceph_cp_ring_native_result result={7,1};
static struct ceph_zap_claim zap;
static struct ceph_cp_execution_admission out;

static void setup(void)
{
    struct ceph_smmu_geometry g;
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&g));
    struct ceph_smmu_stream streams[16]={0};
    for(unsigned n=0;n<16;++n)streams[n].s2cr=2u<<16;
    assert(!ceph_gx_smmu_plan(streams,16,&g,0,1,2,0x110000000ULL,0,&smmu));
    const uint64_t va[]={0xfc000000,0xfc010000,0xfc020000,0xfc030000,0x100000000ULL};
    const size_t bytes[]={32768,4096,32768,4096,4096};
    memset(&cp,0,sizeof(cp));
    cp.memory=(struct ceph_gpu_publication){.group=0x101,.ttbr0=smmu.context.ttbr0,
        .ttbr1=smmu.context.ttbr1,.smr=smmu.context.smr,.cb=smmu.context.cb,
        .slot=smmu.context.slot,.asid=smmu.context.asid};
    for(unsigned n=0;n<CE_CP_BUFFERS;++n)cp.memory.handles[n]=0x100+n;
    cp.table_used=32;
    for(unsigned n=0;n<CE_CP_ROOT;++n)cp.regions[n]=(struct ceph_vm_region){va[n],0x20000000+n*0x100000,
        bytes[n],ceph_cp_workspace_permissions(n)};
    const struct ceph_cp_ring_input in={va[CE_CP_RING],va[CE_CP_RPTR],va[CE_CP_SQE],
        bytes[CE_CP_RING],bytes[CE_CP_RPTR],bytes[CE_CP_SQE]};
    assert(!ceph_cp_ring_plan(&in,&ring));
    zap=(struct ceph_zap_claim){.handle=0x4000000000000010,
        .memory={.mdt=(const volatile uint8_t *)0x100000,.image=(const uint8_t *)0x200000,
        .mdt_pa=0x180000000,.image_pa=0x181000000,.mdt_capacity=16384,
        .image_capacity=1048576,.mdt_bytes=6860,.logical_bytes=4096,.entry_offset=0}};
}

static int plan(void){return ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,
    0x640001,&zap,1,0x6400000000000001ULL,&out);}

int main(void)
{
    setup();out=(struct ceph_cp_execution_admission){0};
    assert(!plan() && out.version==1 && out.ring_writes==7 && out.zap_epoch==0x640001 &&
        out.admission_token==0x6400000000000001ULL && out.rpmh_token==1 &&
        !memcmp(&out.cp,&cp,sizeof(cp)) && !memcmp(&out.smmu,&smmu,sizeof(smmu)) &&
        !memcmp(&out.ring,&ring,sizeof(ring)) && out.zap.handle==zap.handle);
    struct ceph_cp_execution_admission saved=out;
    result.configured=0;assert(plan()<0);result.configured=1;
    result.writes=6;assert(plan()<0);result.writes=7;
    cp.regions[CE_CP_RING].bytes=16384;assert(plan()<0);cp.regions[CE_CP_RING].bytes=32768;
    cp.memory.handles[1]=cp.memory.handles[0];assert(plan()<0);cp.memory.handles[1]=0x101;
    cp.memory.asid++;assert(plan()<0);cp.memory.asid--;
    cp.regions[CE_CP_RPTR].permissions=0;assert(plan()<0);
    cp.regions[CE_CP_RPTR].permissions=ceph_cp_workspace_permissions(CE_CP_RPTR);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,0,&zap,1,1,&out)<0);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,1,&zap,0,1,&out)<0);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,1,&zap,1,0,&out)<0);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,1,NULL,1,1,&out)<0);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,1,&zap,1,1,
        (struct ceph_cp_execution_admission *)&cp)<0);
    assert(ceph_cp_execution_admission_plan(&cp,&smmu,&ring,&result,1,&zap,1,1,
        (struct ceph_cp_execution_admission *)((uint8_t *)&zap+4))<0);
    assert(!plan() && !memcmp(&saved,&out,sizeof(out)));
    puts("CP execution admission identity: exact ring/CP/SMMU/ZAP epoch, output aliases and invalid records PASS");
}
