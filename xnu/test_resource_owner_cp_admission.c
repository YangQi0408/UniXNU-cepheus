#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cepheus_resource_owner_cp_admission.h"

struct host_provider {
    uint64_t marker;
    uint64_t now;
    int validate_result;
    int validate_calls;
    int retain_calls;
};

static struct host_provider host;
static struct ceph_resource_owner_provider provider;
static struct ceph_resource_owner_gate gate;
static struct ceph_resource_owner_request request;
static struct ceph_zap_claim claim;
static struct ceph_owner_evidence_expectation owner_expected;
static struct ceph_zap_resource_expectation resource_expected;
static struct ceph_resource_owner_result result;
static struct ceph_resource_owner_cp_binding binding;

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
    memset(&gate,0,sizeof(gate));
    memset(&request,0,sizeof(request));
    memset(&claim,0,sizeof(claim));
    memset(&owner_expected,0,sizeof(owner_expected));
    memset(&resource_expected,0,sizeof(resource_expected));
    memset(&result,0,sizeof(result));
    memset(&binding,0,sizeof(binding));
    host.marker=UINT64_C(0x484f535443504144);
    host.now=1100;
    claim.handle=0x4000000000000010;
    claim.memory.mdt_pa=0x180000000;
    claim.memory.image_pa=0x181000000;
    claim.memory.mdt_capacity=16384;
    claim.memory.image_capacity=1048576;
    claim.memory.mdt_bytes=6860;
    claim.memory.logical_bytes=4096;

    struct ceph_owner_evidence_snapshot owner;
    struct ceph_zap_resource_evidence resource;
    make_owner(&owner);
    make_resource(&resource,&owner);
    owner_expected=(struct ceph_owner_evidence_expectation){
        .magic=CE_OWNER_EXPECTATION_MAGIC,.version=CE_OWNER_EVIDENCE_VERSION,
        .bytes=sizeof(owner_expected),.owner_domain=CE_OD_XNU,.writer_mask=CE_OW_ALL,
        .rpmh_source=CE_ORS_INHERITED_UEFI,.ack_count=owner.ack_count,
        .owner_id=owner.owner_id,.lease_id=owner.lease_id,
        .resource_epoch=owner.resource_epoch,.now_ticks=1100,.max_age_ticks=300,
        .rpmh_ledger_id=owner.rpmh.ledger_id,
        .rpmh_request_count=owner.rpmh.request_count,.rpmh_checksum=owner.rpmh.checksum};
    resource_expected=(struct ceph_zap_resource_expectation){
        .magic=CE_ZAP_RESOURCE_EXPECTATION_MAGIC,.version=CE_ZAP_RESOURCE_VERSION,
        .bytes=sizeof(resource_expected),.max_age_ticks=300,
        .crypto_ledger_id=resource.crypto.ledger_id,
        .request_count=resource.crypto.request_count,
        .ledger_checksum=resource.crypto.ledger_checksum,
        .ib_bytes=resource.crypto.ib_bytes,.ab_bytes=resource.crypto.ab_bytes};
    request=(struct ceph_resource_owner_request){.magic=CE_RESOURCE_OWNER_REQUEST_MAGIC,
        .boot_epoch=0x640008,.now_ticks=1100,.version=CE_RESOURCE_OWNER_VERSION,
        .bytes=sizeof(request)};
    result.magic=CE_RESOURCE_OWNER_RESULT_MAGIC;
    result.version=CE_RESOURCE_OWNER_VERSION;
    result.bytes=sizeof(result);
    result.lease=(struct ceph_resource_owner_lease){.magic=CE_RESOURCE_OWNER_LEASE_MAGIC,
        .version=CE_RESOURCE_OWNER_VERSION,.bytes=sizeof(result.lease),
        .state=CE_ROLS_LIVE,.mode=CE_ROM_EXCLUSIVE_REAL,.ack_count=owner.ack_count,
        .provider_handle=(uintptr_t)&host,.provider_id=0x64000a,.token=0x640009,
        .boot_epoch=request.boot_epoch,.owner_id=owner.owner_id,.lease_id=owner.lease_id,
        .resource_epoch=owner.resource_epoch,.ack_epoch=owner.ack_epoch,
        .rpmh_ledger_id=owner.rpmh.ledger_id,
        .rpmh_request_count=owner.rpmh.request_count,.rpmh_checksum=owner.rpmh.checksum,
        .crypto_ledger_id=resource.crypto.ledger_id,
        .crypto_request_count=resource.crypto.request_count,
        .crypto_ledger_checksum=resource.crypto.ledger_checksum};
    ceph_resource_owner_lease_seal(&result.lease);
    result.owner_gate=(struct ceph_owner_evidence_gate){.version=CE_OWNER_EVIDENCE_VERSION,
        .state=CE_OEG_ACCEPTED,.retained=1,.accepted=owner};
    result.owner=owner;
    result.resource_gate=(struct ceph_zap_resource_gate){.version=CE_ZAP_RESOURCE_VERSION,
        .state=CE_ZRG_ACCEPTED,.retained=1,.accepted=resource};
    result.resource=resource;
    ceph_resource_owner_result_seal(&result);
    assert(!ceph_resource_owner_result_valid(&result));
    gate=(struct ceph_resource_owner_gate){.version=CE_RESOURCE_OWNER_VERSION,
        .state=CE_ROPG_ACCEPTED,.retained=1,.boot_epoch=request.boot_epoch,
        .attempted_ticks=request.now_ticks,.accepted=result};
    provider=(struct ceph_resource_owner_provider){.magic=CE_RESOURCE_OWNER_PROVIDER_MAGIC,
        .version=CE_RESOURCE_OWNER_VERSION,.bytes=sizeof(provider),.ctx=&host,
        .ctx_bytes=sizeof(host),.acquire=(void *)1,.collect=(void *)1,
        .validate=(int (*)(void *,const struct ceph_resource_owner_lease *,uint64_t))1,
        .retain=(void (*)(void *,const struct ceph_resource_owner_lease *))1};
}

static int validate_cb(void *ctx,const struct ceph_resource_owner_lease *lease,
    uint64_t now)
{
    struct host_provider *h=ctx;
    ++h->validate_calls;
    if(h->validate_result && h->validate_calls==h->validate_result)return -1;
    return !h || h->marker!=UINT64_C(0x484f535443504144) ||
        lease->provider_handle!=(uintptr_t)h || lease->token!=0x640009 ||
        now!=h->now?-1:0;
}

static void retain_cb(void *ctx,const struct ceph_resource_owner_lease *lease)
{
    struct host_provider *h=ctx;
    assert(h && h->marker==UINT64_C(0x484f535443504144));
    assert(lease && lease->provider_handle==(uintptr_t)h);
    ++h->retain_calls;
}

static uint64_t now_cb(void *ctx)
{
    struct host_provider *h=ctx;
    assert(h && h->marker==UINT64_C(0x484f535443504144));
    return h->now;
}

__attribute__((noinline)) static int attach_checked(void)
{
    return ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,&host);
}

static void bind_provider(void)
{
    provider.acquire=(void *)1;
    provider.collect=(void *)1;
    provider.validate=validate_cb;
    provider.retain=retain_cb;
}

static int attach(void)
{
    return attach_checked();
}

static struct ceph_cp_execution_admission_owner attached_owner(void)
{
    int e=attach_checked();
    if(e)abort();
    struct ceph_cp_execution_admission_owner owner=ceph_resource_owner_cp_owner(&binding);
    if(!owner.finish || !owner.validate)abort();
    return owner;
}

static struct ceph_cp_execution_admission make_record(void)
{
    struct ceph_cp_execution_admission record={0};
    record.version=CE_CP_EXECUTION_ADMISSION_VERSION;
    record.ring_writes=7;
    record.admission_token=0x6400000000000001ULL;
    record.zap_epoch=result.owner.resource_epoch;
    record.zap=claim;
    record.rpmh_token=result.lease.rpmh_request_count;
    record.owner_id=result.owner.owner_id;
    record.lease_id=result.owner.lease_id;
    record.resource_epoch=result.owner.resource_epoch;
    return record;
}

static void expect_attach_rejected(void)
{
    struct ceph_resource_owner_cp_binding before=binding;
    assert(attach()<0);
    assert(!memcmp(&before,&binding,sizeof(binding)));
    assert(host.validate_calls==0 && host.retain_calls==0);
}

int main(void)
{
    make_fixture();bind_provider();
    int attached=attach_checked();
    assert(!attached);
    assert(binding.state==CE_ROCP_ATTACHED && binding.retained==0 && !binding.finished);
    assert(host.validate_calls==1);
    struct ceph_cp_execution_admission_owner owner=ceph_resource_owner_cp_owner(&binding);
    assert(owner.ctx==&binding && owner.evidence_gate==&binding.contract.result.owner_gate &&
        owner.evidence==&binding.contract.result.owner && owner.validate && owner.finish);
    int validated=owner.validate(owner.ctx);
    assert(!validated);
    assert(binding.last_ticks==host.now);
    host.now=1150;
    int validated_later=owner.validate(owner.ctx);
    assert(!validated_later);
    struct ceph_cp_execution_admission record=make_record();
    owner.finish(owner.ctx,&record,1);
    assert(binding.finished && binding.retained==1 && host.retain_calls==1 &&
        binding.state==CE_ROCP_ATTACHED);
    owner.finish(owner.ctx,NULL,0);
    assert(binding.state==CE_ROCP_FAILED && host.retain_calls==1);
    owner.finish(owner.ctx,NULL,1);
    assert(host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && !binding.retained && host.retain_calls==0 &&
        binding.state==CE_ROCP_ATTACHED);
    owner.finish(owner.ctx,&record,0);
    assert(binding.state==CE_ROCP_FAILED && host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    owner.finish(owner.ctx,NULL,1);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    struct ceph_cp_execution_admission zero;
    memset(&zero,0,sizeof(zero));
    owner.finish(owner.ctx,&zero,0);
    assert(binding.retained==1 && binding.state==CE_ROCP_FAILED && host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();record.owner_id++;owner.finish(owner.ctx,&record,0);
    assert(binding.retained==1 && binding.state==CE_ROCP_FAILED && host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();record.zap_epoch++;owner.finish(owner.ctx,&record,0);
    assert(binding.retained==1 && binding.state==CE_ROCP_FAILED);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();record.lease_id++;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();record.zap.handle++;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();record.rpmh_token++;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    host.now=1100;host.validate_result=host.validate_calls+1;
    assert(owner.validate(owner.ctx)<0 && binding.state==CE_ROCP_FAILED);
    owner.finish(owner.ctx,NULL,1);
    assert(binding.retained==1 && host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    host.now=1401;assert(owner.validate(owner.ctx)<0);
    host.now=1150;assert(owner.validate(owner.ctx)<0);

    make_fixture();bind_provider();
    owner=attached_owner();
    host.now=0;assert(owner.validate(owner.ctx)<0 && binding.state==CE_ROCP_FAILED);

    make_fixture();bind_provider();
    owner=attached_owner();
    host.now=1099;assert(owner.validate(owner.ctx)<0 && binding.state==CE_ROCP_FAILED);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();host.now=1401;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();host.now=1100;host.validate_result=host.validate_calls+1;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();host.now=0;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();
    owner=attached_owner();
    record=make_record();host.now=1099;
    owner.finish(owner.ctx,&record,0);
    assert(binding.finished && binding.retained==1 && binding.state==CE_ROCP_FAILED &&
        host.retain_calls==1);

    make_fixture();bind_provider();gate.state=CE_ROPG_UNCERTAIN;expect_attach_rejected();
    make_fixture();bind_provider();gate.retained=0;expect_attach_rejected();
    make_fixture();bind_provider();gate.boot_epoch++;expect_attach_rejected();
    make_fixture();bind_provider();gate.accepted.owner.owner_id++;expect_attach_rejected();
    make_fixture();bind_provider();result.owner.owner_id++;expect_attach_rejected();
    make_fixture();bind_provider();result.checksum^=1;expect_attach_rejected();
    make_fixture();bind_provider();request.now_ticks++;expect_attach_rejected();
    make_fixture();bind_provider();provider.validate=NULL;expect_attach_rejected();
    make_fixture();bind_provider();assert(ceph_resource_owner_cp_binding_init(&binding,&gate,
        &provider,&request,&claim,&owner_expected,&resource_expected,&result,NULL,&host)<0);
    assert(!ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,&host));
    assert(ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,&host)<0);

    make_fixture();bind_provider();
    assert(ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,&result)<0);
    assert(ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,&gate)<0);
    assert(ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,(uint8_t *)&binding+4)<0);
    assert(ceph_resource_owner_cp_binding_init(&binding,&gate,&provider,&request,&claim,
        &owner_expected,&resource_expected,&result,now_cb,NULL)<0);
    assert(ceph_resource_owner_cp_binding_init((struct ceph_resource_owner_cp_binding *)
        ((uint8_t *)&binding+4),&gate,&provider,&request,&claim,&owner_expected,
        &resource_expected,&result,now_cb,&host)<0);
    assert(!binding.magic);

    puts("resource-owner CP admission bridge: accepted gate/result identity, live lease/evidence age, CP record binding, permanent retention and aliases PASS (host-only; no owner/hardware evidence)");
    return 0;
}
