#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_owner_evidence.h"

static struct ceph_owner_evidence_snapshot snapshot;
static struct ceph_owner_evidence_expectation expectation;
static struct ceph_owner_evidence_gate gate;
static struct ceph_owner_evidence_snapshot output;

static void make_fixture(void)
{
    memset(&snapshot,0,sizeof(snapshot));
    memset(&expectation,0,sizeof(expectation));
    memset(&gate,0,sizeof(gate));
    memset(&output,0xa5,sizeof(output));
    snapshot.magic=CE_OWNER_EVIDENCE_MAGIC;
    snapshot.version=CE_OWNER_EVIDENCE_VERSION;
    snapshot.bytes=sizeof(snapshot);
    snapshot.complete=1;
    snapshot.owner_domain=CE_OD_XNU;
    snapshot.writer_mask=CE_OW_ALL;
    snapshot.ack_state=CE_OA_COMPLETE;
    snapshot.ack_count=2;
    snapshot.retention=CE_ORT_BOOT;
    snapshot.owner_id=0x640001;
    snapshot.lease_id=0x640002;
    snapshot.resource_epoch=0x640003;
    snapshot.captured_ticks=1000;
    snapshot.ack_epoch=snapshot.resource_epoch;
    snapshot.ack_owner_id=snapshot.owner_id;
    snapshot.sid_mask[0]=CE_OWNER_TBU0_LOCAL_SID_MASK;
    snapshot.sid_mask[1]=CE_OWNER_TBU1_LOCAL_SID_MASK;
    snapshot.tbu[0]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU0_BASE,
        CE_OWNER_TBU_BYTES,0,0x400,CE_OTR_OWNER_CONFIRMED,0,snapshot.lease_id,
        CE_OWNER_TBU0_STATUS_REG,1,0};
    snapshot.tbu[1]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU1_BASE,
        CE_OWNER_TBU_BYTES,0x400,0x400,CE_OTR_OWNER_CONFIRMED,0,snapshot.lease_id,
        CE_OWNER_TBU1_STATUS_REG,1,0};
    snapshot.rpmh=(struct ceph_owner_rpmh_evidence){CE_ORS_INHERITED_UEFI,
        CE_OR_COMPLETE,1,0,snapshot.owner_id,0x640004,snapshot.resource_epoch,9,0x640005};
    ceph_owner_evidence_seal(&snapshot);

    expectation.magic=CE_OWNER_EXPECTATION_MAGIC;
    expectation.version=CE_OWNER_EVIDENCE_VERSION;
    expectation.bytes=sizeof(expectation);
    expectation.owner_domain=CE_OD_XNU;
    expectation.writer_mask=CE_OW_ALL;
    expectation.rpmh_source=CE_ORS_INHERITED_UEFI;
    expectation.ack_count=2;
    expectation.owner_id=snapshot.owner_id;
    expectation.lease_id=snapshot.lease_id;
    expectation.resource_epoch=snapshot.resource_epoch;
    expectation.now_ticks=1100;
    expectation.max_age_ticks=100;
    expectation.rpmh_ledger_id=snapshot.rpmh.ledger_id;
    expectation.rpmh_request_count=snapshot.rpmh.request_count;
    expectation.rpmh_checksum=snapshot.rpmh.checksum;
}

static int admit(void)
{
    return ceph_owner_evidence_admit(&gate,&snapshot,&expectation,&output);
}

static void reseal(void)
{
    ceph_owner_evidence_seal(&snapshot);
}

static void expect_refused(void)
{
    struct ceph_owner_evidence_snapshot before=output;
    assert(admit()<0);
    assert(gate.state==CE_OEG_REFUSED && !gate.retained);
    assert(!memcmp(&before,&output,sizeof(output)));
}

static void expect_uncertain(void)
{
    struct ceph_owner_evidence_snapshot before=output;
    assert(admit()<0);
    assert(gate.state==CE_OEG_UNCERTAIN && gate.retained==1);
    assert(!memcmp(&before,&output,sizeof(output)));
    assert(admit()<0);
    assert(gate.state==CE_OEG_UNCERTAIN && gate.retained==1);
}

int main(void)
{
    make_fixture();
    assert(_Alignof(struct ceph_owner_evidence_gate)==8);
    assert(!admit());
    assert(gate.state==CE_OEG_ACCEPTED && gate.retained==1);
    assert(!memcmp(&gate.accepted,&snapshot,sizeof(snapshot)));
    assert(!memcmp(&output,&snapshot,sizeof(snapshot)));
    assert(admit()<0 && gate.state==CE_OEG_ACCEPTED);

    make_fixture();
    snapshot.tbu[0]=(struct ceph_owner_tbu_evidence){0};
    reseal();expect_refused();
    make_fixture();
    snapshot.tbu[1]=(struct ceph_owner_tbu_evidence){0};
    reseal();expect_refused();

    make_fixture();
    snapshot.tbu[1].sid_start=0x3ff;
    reseal();expect_refused();
    make_fixture();
    snapshot.tbu[0].sid_count=0x401;
    reseal();expect_refused();
    make_fixture();
    snapshot.sid_mask[1]|=UINT64_C(1)<<2;
    reseal();expect_refused();

    make_fixture();
    snapshot.tbu[0].readiness_source=CE_OTR_ECATS_DEBUG;
    reseal();expect_refused();
    make_fixture();
    snapshot.tbu[1].readiness_token=0;
    reseal();expect_refused();

    make_fixture();
    snapshot.rpmh.source=CE_ORS_NATIVE_XNU;
    reseal();expect_refused();
    make_fixture();
    snapshot.rpmh.owner_id++;
    reseal();expect_refused();
    make_fixture();
    snapshot.rpmh.state=CE_OR_FOREIGN;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.rpmh.state=CE_OR_STALE;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.rpmh.state=CE_OR_SIMULATED;
    reseal();expect_uncertain();

    make_fixture();
    snapshot.ack_state=CE_OA_UNCERTAIN;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.ack_state=CE_OA_SIMULATED;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.simulated=1;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.complete=0;
    reseal();expect_uncertain();
    make_fixture();
    snapshot.retention=CE_ORT_UNCERTAIN;
    reseal();expect_uncertain();

    make_fixture();
    snapshot.captured_ticks=0;
    reseal();expect_uncertain();
    make_fixture();
    expectation.now_ticks=999;
    expect_refused();
    make_fixture();
    expectation.now_ticks=1101;
    expect_refused();
    make_fixture();
    expectation.now_ticks=UINT64_MAX;
    expectation.max_age_ticks=UINT64_MAX;
    expect_refused();

    make_fixture();
    snapshot.checksum^=1;
    expect_uncertain();
    make_fixture();
    snapshot.rpmh.checksum^=1;
    reseal();expect_refused();
    make_fixture();
    snapshot.rpmh.complete=0;
    reseal();expect_refused();

    make_fixture();
    snapshot.bytes--;
    reseal();expect_refused();
    make_fixture();
    expectation.bytes--;
    expect_refused();
    make_fixture();
    expectation.owner_id++;
    expect_refused();

    make_fixture();
    assert(ceph_owner_evidence_admit(NULL,&snapshot,&expectation,&output)<0);
    assert(ceph_owner_evidence_admit(&gate,NULL,&expectation,&output)<0);
    assert(ceph_owner_evidence_admit(&gate,&snapshot,NULL,&output)<0);
    assert(ceph_owner_evidence_admit(&gate,&snapshot,&expectation,NULL)<0);
    assert(ceph_owner_evidence_admit(&gate,&snapshot,&expectation,
        (struct ceph_owner_evidence_snapshot *)&gate)<0);
    assert(ceph_owner_evidence_admit(&gate,&snapshot,&expectation,
        (struct ceph_owner_evidence_snapshot *)((uint8_t *)&snapshot+4))<0);
    assert(gate.state==CE_OEG_NEW);
    assert(!admit());

    make_fixture();
    gate.version=CE_OWNER_EVIDENCE_VERSION+1;
    expect_refused();

    make_fixture();
    assert(!admit());
    snapshot.owner_id++;
    reseal();
    assert(!memcmp(&gate.accepted,&output,sizeof(output)));
    assert(memcmp(&snapshot,&output,sizeof(output))!=0);

    puts("owner evidence gate: exact dual-TBU/RPMh/ACK identity, copied inputs, aliases and permanent uncertainty retention PASS");
    return 0;
}
