/* Fail-closed live resource-owner provider boundary. The provider owns the
 * lease and supplies the evidence; this header performs no MMIO, SMC, PAS,
 * crypto, clock or GPU operation and installs no default backend. */
#ifndef CEPHEUS_RESOURCE_OWNER_PROVIDER_H
#define CEPHEUS_RESOURCE_OWNER_PROVIDER_H
#include <stddef.h>
#include <stdint.h>
#include "cepheus_zap_resource_evidence.h"

#define CE_RESOURCE_OWNER_LEASE_MAGIC UINT64_C(0x3153454c574f5252)
#define CE_RESOURCE_OWNER_REQUEST_MAGIC UINT64_C(0x31514552574f5252)
#define CE_RESOURCE_OWNER_PROVIDER_MAGIC UINT64_C(0x3150564f52575243)
#define CE_RESOURCE_OWNER_RESULT_MAGIC UINT64_C(0x3152554f52575243)
#define CE_RESOURCE_OWNER_VERSION 1u
#define CE_RESOURCE_OWNER_LEASE_BYTES 160u
#define CE_RESOURCE_OWNER_REQUEST_BYTES 40u
#define CE_RESOURCE_OWNER_PROVIDER_BYTES 64u
#define CE_RESOURCE_OWNER_RESULT_BYTES 1432u
#define CE_RESOURCE_OWNER_GATE_BYTES 1464u

enum ceph_resource_owner_lease_state {
    CE_ROLS_NONE=0,
    CE_ROLS_LIVE=1,
    CE_ROLS_RETAINED=2,
    CE_ROLS_RELEASED=3
};
enum ceph_resource_owner_mode {
    CE_ROM_NONE=0,
    CE_ROM_EXCLUSIVE_REAL=1,
    CE_ROM_SIMULATED=2,
    CE_ROM_INHERITED_UNVERIFIED=3
};
enum ceph_resource_owner_gate_state {
    CE_ROPG_NEW=0,
    CE_ROPG_ATTEMPTED=1,
    CE_ROPG_ACCEPTED=2,
    CE_ROPG_UNCERTAIN=3
};

/* The provider fills this record. provider_handle must identify provider-owned
 * state and token must be a nonzero live lease identity chosen by that state.
 * The ledger and ACK identities are duplicated here so the gate can require an
 * exact match with both collected evidence records and the caller's limits. */
struct ceph_resource_owner_lease {
    uint64_t magic;
    uint32_t version,bytes;
    uint32_t state,mode;
    uint32_t simulated,reserved0;
    uint32_t ack_count,reserved1;
    uintptr_t provider_handle;
    uint64_t provider_id;
    uint64_t token;
    uint64_t boot_epoch;
    uint64_t owner_id;
    uint64_t lease_id;
    uint64_t resource_epoch;
    uint64_t ack_epoch;
    uint64_t rpmh_ledger_id;
    uint64_t rpmh_request_count;
    uint64_t rpmh_checksum;
    uint64_t crypto_ledger_id;
    uint64_t crypto_request_count;
    uint64_t crypto_ledger_checksum;
    uint64_t checksum;
};

struct ceph_resource_owner_request {
    uint64_t magic;
    uint64_t boot_epoch;
    uint64_t now_ticks;
    uint32_t version,bytes;
    uint32_t reserved0;
};

struct ceph_resource_owner_provider {
    uint64_t magic;
    uint32_t version,bytes;
    void *ctx;
    uint64_t ctx_bytes;
    int (*acquire)(void *,const struct ceph_zap_claim *,
        const struct ceph_owner_evidence_expectation *,
        const struct ceph_zap_resource_expectation *,uint64_t,
        struct ceph_resource_owner_lease *);
    int (*collect)(void *,const struct ceph_zap_claim *,
        const struct ceph_resource_owner_lease *,
        struct ceph_owner_evidence_snapshot *,
        struct ceph_zap_resource_evidence *);
    int (*validate)(void *,const struct ceph_resource_owner_lease *,uint64_t);
    void (*retain)(void *,const struct ceph_resource_owner_lease *);
};

struct ceph_resource_owner_result {
    uint64_t magic;
    uint32_t version,bytes;
    struct ceph_resource_owner_lease lease;
    struct ceph_owner_evidence_gate owner_gate;
    struct ceph_owner_evidence_snapshot owner;
    struct ceph_zap_resource_gate resource_gate;
    struct ceph_zap_resource_evidence resource;
    uint64_t checksum;
};

struct ceph_resource_owner_gate {
    uint32_t version,state,retained,reserved;
    uint64_t boot_epoch,attempted_ticks;
    struct ceph_resource_owner_result accepted;
} __attribute__((aligned(8)));

static inline uint64_t ceph_resource_owner_lease_checksum(
    const struct ceph_resource_owner_lease *lease)
{
    if(!lease)return 0;
    uint64_t sum=UINT64_C(14695981039346656037);
    const uint32_t u32[]={lease->version,lease->bytes,lease->state,lease->mode,
        lease->simulated,lease->reserved0,lease->ack_count,lease->reserved1};
    for(unsigned i=0;i<sizeof(u32)/sizeof(u32[0]);++i)
        sum=ceph_owner_evidence_mix(sum,u32[i]);
    const uint64_t u64[]={lease->magic,(uint64_t)lease->provider_handle,
        lease->provider_id,lease->token,lease->boot_epoch,lease->owner_id,
        lease->lease_id,lease->resource_epoch,lease->ack_epoch,
        lease->rpmh_ledger_id,lease->rpmh_request_count,lease->rpmh_checksum,
        lease->crypto_ledger_id,lease->crypto_request_count,
        lease->crypto_ledger_checksum};
    for(unsigned i=0;i<sizeof(u64)/sizeof(u64[0]);++i)
        sum=ceph_owner_evidence_mix(sum,u64[i]);
    return sum;
}

static inline void ceph_resource_owner_lease_seal(
    struct ceph_resource_owner_lease *lease)
{
    if(lease)lease->checksum=ceph_resource_owner_lease_checksum(lease);
}

static inline uint64_t ceph_resource_owner_result_checksum(
    const struct ceph_resource_owner_result *result)
{
    if(!result)return 0;
    uint64_t sum=UINT64_C(14695981039346656037);
    const uint64_t values[]={result->magic,result->version,result->bytes,
        result->lease.checksum,result->owner_gate.state,result->owner_gate.retained,
        result->owner.checksum,result->resource_gate.state,
        result->resource_gate.retained,result->resource.checksum};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
        sum=ceph_owner_evidence_mix(sum,values[i]);
    return sum;
}

static inline void ceph_resource_owner_result_seal(
    struct ceph_resource_owner_result *result)
{
    if(result)result->checksum=ceph_resource_owner_result_checksum(result);
}

static inline int ceph_resource_owner_result_valid(
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
        !ceph_zap_resource_same(&result->resource_gate.accepted,&result->resource) ||
        result->owner_gate.accepted.checksum!=result->owner.checksum ||
        result->resource.owner_evidence_checksum!=result->owner.checksum ||
        result->checksum!=ceph_resource_owner_result_checksum(result))return -1;
    return 0;
}

static inline int ceph_resource_owner_lease_matches(
    const struct ceph_resource_owner_lease *lease,const void *ctx,
    const struct ceph_resource_owner_request *request,
    const struct ceph_owner_evidence_expectation *owner_expected,
    const struct ceph_zap_resource_expectation *resource_expected)
{
    if(!lease || !request || !owner_expected || !resource_expected ||
        sizeof(*lease)!=CE_RESOURCE_OWNER_LEASE_BYTES ||
        lease->magic!=CE_RESOURCE_OWNER_LEASE_MAGIC ||
        lease->version!=CE_RESOURCE_OWNER_VERSION || lease->bytes!=sizeof(*lease) ||
        lease->state!=CE_ROLS_LIVE || lease->mode!=CE_ROM_EXCLUSIVE_REAL ||
        lease->simulated || lease->reserved0 || lease->reserved1 || !lease->provider_handle ||
        lease->provider_handle!=(uintptr_t)ctx || !lease->provider_id || !lease->token ||
        lease->boot_epoch!=request->boot_epoch ||
        lease->owner_id!=owner_expected->owner_id ||
        lease->lease_id!=owner_expected->lease_id ||
        lease->resource_epoch!=owner_expected->resource_epoch ||
        lease->ack_epoch!=owner_expected->resource_epoch ||
        lease->ack_count!=owner_expected->ack_count ||
        lease->rpmh_ledger_id!=owner_expected->rpmh_ledger_id ||
        lease->rpmh_request_count!=owner_expected->rpmh_request_count ||
        lease->rpmh_checksum!=owner_expected->rpmh_checksum ||
        lease->crypto_ledger_id!=resource_expected->crypto_ledger_id ||
        lease->crypto_request_count!=resource_expected->request_count ||
        lease->crypto_ledger_checksum!=resource_expected->ledger_checksum ||
        lease->checksum!=ceph_resource_owner_lease_checksum(lease))return -1;
    return 0;
}

static inline int ceph_resource_owner_evidence_matches(
    const struct ceph_resource_owner_lease *lease,
    const struct ceph_owner_evidence_snapshot *owner,
    const struct ceph_zap_resource_evidence *resource)
{
    if(!lease || !owner || !resource || owner->owner_id!=lease->owner_id ||
        resource->owner_evidence_checksum!=owner->checksum ||
        owner->lease_id!=lease->lease_id || owner->resource_epoch!=lease->resource_epoch ||
        owner->ack_epoch!=lease->ack_epoch || owner->ack_count!=lease->ack_count ||
        owner->rpmh.ledger_id!=lease->rpmh_ledger_id ||
        owner->rpmh.request_count!=lease->rpmh_request_count ||
        owner->rpmh.checksum!=lease->rpmh_checksum ||
        resource->crypto.owner_id!=lease->owner_id ||
        resource->crypto.lease_id!=lease->lease_id ||
        resource->crypto.resource_epoch!=lease->resource_epoch ||
        resource->crypto.ledger_id!=lease->crypto_ledger_id ||
        resource->crypto.request_count!=lease->crypto_request_count ||
        resource->crypto.ledger_checksum!=lease->crypto_ledger_checksum ||
        resource->crypto.ack_epoch!=lease->ack_epoch ||
        resource->crypto.ack_count!=lease->ack_count)return -1;
    return 0;
}

static inline int ceph_resource_owner_provider_valid(
    const struct ceph_resource_owner_provider *provider)
{
    return !provider || sizeof(*provider)!=CE_RESOURCE_OWNER_PROVIDER_BYTES ||
        provider->magic!=CE_RESOURCE_OWNER_PROVIDER_MAGIC ||
        provider->version!=CE_RESOURCE_OWNER_VERSION ||
        provider->bytes!=sizeof(*provider) || !provider->ctx || !provider->ctx_bytes ||
        !provider->acquire || !provider->collect || !provider->validate || !provider->retain;
}

static inline int ceph_resource_owner_output_overlap(
    const struct ceph_resource_owner_result *out,
    const struct ceph_resource_owner_gate *gate,
    const struct ceph_resource_owner_provider *provider,
    const struct ceph_resource_owner_request *request,
    const struct ceph_zap_claim *claim,
    const struct ceph_owner_evidence_expectation *owner_expected,
    const struct ceph_zap_resource_expectation *resource_expected)
{
    if(!out || !gate || !provider || !request || !claim || !owner_expected ||
        !resource_expected)return -1;
    const void *bases[]={gate,provider,request,claim,owner_expected,resource_expected,
        provider->ctx};
    const size_t bytes[]={sizeof(*gate),sizeof(*provider),sizeof(*request),
        sizeof(*claim),sizeof(*owner_expected),sizeof(*resource_expected),
        (size_t)provider->ctx_bytes};
    if((uint64_t)(size_t)provider->ctx_bytes!=provider->ctx_bytes)return -1;
    for(unsigned i=0;i<sizeof(bases)/sizeof(bases[0]);++i)
        if(ceph_owner_evidence_overlap(out,sizeof(*out),bases[i],bytes[i]))return -1;
    return 0;
}

/* One provider attempt per boot. On every attempted failure the lease is kept
 * opaque and retain is invoked; there is intentionally no reset/release path. */
static inline int ceph_resource_owner_acquire(
    struct ceph_resource_owner_gate *gate,
    const struct ceph_resource_owner_provider *provider,
    const struct ceph_resource_owner_request *request,
    const struct ceph_zap_claim *claim,
    const struct ceph_owner_evidence_expectation *owner_expected,
    const struct ceph_zap_resource_expectation *resource_expected,
    struct ceph_resource_owner_result *out)
{
    if(!gate || ((uintptr_t)gate&7u) || !out || ((uintptr_t)out&7u) ||
        ceph_resource_owner_provider_valid(provider) || !request || !claim ||
        !owner_expected || !resource_expected ||
        sizeof(*request)!=CE_RESOURCE_OWNER_REQUEST_BYTES ||
        sizeof(*out)!=CE_RESOURCE_OWNER_RESULT_BYTES ||
        sizeof(*gate)!=CE_RESOURCE_OWNER_GATE_BYTES ||
        request->magic!=CE_RESOURCE_OWNER_REQUEST_MAGIC ||
        request->version!=CE_RESOURCE_OWNER_VERSION ||
        request->bytes!=sizeof(*request) || request->reserved0 ||
        !request->boot_epoch || !request->now_ticks ||
        owner_expected->now_ticks!=request->now_ticks ||
        (gate->version && gate->version!=CE_RESOURCE_OWNER_VERSION) ||
        ceph_resource_owner_output_overlap(out,gate,provider,request,claim,
            owner_expected,resource_expected))return -1;
    if(gate->state!=CE_ROPG_NEW)return -1;

    struct ceph_resource_owner_lease lease={};
    struct ceph_owner_evidence_snapshot owner={};
    struct ceph_zap_resource_evidence resource={};
    struct ceph_owner_evidence_gate owner_gate={};
    struct ceph_zap_resource_gate resource_gate={};
    struct ceph_owner_evidence_snapshot owner_out={};
    struct ceph_zap_resource_evidence resource_out={};
    void *provider_ctx=provider->ctx;

    /* Keep the exact provider identity used for acquire/collect/validation.
     * A failed attempt may still need to retain the provider-owned lease even
     * if caller memory changes after the provider has handed it out. */
    gate->version=CE_RESOURCE_OWNER_VERSION;
    gate->state=CE_ROPG_ATTEMPTED;
    gate->retained=1;
    gate->boot_epoch=request->boot_epoch;
    gate->attempted_ticks=request->now_ticks;
    int e=provider->acquire(provider_ctx,claim,owner_expected,resource_expected,
        request->now_ticks,&lease);
    if(e || ceph_resource_owner_lease_matches(&lease,provider_ctx,request,
        owner_expected,resource_expected) || provider->validate(provider_ctx,&lease,
        request->now_ticks))goto retain;
    e=provider->collect(provider_ctx,claim,&lease,&owner,&resource);
    if(e || provider->validate(provider_ctx,&lease,request->now_ticks) ||
        ceph_resource_owner_evidence_matches(&lease,&owner,&resource))goto retain;
    if(ceph_owner_evidence_admit(&owner_gate,&owner,owner_expected,&owner_out) ||
        ceph_zap_resource_admit(&resource_gate,claim,&resource,&owner_gate,&owner_out,
            resource_expected,request->now_ticks,&resource_out))goto retain;
    if(!ceph_zap_resource_same(&resource_gate.accepted,&resource_out) ||
        owner_gate.accepted.checksum!=owner_out.checksum ||
        resource_out.owner_evidence_checksum!=owner_out.checksum)goto retain;

    struct ceph_resource_owner_result result={};
    result.magic=CE_RESOURCE_OWNER_RESULT_MAGIC;
    result.version=CE_RESOURCE_OWNER_VERSION;
    result.bytes=sizeof(result);
    result.lease=lease;
    result.owner_gate=owner_gate;
    result.owner=owner_out;
    result.resource_gate=resource_gate;
    result.resource=resource_out;
    ceph_resource_owner_result_seal(&result);
    if(ceph_resource_owner_result_valid(&result))goto retain;
    gate->accepted=result;
    gate->state=CE_ROPG_ACCEPTED;
    *out=result;
    return 0;

retain:
    gate->state=CE_ROPG_UNCERTAIN;
    gate->retained=1;
    provider->retain(provider_ctx,&lease);
    return -1;
}
#endif
