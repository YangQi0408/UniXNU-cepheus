#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_resource_owner_provider.h"

struct host_provider {
    uint64_t marker;
    int acquire_fail;
    int collect_fail;
    int validate_result;
    int validate_calls;
    int retain_calls;
    int collect_mode;
    uint64_t token;
    struct ceph_zap_claim claim;
    struct ceph_owner_evidence_expectation owner_expected;
    struct ceph_zap_resource_expectation resource_expected;
    struct ceph_owner_evidence_snapshot owner;
    struct ceph_zap_resource_evidence resource;
};

static struct host_provider host;
static struct ceph_resource_owner_provider provider;
static struct ceph_resource_owner_request request;
static struct ceph_zap_claim claim;
static struct ceph_owner_evidence_expectation owner_expected;
static struct ceph_zap_resource_expectation resource_expected;
static struct ceph_resource_owner_gate gate;
static struct ceph_resource_owner_result output;
static int retain_calls;

static void make_owner(struct ceph_owner_evidence_snapshot *owner)
{
    memset(owner,0,sizeof(*owner));
    owner->magic=CE_OWNER_EVIDENCE_MAGIC;
    owner->version=CE_OWNER_EVIDENCE_VERSION;
    owner->bytes=sizeof(*owner);
    owner->complete=1;
    owner->owner_domain=CE_OD_XNU;
    owner->writer_mask=CE_OW_ALL;
    owner->ack_state=CE_OA_COMPLETE;
    owner->ack_count=2;
    owner->retention=CE_ORT_BOOT;
    owner->owner_id=0x640001;
    owner->lease_id=0x640002;
    owner->resource_epoch=0x640003;
    owner->captured_ticks=1000;
    owner->ack_epoch=owner->resource_epoch;
    owner->ack_owner_id=owner->owner_id;
    owner->sid_mask[0]=CE_OWNER_TBU0_LOCAL_SID_MASK;
    owner->sid_mask[1]=CE_OWNER_TBU1_LOCAL_SID_MASK;
    owner->tbu[0]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU0_BASE,
        CE_OWNER_TBU_BYTES,0,0x400,CE_OTR_OWNER_CONFIRMED,0,owner->lease_id,
        CE_OWNER_TBU0_STATUS_REG,1,0};
    owner->tbu[1]=(struct ceph_owner_tbu_evidence){CE_OWNER_TBU1_BASE,
        CE_OWNER_TBU_BYTES,0x400,0x400,CE_OTR_OWNER_CONFIRMED,0,owner->lease_id,
        CE_OWNER_TBU1_STATUS_REG,1,0};
    owner->rpmh=(struct ceph_owner_rpmh_evidence){CE_ORS_INHERITED_UEFI,
        CE_OR_COMPLETE,1,0,owner->owner_id,0x640004,owner->resource_epoch,9,0x640005};
    ceph_owner_evidence_seal(owner);
}

static void make_resource(struct ceph_zap_resource_evidence *resource,
    const struct ceph_owner_evidence_snapshot *owner)
{
    const uint64_t bcms[CE_ZAP_RESOURCE_BCM_COUNT]={CE_ZAP_RESOURCE_BCM_CE0,
        CE_ZAP_RESOURCE_BCM_SN11,CE_ZAP_RESOURCE_BCM_SN0,CE_ZAP_RESOURCE_BCM_SH0,
        CE_ZAP_RESOURCE_BCM_MC0,CE_ZAP_RESOURCE_BCM_ACV};
    memset(resource,0,sizeof(*resource));
    resource->magic=CE_ZAP_RESOURCE_MAGIC;
    resource->version=CE_ZAP_RESOURCE_VERSION;
    resource->bytes=sizeof(*resource);
    resource->complete=1;
    resource->owner_evidence_checksum=owner->checksum;
    resource->captured_ticks=1000;
    resource->memory=(struct ceph_zap_resource_memory){claim.handle,
        claim.memory.mdt_pa,claim.memory.image_pa,claim.memory.mdt_capacity,
        claim.memory.image_capacity,claim.memory.mdt_bytes,claim.memory.logical_bytes,
        1,16384,CE_ZRC_DEVICE_NGNRNE,CE_ZRC_NORMAL_NC,CE_ZRC_DEVICE_NGNRNE,1,0,0};
    resource->crypto=(struct ceph_zap_resource_crypto){CE_ZRS_INHERITED_UEFI,
        CE_ZR_COMPLETE,1,0,CE_ZAP_RESOURCE_BCM_COUNT,0,owner->owner_id,owner->lease_id,
        owner->resource_epoch,CE_ZAP_RESOURCE_CRYPTO_MASTER,CE_ZAP_RESOURCE_CRYPTO_SLAVE,
        {bcms[0],bcms[1],bcms[2],bcms[3],bcms[4],bcms[5]},1600000000,55250000,
        0x640006,1,0x640007,owner->ack_owner_id,owner->ack_epoch,owner->ack_count};
    ceph_zap_resource_seal(resource);
}

static void make_fixture(void)
{
    memset(&host,0,sizeof(host));
    memset(&provider,0,sizeof(provider));
    memset(&request,0,sizeof(request));
    memset(&claim,0,sizeof(claim));
    memset(&owner_expected,0,sizeof(owner_expected));
    memset(&resource_expected,0,sizeof(resource_expected));
    memset(&gate,0,sizeof(gate));
    memset(&output,0xa5,sizeof(output));

    host.marker=UINT64_C(0x484f535450524f56);
    host.token=0x640009;
    claim.handle=0x4000000000000010;
    claim.memory.mdt_pa=0x180000000;
    claim.memory.image_pa=0x181000000;
    claim.memory.mdt_capacity=16384;
    claim.memory.image_capacity=1048576;
    claim.memory.mdt_bytes=6860;
    claim.memory.logical_bytes=4096;
    make_owner(&host.owner);
    make_resource(&host.resource,&host.owner);

    owner_expected.magic=CE_OWNER_EXPECTATION_MAGIC;
    owner_expected.version=CE_OWNER_EVIDENCE_VERSION;
    owner_expected.bytes=sizeof(owner_expected);
    owner_expected.owner_domain=CE_OD_XNU;
    owner_expected.writer_mask=CE_OW_ALL;
    owner_expected.rpmh_source=CE_ORS_INHERITED_UEFI;
    owner_expected.ack_count=host.owner.ack_count;
    owner_expected.owner_id=host.owner.owner_id;
    owner_expected.lease_id=host.owner.lease_id;
    owner_expected.resource_epoch=host.owner.resource_epoch;
    owner_expected.now_ticks=1100;
    owner_expected.max_age_ticks=100;
    owner_expected.rpmh_ledger_id=host.owner.rpmh.ledger_id;
    owner_expected.rpmh_request_count=host.owner.rpmh.request_count;
    owner_expected.rpmh_checksum=host.owner.rpmh.checksum;
    resource_expected.magic=CE_ZAP_RESOURCE_EXPECTATION_MAGIC;
    resource_expected.version=CE_ZAP_RESOURCE_VERSION;
    resource_expected.bytes=sizeof(resource_expected);
    resource_expected.max_age_ticks=101;
    resource_expected.crypto_ledger_id=host.resource.crypto.ledger_id;
    resource_expected.request_count=host.resource.crypto.request_count;
    resource_expected.ledger_checksum=host.resource.crypto.ledger_checksum;
    resource_expected.ib_bytes=host.resource.crypto.ib_bytes;
    resource_expected.ab_bytes=host.resource.crypto.ab_bytes;

    request.magic=CE_RESOURCE_OWNER_REQUEST_MAGIC;
    request.version=CE_RESOURCE_OWNER_VERSION;
    request.bytes=sizeof(request);
    request.boot_epoch=0x640008;
    request.now_ticks=owner_expected.now_ticks;

    host.claim=claim;
    host.owner_expected=owner_expected;
    host.resource_expected=resource_expected;
    provider.magic=CE_RESOURCE_OWNER_PROVIDER_MAGIC;
    provider.version=CE_RESOURCE_OWNER_VERSION;
    provider.bytes=sizeof(provider);
    provider.ctx=&host;
    provider.ctx_bytes=sizeof(host);
}

static int acquire_cb(void *ctx,const struct ceph_zap_claim *actual,
    const struct ceph_owner_evidence_expectation *owner_want,
    const struct ceph_zap_resource_expectation *resource_want,uint64_t now,
    struct ceph_resource_owner_lease *lease)
{
    struct host_provider *h=ctx;
    if(!h || h->marker!=UINT64_C(0x484f535450524f56) ||
        memcmp(actual,&h->claim,sizeof(*actual)) || memcmp(owner_want,&h->owner_expected,
        sizeof(*owner_want)) || memcmp(resource_want,&h->resource_expected,
        sizeof(*resource_want)) || now!=owner_want->now_ticks)return -1;
    if(h->acquire_fail)return -100;
    *lease=(struct ceph_resource_owner_lease){
        .magic=CE_RESOURCE_OWNER_LEASE_MAGIC,.version=CE_RESOURCE_OWNER_VERSION,
        .bytes=sizeof(*lease),.state=CE_ROLS_LIVE,.mode=CE_ROM_EXCLUSIVE_REAL,
        .ack_count=h->owner.ack_count,.provider_handle=(uintptr_t)h,
        .provider_id=0x64000a,.token=h->token,.boot_epoch=request.boot_epoch,
        .owner_id=h->owner.owner_id,.lease_id=h->owner.lease_id,
        .resource_epoch=h->owner.resource_epoch,.ack_epoch=h->owner.ack_epoch,
        .rpmh_ledger_id=h->owner.rpmh.ledger_id,
        .rpmh_request_count=h->owner.rpmh.request_count,
        .rpmh_checksum=h->owner.rpmh.checksum,
        .crypto_ledger_id=h->resource.crypto.ledger_id,
        .crypto_request_count=h->resource.crypto.request_count,
        .crypto_ledger_checksum=h->resource.crypto.ledger_checksum};
    ceph_resource_owner_lease_seal(lease);
    return 0;
}

static int collect_cb(void *ctx,const struct ceph_zap_claim *actual,
    const struct ceph_resource_owner_lease *lease,
    struct ceph_owner_evidence_snapshot *owner,
    struct ceph_zap_resource_evidence *resource)
{
    struct host_provider *h=ctx;
    if(!h || memcmp(actual,&h->claim,sizeof(*actual)) || lease->token!=h->token ||
        lease->provider_handle!=(uintptr_t)h)return -1;
    if(h->collect_fail)return -1;
    *owner=h->owner;
    *resource=h->resource;
    if(h->collect_mode==1)owner->simulated=1;
    else if(h->collect_mode==2)resource->crypto.resource_epoch++;
    else if(h->collect_mode==3)owner->checksum^=1;
    else if(h->collect_mode==4)owner->rpmh.state=CE_OR_FOREIGN;
    else if(h->collect_mode==5)resource->owner_evidence_checksum^=1;
    if(h->collect_mode>=1 && h->collect_mode<=5) {
        if(h->collect_mode!=2 && h->collect_mode!=3)ceph_owner_evidence_seal(owner);
        ceph_zap_resource_seal(resource);
    }
    return 0;
}

static int validate_cb(void *ctx,const struct ceph_resource_owner_lease *lease,
    uint64_t now)
{
    struct host_provider *h=ctx;
    ++h->validate_calls;
    if(h->validate_result && h->validate_calls==h->validate_result)return -1;
    return !lease || lease->provider_handle!=(uintptr_t)h || lease->token!=h->token ||
        now!=owner_expected.now_ticks?-1:0;
}

static void retain_cb(void *ctx,const struct ceph_resource_owner_lease *lease)
{
    ++retain_calls;
    assert(lease);
    assert(ctx==(void *)&host || ctx==(uint8_t *)&host+1);
    if(ctx==&host)assert(lease->provider_handle==(uintptr_t)&host ||
        !lease->provider_handle);
}

static void bind_provider(void)
{
    provider.acquire=acquire_cb;
    provider.collect=collect_cb;
    provider.validate=validate_cb;
    provider.retain=retain_cb;
}

static int run(void)
{
    return ceph_resource_owner_acquire(&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&output);
}

static void expect_fail_retained(void)
{
    struct ceph_resource_owner_result before=output;
    int retains=retain_calls;
    assert(run()<0);
    assert(gate.state==CE_ROPG_UNCERTAIN && gate.retained==1);
    assert(retain_calls==retains+1);
    assert(!memcmp(&before,&output,sizeof(output)));
}

static void expect_rejected_before_attempt(void)
{
    struct ceph_resource_owner_result before=output;
    int retains=retain_calls;
    assert(run()<0);
    assert(gate.state==CE_ROPG_NEW && gate.retained==0);
    assert(retain_calls==retains);
    assert(!memcmp(&before,&output,sizeof(output)));
}

int main(void)
{
    make_fixture();bind_provider();
    assert(sizeof(struct ceph_resource_owner_lease)==CE_RESOURCE_OWNER_LEASE_BYTES);
    assert(sizeof(struct ceph_resource_owner_request)==CE_RESOURCE_OWNER_REQUEST_BYTES);
    assert(sizeof(struct ceph_resource_owner_provider)==CE_RESOURCE_OWNER_PROVIDER_BYTES);
    assert(sizeof(struct ceph_resource_owner_result)==CE_RESOURCE_OWNER_RESULT_BYTES);
    assert(sizeof(struct ceph_resource_owner_gate)==CE_RESOURCE_OWNER_GATE_BYTES);
    assert(!run());
    assert(gate.state==CE_ROPG_ACCEPTED && gate.retained==1);
    assert(host.validate_calls==2 && retain_calls==0);
    assert(!ceph_resource_owner_result_valid(&output));
    assert(output.lease.token==host.token);
    assert(output.owner.owner_id==host.owner.owner_id);
    assert(output.resource.crypto.ledger_id==host.resource.crypto.ledger_id);
    assert(run()<0 && gate.state==CE_ROPG_ACCEPTED);

    make_fixture();bind_provider();provider.acquire=NULL;expect_rejected_before_attempt();
    make_fixture();bind_provider();provider.ctx_bytes=0;expect_rejected_before_attempt();
    make_fixture();bind_provider();request.boot_epoch=0;expect_rejected_before_attempt();
    make_fixture();bind_provider();request.now_ticks++;expect_rejected_before_attempt();
    make_fixture();bind_provider();request.reserved0=1;expect_rejected_before_attempt();

    make_fixture();bind_provider();host.acquire_fail=1;expect_fail_retained();
    make_fixture();bind_provider();host.collect_fail=1;expect_fail_retained();
    make_fixture();bind_provider();host.validate_result=1;expect_fail_retained();
    make_fixture();bind_provider();host.validate_result=2;expect_fail_retained();
    make_fixture();bind_provider();host.collect_mode=1;expect_fail_retained();
    make_fixture();bind_provider();host.collect_mode=2;expect_fail_retained();
    make_fixture();bind_provider();host.collect_mode=3;expect_fail_retained();
    make_fixture();bind_provider();host.collect_mode=4;expect_fail_retained();
    make_fixture();bind_provider();host.collect_mode=5;expect_fail_retained();
    make_fixture();bind_provider();assert(ceph_resource_owner_acquire(NULL,&provider,&request,
        &claim,&owner_expected,&resource_expected,&output)<0);
    assert(gate.state==CE_ROPG_NEW && gate.retained==0 && retain_calls==9);
    assert(ceph_resource_owner_acquire(&gate,NULL,&request,&claim,&owner_expected,
        &resource_expected,&output)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,NULL,&claim,&owner_expected,
        &resource_expected,&output)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,NULL,&owner_expected,
        &resource_expected,&output)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,&claim,NULL,
        &resource_expected,&output)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,&claim,&owner_expected,
        NULL,&output)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,&claim,&owner_expected,
        &resource_expected,NULL)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,&claim,&owner_expected,
        &resource_expected,(struct ceph_resource_owner_result *)&gate)<0);
    assert(ceph_resource_owner_acquire(&gate,&provider,&request,&claim,&owner_expected,
        &resource_expected,(struct ceph_resource_owner_result *)((uint8_t *)&host+4))<0);
    assert(gate.state==CE_ROPG_NEW);

    make_fixture();bind_provider();gate.version=CE_RESOURCE_OWNER_VERSION+1;
    expect_rejected_before_attempt();

    puts("resource-owner provider boundary: live provider lease, exact owner/RPMh/crypto/ACK identity, pre/post validation, aliases and permanent retained failure PASS (host-only; no owner/hardware evidence)");
    return 0;
}
