/* Bridge an accepted resource-owner provider result into the private CP
 * admission owner contract. This header performs no MMIO, SMC, PAS, crypto,
 * clock or GPU operation and installs no default provider. */
#ifndef CEPHEUS_RESOURCE_OWNER_CP_ADMISSION_H
#define CEPHEUS_RESOURCE_OWNER_CP_ADMISSION_H
#include <stddef.h>
#include <stdint.h>
#include "cepheus_cp_execution_admission.h"
#include "cepheus_resource_owner_provider.h"

#define CE_RESOURCE_OWNER_CP_BINDING_MAGIC UINT64_C(0x3144504357525243)
#define CE_RESOURCE_OWNER_CP_BINDING_VERSION 1u

enum ceph_resource_owner_cp_state {
    CE_ROCP_NEW=0,
    CE_ROCP_ATTACHED=1,
    CE_ROCP_FAILED=2
};

/* The contract is copied once and must stay immutable. The binding carries
 * only the adapter's own lifecycle state outside this checksummed copy. */
struct ceph_resource_owner_cp_contract {
    struct ceph_resource_owner_provider provider;
    struct ceph_resource_owner_request request;
    struct ceph_zap_claim claim;
    struct ceph_owner_evidence_expectation owner_expected;
    struct ceph_zap_resource_expectation resource_expected;
    struct ceph_resource_owner_result result;
    uint64_t (*now_ticks)(void *);
    void *clock_ctx;
    uint64_t checksum;
} __attribute__((aligned(8)));

struct ceph_resource_owner_cp_binding {
    uint64_t magic;
    uint32_t version,bytes;
    uint32_t state,retained,finished,reserved;
    uint64_t last_ticks;
    struct ceph_resource_owner_cp_contract contract;
} __attribute__((aligned(8)));

static inline uint64_t ceph_resource_owner_cp_contract_checksum(
    const struct ceph_resource_owner_cp_contract *contract)
{
    if(!contract)return 0;
    const uint8_t *bytes=(const uint8_t *)contract;
    const size_t count=offsetof(struct ceph_resource_owner_cp_contract,checksum);
    uint64_t sum=UINT64_C(14695981039346656037);
    for(size_t i=0;i<count;++i)
        sum=(sum^bytes[i])*UINT64_C(1099511628211);
    return sum;
}

static inline int ceph_resource_owner_cp_bytes_equal(const void *a,const void *b,
    size_t bytes)
{
    if(!a || !b || !bytes)return 0;
    const uint8_t *x=a,*y=b;
    for(size_t i=0;i<bytes;++i)if(x[i]!=y[i])return 0;
    return 1;
}

static inline int ceph_resource_owner_cp_bytes_zero(const void *value,size_t bytes)
{
    if(!value || !bytes)return 0;
    const uint8_t *p=value;
    for(size_t i=0;i<bytes;++i)if(p[i])return 0;
    return 1;
}

static inline void ceph_resource_owner_cp_contract_seal(
    struct ceph_resource_owner_cp_contract *contract)
{
    if(contract)contract->checksum=ceph_resource_owner_cp_contract_checksum(contract);
}

static inline int ceph_resource_owner_cp_contract_valid(
    const struct ceph_resource_owner_cp_contract *contract,uint64_t now_ticks)
{
    if(!contract || !now_ticks || contract->checksum!=ceph_resource_owner_cp_contract_checksum(contract) ||
        ceph_resource_owner_provider_valid(&contract->provider) ||
        contract->request.magic!=CE_RESOURCE_OWNER_REQUEST_MAGIC ||
        contract->request.version!=CE_RESOURCE_OWNER_VERSION ||
        contract->request.bytes!=sizeof(contract->request) ||
        contract->request.reserved0 || !contract->request.boot_epoch ||
        !contract->request.now_ticks ||
        contract->owner_expected.now_ticks!=contract->request.now_ticks ||
        ceph_resource_owner_result_valid(&contract->result) ||
        !ceph_resource_owner_cp_bytes_equal(&contract->result.owner_gate.accepted,
            &contract->result.owner,sizeof(contract->result.owner)) ||
        !ceph_resource_owner_cp_bytes_equal(&contract->result.resource_gate.accepted,
            &contract->result.resource,sizeof(contract->result.resource)) ||
        ceph_resource_owner_lease_matches(&contract->result.lease,contract->provider.ctx,
            &contract->request,&contract->owner_expected,&contract->resource_expected) ||
        ceph_resource_owner_evidence_matches(&contract->result.lease,&contract->result.owner,
            &contract->result.resource) || now_ticks<contract->request.now_ticks)return -1;
    struct ceph_owner_evidence_expectation owner_expected=contract->owner_expected;
    owner_expected.now_ticks=now_ticks;
    if(!ceph_owner_evidence_matches(&contract->result.owner,&owner_expected) ||
        ceph_zap_resource_valid(&contract->result.resource,&contract->claim,
            &contract->result.owner_gate,&contract->result.owner,
            &contract->resource_expected,now_ticks))return -1;
    return 0;
}

static inline int ceph_resource_owner_cp_result_sealed(
    const struct ceph_resource_owner_result *result)
{
    if(!result || result->magic!=CE_RESOURCE_OWNER_RESULT_MAGIC ||
        result->version!=CE_RESOURCE_OWNER_VERSION ||
        result->bytes!=sizeof(*result) || !result->lease.checksum ||
        result->lease.checksum!=ceph_resource_owner_lease_checksum(&result->lease) ||
        result->owner_gate.version!=CE_OWNER_EVIDENCE_VERSION ||
        result->owner_gate.state!=CE_OEG_ACCEPTED || result->owner_gate.retained!=1 ||
        result->owner_gate.reserved || result->resource_gate.version!=CE_ZAP_RESOURCE_VERSION ||
        result->resource_gate.state!=CE_ZRG_ACCEPTED ||
        result->resource_gate.retained!=1 || result->resource_gate.reserved ||
        result->owner_gate.accepted.checksum!=result->owner.checksum ||
        result->resource.owner_evidence_checksum!=result->owner.checksum ||
        result->checksum!=ceph_resource_owner_result_checksum(result))return -1;
    return 0;
}

static inline int ceph_resource_owner_cp_input_overlap(
    struct ceph_resource_owner_cp_binding *binding,
    const struct ceph_resource_owner_gate *gate,
    const struct ceph_resource_owner_provider *provider,
    const struct ceph_resource_owner_request *request,
    const struct ceph_zap_claim *claim,
    const struct ceph_owner_evidence_expectation *owner_expected,
    const struct ceph_zap_resource_expectation *resource_expected,
    const struct ceph_resource_owner_result *result,const void *clock_ctx)
{
    if(!binding || !gate || !provider || !request || !claim || !owner_expected ||
        !resource_expected || !result)return -1;
    const void *bases[]={gate,provider,request,claim,owner_expected,resource_expected,result,
        provider->ctx};
    const size_t bytes[]={sizeof(*gate),sizeof(*provider),sizeof(*request),sizeof(*claim),
        sizeof(*owner_expected),sizeof(*resource_expected),sizeof(*result),
        (size_t)provider->ctx_bytes};
    if((uint64_t)(size_t)provider->ctx_bytes!=provider->ctx_bytes ||
        (uintptr_t)binding>UINTPTR_MAX-sizeof(*binding) ||
        (uintptr_t)provider->ctx>UINTPTR_MAX-(size_t)provider->ctx_bytes)return -1;
    for(unsigned i=0;i<sizeof(bases)/sizeof(bases[0]);++i)
        if(ceph_owner_evidence_overlap(binding,sizeof(*binding),bases[i],bytes[i]))return -1;
    const void *records[]={gate,provider,request,claim,owner_expected,resource_expected,result};
    const size_t record_bytes[]={sizeof(*gate),sizeof(*provider),sizeof(*request),
        sizeof(*claim),sizeof(*owner_expected),sizeof(*resource_expected),sizeof(*result)};
    if(ceph_owner_evidence_overlap(binding,sizeof(*binding),clock_ctx,1))return -1;
    for(unsigned i=0;i<sizeof(records)/sizeof(records[0]);++i)
        if(ceph_owner_evidence_overlap(clock_ctx,1,records[i],record_bytes[i]))return -1;
    return 0;
}

static inline int ceph_resource_owner_cp_gate_valid(
    const struct ceph_resource_owner_gate *gate,
    const struct ceph_resource_owner_result *result,
    const struct ceph_resource_owner_request *request)
{
    return !gate || !result || !request || ((uintptr_t)gate&7u) ||
        gate->version!=CE_RESOURCE_OWNER_VERSION || gate->state!=CE_ROPG_ACCEPTED ||
        gate->retained!=1 || gate->reserved || !gate->boot_epoch || !gate->attempted_ticks ||
        gate->boot_epoch!=request->boot_epoch || gate->attempted_ticks>request->now_ticks ||
        !ceph_resource_owner_cp_bytes_equal(&gate->accepted,result,sizeof(*result))?-1:0;
}

/* Attach one already accepted provider result. The binding must be
 * zero-initialized and caller-owned for the lifetime of CP admission. On
 * failure it remains unchanged. The clock callback supplies monotonic ticks in
 * the same units as the request/evidence age limits. */
static inline int ceph_resource_owner_cp_binding_init(
    struct ceph_resource_owner_cp_binding *binding,
    const struct ceph_resource_owner_gate *gate,
    const struct ceph_resource_owner_provider *provider,
    const struct ceph_resource_owner_request *request,
    const struct ceph_zap_claim *claim,
    const struct ceph_owner_evidence_expectation *owner_expected,
    const struct ceph_zap_resource_expectation *resource_expected,
    const struct ceph_resource_owner_result *result,
    uint64_t (*now_ticks)(void *),void *clock_ctx)
{
    if(!binding || ((uintptr_t)binding&7u) || binding->magic || binding->version ||
        binding->bytes || binding->state || binding->retained || binding->finished ||
        binding->reserved || binding->last_ticks || !now_ticks || !clock_ctx ||
        ceph_resource_owner_cp_input_overlap(binding,gate,provider,request,claim,
            owner_expected,resource_expected,result,clock_ctx) ||
        ceph_resource_owner_provider_valid(provider) ||
        !request || !claim || !owner_expected || !resource_expected || !result ||
        ((uintptr_t)provider&7u) || ((uintptr_t)request&7u) || ((uintptr_t)claim&7u) ||
        ((uintptr_t)owner_expected&7u) || ((uintptr_t)resource_expected&7u) ||
        ((uintptr_t)result&7u) || ceph_resource_owner_cp_gate_valid(gate,result,request) ||
        ceph_resource_owner_cp_result_sealed(result) ||
        ceph_resource_owner_lease_matches(&result->lease,provider->ctx,request,
            owner_expected,resource_expected) ||
        ceph_resource_owner_evidence_matches(&result->lease,&result->owner,&result->resource) ||
        !ceph_resource_owner_cp_bytes_equal(&gate->accepted.owner_gate,&result->owner_gate,
            sizeof(result->owner_gate)) ||
        !ceph_resource_owner_cp_bytes_equal(&gate->accepted.owner,&result->owner,
            sizeof(result->owner)) ||
        !ceph_resource_owner_cp_bytes_equal(&gate->accepted.resource_gate,&result->resource_gate,
            sizeof(result->resource_gate)) ||
        !ceph_resource_owner_cp_bytes_equal(&gate->accepted.resource,&result->resource,
            sizeof(result->resource)))return -1;

    struct ceph_resource_owner_cp_contract contract={};
    contract.provider=*provider;
    contract.request=*request;
    contract.claim=*claim;
    contract.owner_expected=*owner_expected;
    contract.resource_expected=*resource_expected;
    contract.result=*result;
    contract.now_ticks=now_ticks;
    contract.clock_ctx=clock_ctx;
    ceph_resource_owner_cp_contract_seal(&contract);
    if(ceph_resource_owner_cp_contract_valid(&contract,request->now_ticks))return -1;
    if(contract.provider.validate(contract.provider.ctx,&contract.result.lease,
        request->now_ticks))return -1;
    if(ceph_resource_owner_cp_contract_valid(&contract,request->now_ticks))return -1;

    binding->contract=contract;
    binding->last_ticks=request->now_ticks;
    binding->magic=CE_RESOURCE_OWNER_CP_BINDING_MAGIC;
    binding->version=CE_RESOURCE_OWNER_CP_BINDING_VERSION;
    binding->bytes=sizeof(*binding);
    binding->state=CE_ROCP_ATTACHED;
    return 0;
}

static inline int ceph_resource_owner_cp_binding_attached(
    const struct ceph_resource_owner_cp_binding *binding)
{
    return !binding || binding->magic!=CE_RESOURCE_OWNER_CP_BINDING_MAGIC ||
        binding->version!=CE_RESOURCE_OWNER_CP_BINDING_VERSION ||
        binding->bytes!=sizeof(*binding) || binding->state!=CE_ROCP_ATTACHED ||
        binding->reserved;
}

static inline int ceph_resource_owner_cp_binding_initialized(
    const struct ceph_resource_owner_cp_binding *binding)
{
    if(!binding)return -1;
    if(!binding->magic && !binding->version && !binding->bytes &&
        !binding->state && !binding->retained && !binding->finished &&
        !binding->reserved && !binding->last_ticks)return 0;
    return binding->magic!=CE_RESOURCE_OWNER_CP_BINDING_MAGIC ||
        binding->version!=CE_RESOURCE_OWNER_CP_BINDING_VERSION ||
        binding->bytes!=sizeof(*binding) || binding->reserved ||
        (binding->state!=CE_ROCP_ATTACHED && binding->state!=CE_ROCP_FAILED)?-1:0;
}

static inline int ceph_resource_owner_cp_record_valid(
    const struct ceph_resource_owner_cp_binding *binding,
    const struct ceph_cp_execution_admission *record)
{
    if(!binding || !record)return -1;
    if(record->version==0)return ceph_resource_owner_cp_bytes_zero(record,sizeof(*record))?1:-1;
    if(record->version!=CE_CP_EXECUTION_ADMISSION_VERSION || record->ring_writes!=7 ||
        !record->admission_token || !record->zap_epoch || !record->rpmh_token ||
        record->rpmh_token!=binding->contract.result.lease.rpmh_request_count ||
        record->zap_epoch!=binding->contract.result.owner.resource_epoch ||
        record->zap.memory.mdt_pa!=binding->contract.result.resource.memory.mdt_pa ||
        record->zap.memory.image_pa!=binding->contract.result.resource.memory.image_pa ||
        !record->zap.handle || record->zap.handle!=binding->contract.result.resource.memory.handle ||
        record->owner_id!=binding->contract.result.owner.owner_id ||
        record->lease_id!=binding->contract.result.owner.lease_id ||
        record->resource_epoch!=binding->contract.result.owner.resource_epoch)return -1;
    return 0;
}

static inline int ceph_resource_owner_cp_owner_validate(void *ctx)
{
    struct ceph_resource_owner_cp_binding *binding=ctx;
    if(ceph_resource_owner_cp_binding_attached(binding))return -1;
    const struct ceph_resource_owner_cp_contract *contract=&binding->contract;
    uint64_t now=contract->now_ticks(contract->clock_ctx);
    if(!now || now<binding->last_ticks ||
        ceph_resource_owner_cp_contract_valid(contract,now) ||
        contract->provider.validate(contract->provider.ctx,&contract->result.lease,now) ||
        ceph_resource_owner_cp_contract_valid(contract,now)) {
        binding->state=CE_ROCP_FAILED;
        return -1;
    }
    if(!binding->finished)binding->last_ticks=now;
    return 0;
}

static inline void ceph_resource_owner_cp_owner_finish(void *ctx,
    const struct ceph_cp_execution_admission *record,int keep)
{
    struct ceph_resource_owner_cp_binding *binding=ctx;
    if(ceph_resource_owner_cp_binding_initialized(binding))return;
    const struct ceph_resource_owner_cp_contract *contract=&binding->contract;
    int first=!binding->finished;
    int record_state=first?ceph_resource_owner_cp_record_valid(binding,record):-1;
    int bad=(keep!=0 && keep!=1) || !first || record_state!=0 ||
        binding->retained || binding->state!=CE_ROCP_ATTACHED;
    uint64_t now=contract->now_ticks(contract->clock_ctx);
    if(!now || now<binding->last_ticks || ceph_resource_owner_cp_contract_valid(contract,now) ||
        contract->provider.validate(contract->provider.ctx,&contract->result.lease,now))bad=1;
    if((keep || bad) && !binding->retained) {
        contract->provider.retain(contract->provider.ctx,&contract->result.lease);
        binding->retained=1;
    }
    binding->finished=1;
    if(record_state==0 && !bad)binding->state=CE_ROCP_ATTACHED;
    else binding->state=CE_ROCP_FAILED;
}

static inline struct ceph_cp_execution_admission_owner
ceph_resource_owner_cp_owner(struct ceph_resource_owner_cp_binding *binding)
{
    struct ceph_cp_execution_admission_owner owner={0};
    if(ceph_resource_owner_cp_binding_attached(binding))return owner;
    owner.ctx=binding;
    owner.evidence_gate=&binding->contract.result.owner_gate;
    owner.evidence=&binding->contract.result.owner;
    owner.validate=ceph_resource_owner_cp_owner_validate;
    owner.finish=ceph_resource_owner_cp_owner_finish;
    return owner;
}
#endif
