#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_zap_resource_evidence.h"

static struct ceph_zap_claim claim;
static struct ceph_owner_evidence_gate owner_gate;
static struct ceph_owner_evidence_snapshot owner;
static struct ceph_zap_resource_expectation expected;
static struct ceph_zap_resource_evidence evidence;
static struct ceph_zap_resource_gate gate;
static struct ceph_zap_resource_evidence output;

static void make_fixture(void)
{
    memset(&claim,0,sizeof(claim));memset(&owner_gate,0,sizeof(owner_gate));
    memset(&owner,0,sizeof(owner));memset(&expected,0,sizeof(expected));
    memset(&evidence,0,sizeof(evidence));memset(&gate,0,sizeof(gate));
    memset(&output,0xa5,sizeof(output));
    claim.handle=0x4000000000000010;
    claim.memory.mdt_pa=0x180000000;
    claim.memory.image_pa=0x181000000;
    claim.memory.mdt_capacity=16384;
    claim.memory.image_capacity=1048576;
    claim.memory.mdt_bytes=6860;
    claim.memory.logical_bytes=4096;

    owner.magic=CE_OWNER_EVIDENCE_MAGIC;owner.version=CE_OWNER_EVIDENCE_VERSION;
    owner.bytes=sizeof(owner);owner.complete=1;owner.owner_domain=CE_OD_XNU;
    owner.writer_mask=CE_OW_ALL;owner.ack_state=CE_OA_COMPLETE;owner.ack_count=2;
    owner.retention=CE_ORT_BOOT;owner.owner_id=0x640001;owner.lease_id=0x640002;
    owner.resource_epoch=0x640003;owner.captured_ticks=1000;
    owner.ack_epoch=owner.resource_epoch;owner.ack_owner_id=owner.owner_id;
    owner.sid_mask[0]=CE_OWNER_TBU0_LOCAL_SID_MASK;
    owner.sid_mask[1]=CE_OWNER_TBU1_LOCAL_SID_MASK;
    owner.tbu[0]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU0_BASE,
        CE_OWNER_TBU_BYTES,0,0x400,CE_OTR_OWNER_CONFIRMED,0,owner.lease_id,
        CE_OWNER_TBU0_STATUS_REG,1,0};
    owner.tbu[1]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU1_BASE,
        CE_OWNER_TBU_BYTES,0x400,0x400,CE_OTR_OWNER_CONFIRMED,0,owner.lease_id,
        CE_OWNER_TBU1_STATUS_REG,1,0};
    owner.rpmh=(struct ceph_owner_rpmh_evidence){CE_ORS_INHERITED_UEFI,
        CE_OR_COMPLETE,1,0,owner.owner_id,0x640004,owner.resource_epoch,9,0x640005};
    ceph_owner_evidence_seal(&owner);
    owner_gate=(struct ceph_owner_evidence_gate){.version=CE_OWNER_EVIDENCE_VERSION,
        .state=CE_OEG_ACCEPTED,.retained=1,.accepted=owner};

    expected.magic=CE_ZAP_RESOURCE_EXPECTATION_MAGIC;
    expected.version=CE_ZAP_RESOURCE_VERSION;expected.bytes=sizeof(expected);
    expected.max_age_ticks=100;expected.crypto_ledger_id=0x640006;
    expected.request_count=1;expected.ledger_checksum=0x640007;
    expected.ib_bytes=1600000000;expected.ab_bytes=55250000;

    evidence.magic=CE_ZAP_RESOURCE_MAGIC;evidence.version=CE_ZAP_RESOURCE_VERSION;
    evidence.bytes=sizeof(evidence);evidence.complete=1;
    evidence.owner_evidence_checksum=owner.checksum;evidence.captured_ticks=1050;
    evidence.memory=(struct ceph_zap_resource_memory){claim.handle,claim.memory.mdt_pa,
        claim.memory.image_pa,claim.memory.mdt_capacity,claim.memory.image_capacity,
        claim.memory.mdt_bytes,claim.memory.logical_bytes,1,16384,
        CE_ZRC_DEVICE_NGNRNE,CE_ZRC_NORMAL_NC,CE_ZRC_DEVICE_NGNRNE,1,0,0};
    evidence.crypto=(struct ceph_zap_resource_crypto){CE_ZRS_INHERITED_UEFI,CE_ZR_COMPLETE,
        1,0,CE_ZAP_RESOURCE_BCM_COUNT,0,owner.owner_id,owner.lease_id,owner.resource_epoch,
        CE_ZAP_RESOURCE_CRYPTO_MASTER,CE_ZAP_RESOURCE_CRYPTO_SLAVE,
        {CE_ZAP_RESOURCE_BCM_CE0,CE_ZAP_RESOURCE_BCM_SN11,CE_ZAP_RESOURCE_BCM_SN0,
            CE_ZAP_RESOURCE_BCM_SH0,CE_ZAP_RESOURCE_BCM_MC0,CE_ZAP_RESOURCE_BCM_ACV},
        expected.ib_bytes,expected.ab_bytes,expected.crypto_ledger_id,
        expected.request_count,expected.ledger_checksum,owner.ack_owner_id,
        owner.ack_epoch,owner.ack_count};
    ceph_zap_resource_seal(&evidence);
}

static int admit(void)
{
    return ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,&owner,
        &expected,1100,&output);
}

static void reseal(void){ceph_zap_resource_seal(&evidence);}

static void refused(void)
{
    struct ceph_zap_resource_evidence before=output;
    assert(admit()<0 && gate.state==CE_ZRG_REFUSED && !gate.retained);
    assert(!memcmp(&before,&output,sizeof(output)));
}

static void reject(void){owner_gate.state=CE_OEG_REFUSED;refused();}

static void uncertain(void)
{
    struct ceph_zap_resource_evidence before=output;
    assert(admit()<0 && gate.state==CE_ZRG_UNCERTAIN && gate.retained==1);
    assert(!memcmp(&before,&output,sizeof(output)));
}

int main(void)
{
    make_fixture();
    assert(sizeof(struct ceph_zap_resource_memory)==CE_ZAP_RESOURCE_MEMORY_BYTES);
    assert(sizeof(struct ceph_zap_resource_crypto)==CE_ZAP_RESOURCE_CRYPTO_BYTES);
    assert(sizeof(struct ceph_zap_resource_evidence)==CE_ZAP_RESOURCE_EVIDENCE_BYTES);
    assert(sizeof(struct ceph_zap_resource_expectation)==CE_ZAP_RESOURCE_EXPECTATION_BYTES);
    assert(sizeof(struct ceph_zap_resource_gate)==CE_ZAP_RESOURCE_GATE_BYTES);
    assert(_Alignof(struct ceph_zap_resource_gate)==8);
    assert(!admit());
    assert(gate.state==CE_ZRG_ACCEPTED && gate.retained==1);
    assert(!memcmp(&gate.accepted,&evidence,sizeof(evidence)));
    assert(!memcmp(&output,&evidence,sizeof(evidence)));
    assert(admit()<0 && gate.state==CE_ZRG_ACCEPTED);

    make_fixture();owner_gate.state=CE_OEG_REFUSED;refused();
    make_fixture();owner_gate.state=CE_OEG_UNCERTAIN;uncertain();
    make_fixture();owner_gate.retained=0;refused();
    make_fixture();owner_gate.state=CE_OEG_ACCEPTED;owner_gate.retained=0;refused();
    make_fixture();owner.ack_state=CE_OA_UNCERTAIN;ceph_owner_evidence_seal(&owner);
    owner_gate.accepted=owner;evidence.owner_evidence_checksum=owner.checksum;reseal();uncertain();
    make_fixture();owner.simulated=1;ceph_owner_evidence_seal(&owner);
    owner_gate.accepted=owner;evidence.owner_evidence_checksum=owner.checksum;reseal();uncertain();
    make_fixture();owner.tbu[1].base++;owner_gate.state=CE_OEG_REFUSED;ceph_owner_evidence_seal(&owner);
    owner_gate.accepted=owner;evidence.owner_evidence_checksum=owner.checksum;reseal();refused();
    make_fixture();owner.ack_count=3;owner_gate.state=CE_OEG_REFUSED;ceph_owner_evidence_seal(&owner);
    owner_gate.accepted=owner;evidence.owner_evidence_checksum=owner.checksum;
    evidence.crypto.ack_count=3;reseal();refused();

    make_fixture();evidence.checksum^=1;uncertain();
    make_fixture();evidence.simulated=1;reseal();uncertain();
    make_fixture();evidence.complete=0;reseal();uncertain();
    make_fixture();evidence.crypto.state=CE_ZR_FOREIGN;reseal();uncertain();
    make_fixture();evidence.crypto.state=CE_ZR_SIMULATED;reseal();uncertain();
    make_fixture();evidence.crypto.complete=0;reseal();uncertain();
    make_fixture();evidence.memory.complete=0;reseal();uncertain();
    make_fixture();evidence.memory.simulated=1;reseal();uncertain();

    make_fixture();evidence.memory.handle++;reseal();reject();
    make_fixture();evidence.memory.mdt_pa+=16384;reseal();reject();
    make_fixture();evidence.memory.image_pa=evidence.memory.mdt_pa;reseal();reject();
    make_fixture();evidence.memory.mdt_capacity=8192;reseal();reject();
    make_fixture();evidence.memory.image_capacity=524288;reseal();reject();
    make_fixture();evidence.memory.mdt_bytes=4096;reseal();reject();
    make_fixture();evidence.memory.logical_bytes=1048576;reseal();reject();
    make_fixture();claim.memory.entry_offset=1;reject();
    make_fixture();evidence.memory.secure_pa_reachable=0;reseal();reject();
    make_fixture();evidence.memory.xpu_granule=8192;reseal();reject();
    make_fixture();evidence.memory.xpu_granule=4096;reseal();assert(!admit());
    make_fixture();evidence.memory.image_pa+=4096;reseal();reject();
    make_fixture();evidence.memory.cpu_mdt_cache=CE_ZRC_NORMAL_NC;reseal();reject();
    make_fixture();evidence.memory.cpu_image_cache=CE_ZRC_DEVICE_NGNRNE;reseal();reject();
    make_fixture();evidence.memory.secure_cache=CE_ZRC_NORMAL_NC;reseal();reject();
    make_fixture();evidence.memory.mdt_pa=(1ULL<<40)-16384;reseal();reject();
    make_fixture();evidence.memory.image_pa=(1ULL<<40)-1048576;reseal();reject();

    make_fixture();evidence.crypto.owner_id++;reseal();reject();
    make_fixture();evidence.crypto.lease_id++;reseal();reject();
    make_fixture();evidence.crypto.resource_epoch++;reseal();reject();
    make_fixture();evidence.crypto.source=CE_ZRS_NONE;reseal();reject();
    make_fixture();evidence.crypto.state=CE_ZR_NONE;reseal();reject();
    make_fixture();evidence.crypto.master++;reseal();reject();
    make_fixture();evidence.crypto.slave++;reseal();reject();
    make_fixture();evidence.crypto.bcm[0]++;reseal();reject();
    make_fixture();evidence.crypto.bcm[5]++;reseal();reject();
    make_fixture();evidence.crypto.bcm[1]=evidence.crypto.bcm[0];reseal();reject();
    make_fixture();evidence.crypto.bcm_count--;reseal();reject();
    make_fixture();evidence.crypto.ib_bytes--;reseal();reject();
    make_fixture();evidence.crypto.ab_bytes--;reseal();reject();
    make_fixture();evidence.crypto.ledger_id++;reseal();reject();
    make_fixture();evidence.crypto.request_count++;reseal();reject();
    make_fixture();evidence.crypto.ledger_checksum++;reseal();reject();
    make_fixture();evidence.crypto.ack_owner_id++;reseal();reject();
    make_fixture();evidence.crypto.ack_epoch++;reseal();reject();
    make_fixture();evidence.crypto.ack_count++;reseal();reject();

    make_fixture();evidence.captured_ticks=999;reseal();reject();
    make_fixture();evidence.captured_ticks=1101;reseal();reject();
    make_fixture();evidence.captured_ticks=1001;reseal();reject();
    make_fixture();evidence.captured_ticks=1050;expected.max_age_ticks=101;assert(!admit());
    make_fixture();evidence.captured_ticks=1050;expected.max_age_ticks=50;reject();
    make_fixture();expected.max_age_ticks=UINT64_MAX;reject();
    make_fixture();expected.max_age_ticks=0;reject();
    make_fixture();expected.crypto_ledger_id=0;reject();
    make_fixture();expected.request_count=0;reject();
    make_fixture();expected.ledger_checksum=0;reject();
    make_fixture();expected.ib_bytes=0;reject();
    make_fixture();expected.ab_bytes=0;reject();
    make_fixture();expected.bytes--;reject();
    make_fixture();evidence.bytes--;reseal();reject();
    make_fixture();evidence.owner_evidence_checksum^=1;reseal();reject();

    make_fixture();
    assert(ceph_zap_resource_admit(NULL,&claim,&evidence,&owner_gate,&owner,&expected,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,NULL,&evidence,&owner_gate,&owner,&expected,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,NULL,&owner_gate,&owner,&expected,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,NULL,&owner,&expected,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,NULL,&expected,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,&owner,NULL,1100,&output)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,&owner,&expected,1100,NULL)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,&owner,&expected,1100,
        (struct ceph_zap_resource_evidence *)&gate)<0);
    assert(ceph_zap_resource_admit(&gate,&claim,&evidence,&owner_gate,&owner,&expected,1100,
        (struct ceph_zap_resource_evidence *)((uint8_t *)&evidence+4))<0);
    assert(gate.state==CE_ZRG_NEW && !admit());

    puts("ZAP resource evidence: secure PA/XPU/cache, crypto route/BCM/ledger/ACK, owner gate, aliases, age and permanent uncertainty retention PASS");
    return 0;
}
