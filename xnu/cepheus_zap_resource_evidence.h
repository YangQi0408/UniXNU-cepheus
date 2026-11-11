/* Fail-closed secure PA/XPU/cache and crypto route evidence for PAS.
 * This is a copied-record contract only. It performs no MMIO, SMC, firmware
 * call, clock/BCM vote or GPU submission, and does not create an owner.
 */
#ifndef CEPHEUS_ZAP_RESOURCE_EVIDENCE_H
#define CEPHEUS_ZAP_RESOURCE_EVIDENCE_H
#include <stddef.h>
#include <stdint.h>
#include "cepheus_owner_evidence.h"
#include "cepheus_zap_memory_private.h"

#define CE_ZAP_RESOURCE_MAGIC UINT64_C(0x3152455a50455a43)
#define CE_ZAP_RESOURCE_EXPECTATION_MAGIC UINT64_C(0x3158455a50455a43)
#define CE_ZAP_RESOURCE_VERSION 1u
#define CE_ZAP_RESOURCE_BCM_COUNT 6u
#define CE_ZAP_RESOURCE_MEMORY_BYTES 88u
#define CE_ZAP_RESOURCE_CRYPTO_BYTES 176u
#define CE_ZAP_RESOURCE_EVIDENCE_BYTES 320u
#define CE_ZAP_RESOURCE_EXPECTATION_BYTES 72u
#define CE_ZAP_RESOURCE_GATE_BYTES 336u

/* Static SM8150 crypto route facts from the accepted offline evidence. These
 * are identities to match, not proof that a vote or ACK exists at runtime. */
#define CE_ZAP_RESOURCE_CRYPTO_MASTER 23u
#define CE_ZAP_RESOURCE_CRYPTO_SLAVE 0u
#define CE_ZAP_RESOURCE_BCM_CE0 UINT64_C(0x3ca30)
#define CE_ZAP_RESOURCE_BCM_SN11 UINT64_C(0x3b160)
#define CE_ZAP_RESOURCE_BCM_SN0 UINT64_C(0x3b260)
#define CE_ZAP_RESOURCE_BCM_SH0 UINT64_C(0x321f8)
#define CE_ZAP_RESOURCE_BCM_MC0 UINT64_C(0x32350)
#define CE_ZAP_RESOURCE_BCM_ACV UINT64_C(0x32448)
#define CE_ZAP_RESOURCE_MDT_BYTES UINT64_C(6860)
#define CE_ZAP_RESOURCE_LOGICAL_BYTES UINT64_C(4096)

enum ceph_zap_resource_source {
    CE_ZRS_NONE = 0,
    CE_ZRS_INHERITED_UEFI = 1,
    CE_ZRS_NATIVE_XNU = 2
};
enum ceph_zap_resource_state {
    CE_ZR_NONE = 0,
    CE_ZR_COMPLETE = 1,
    CE_ZR_UNCERTAIN = 2,
    CE_ZR_FOREIGN = 3,
    CE_ZR_SIMULATED = 4,
    CE_ZR_STALE = 5
};
enum ceph_zap_resource_cache {
    CE_ZRC_NONE = 0,
    CE_ZRC_DEVICE_NGNRNE = 1,
    CE_ZRC_NORMAL_NC = 2
};
enum ceph_zap_resource_gate_state {
    CE_ZRG_NEW = 0,
    CE_ZRG_ACCEPTED = 1,
    CE_ZRG_REFUSED = 2,
    CE_ZRG_UNCERTAIN = 3
};

struct ceph_zap_resource_memory {
    uint64_t handle;
    uint64_t mdt_pa,image_pa;
    uint64_t mdt_capacity,image_capacity;
    uint64_t mdt_bytes,logical_bytes;
    uint32_t secure_pa_reachable,xpu_granule;
    uint32_t cpu_mdt_cache,cpu_image_cache,secure_cache;
    uint32_t complete,simulated,reserved;
};

struct ceph_zap_resource_crypto {
    uint32_t source,state,complete,reserved;
    uint32_t bcm_count,reserved2;
    uint64_t owner_id,lease_id,resource_epoch;
    uint64_t master,slave;
    uint64_t bcm[CE_ZAP_RESOURCE_BCM_COUNT];
    uint64_t ib_bytes,ab_bytes;
    uint64_t ledger_id,request_count,ledger_checksum;
    uint64_t ack_owner_id,ack_epoch,ack_count;
};

struct ceph_zap_resource_evidence {
    uint64_t magic;
    uint32_t version,bytes;
    uint32_t complete,simulated;
    uint32_t reserved0,reserved1;
    uint64_t owner_evidence_checksum;
    uint64_t captured_ticks;
    struct ceph_zap_resource_memory memory;
    struct ceph_zap_resource_crypto crypto;
    uint64_t checksum;
};

struct ceph_zap_resource_expectation {
    uint64_t magic;
    uint32_t version,bytes;
    uint32_t reserved0;
    uint64_t max_age_ticks;
    uint64_t crypto_ledger_id,request_count,ledger_checksum;
    uint64_t ib_bytes,ab_bytes;
};

struct ceph_zap_resource_gate {
    uint32_t version;
    uint32_t state,retained,reserved;
    struct ceph_zap_resource_evidence accepted;
} __attribute__((aligned(8)));

static inline uint64_t ceph_zap_resource_mix(uint64_t sum,uint64_t value)
{
    for(unsigned i=0;i<8;++i) {
        sum=(sum^(value&255u))*UINT64_C(1099511628211);
        value>>=8;
    }
    return sum;
}

static inline uint64_t ceph_zap_resource_checksum(const struct ceph_zap_resource_evidence *s)
{
    if(!s)return 0;
    uint64_t sum=UINT64_C(14695981039346656037);
    const uint32_t u32[]={s->version,s->bytes,s->complete,s->simulated,s->reserved0,s->reserved1,
        s->memory.secure_pa_reachable,s->memory.xpu_granule,s->memory.cpu_mdt_cache,
        s->memory.cpu_image_cache,s->memory.secure_cache,s->memory.complete,
        s->memory.simulated,s->memory.reserved,s->crypto.source,s->crypto.state,
        s->crypto.complete,s->crypto.reserved,s->crypto.bcm_count,s->crypto.reserved2};
    for(unsigned i=0;i<sizeof(u32)/sizeof(u32[0]);++i)sum=ceph_zap_resource_mix(sum,u32[i]);
    const uint64_t u64[]={s->magic,s->owner_evidence_checksum,s->captured_ticks,
        s->memory.handle,s->memory.mdt_pa,s->memory.image_pa,s->memory.mdt_capacity,
        s->memory.image_capacity,s->memory.mdt_bytes,s->memory.logical_bytes,
        s->crypto.owner_id,s->crypto.lease_id,s->crypto.resource_epoch,
        s->crypto.master,s->crypto.slave,s->crypto.ib_bytes,s->crypto.ab_bytes,
        s->crypto.ledger_id,s->crypto.request_count,s->crypto.ledger_checksum,
        s->crypto.ack_owner_id,s->crypto.ack_epoch,s->crypto.ack_count};
    for(unsigned i=0;i<sizeof(u64)/sizeof(u64[0]);++i)sum=ceph_zap_resource_mix(sum,u64[i]);
    for(unsigned i=0;i<CE_ZAP_RESOURCE_BCM_COUNT;++i)
        sum=ceph_zap_resource_mix(sum,s->crypto.bcm[i]);
    return sum;
}

static inline void ceph_zap_resource_seal(struct ceph_zap_resource_evidence *s)
{
    if(s)s->checksum=ceph_zap_resource_checksum(s);
}

static inline int ceph_zap_resource_overlap(const void *a,size_t an,const void *b,size_t bn)
{
    if(!a || !b || !an || !bn)return 0;
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && x<y+bn && y<x+an;
}

static inline int ceph_zap_resource_owner_gate(const struct ceph_owner_evidence_gate *gate,
    const struct ceph_owner_evidence_snapshot *evidence)
{
    if(!gate || !evidence || gate->version!=CE_OWNER_EVIDENCE_VERSION ||
        gate->state!=CE_OEG_ACCEPTED || gate->retained!=1 || gate->reserved)return -1;
    const struct ceph_owner_evidence_snapshot *a=&gate->accepted;
    if(a->magic!=CE_OWNER_EVIDENCE_MAGIC || a->version!=CE_OWNER_EVIDENCE_VERSION ||
        a->bytes!=sizeof(*a) || a->complete!=1 || a->simulated ||
        a->owner_domain!=CE_OD_XNU || a->writer_mask!=CE_OW_ALL ||
        a->retention!=CE_ORT_BOOT || a->reserved0 || a->ack_state!=CE_OA_COMPLETE ||
        !a->ack_count || !a->owner_id || !a->lease_id || !a->resource_epoch ||
        !a->captured_ticks || a->ack_owner_id!=a->owner_id ||
        a->ack_epoch!=a->resource_epoch || a->rpmh.owner_id!=a->owner_id ||
        a->rpmh.resource_epoch!=a->resource_epoch || a->rpmh.state!=CE_OR_COMPLETE ||
        a->rpmh.complete!=1 || a->sid_mask[0]!=CE_OWNER_TBU0_LOCAL_SID_MASK ||
        a->sid_mask[1]!=CE_OWNER_TBU1_LOCAL_SID_MASK ||
        a->tbu[0].sid_start+a->tbu[0].sid_count!=a->tbu[1].sid_start ||
        a->tbu[1].sid_start+a->tbu[1].sid_count!=0x800 ||
        a->checksum!=ceph_owner_evidence_checksum(a))return -1;
    for(unsigned i=0;i<CE_OWNER_TBU_COUNT;++i)
        if(!ceph_owner_evidence_tbu_valid(&a->tbu[i],i,a->lease_id))return -1;
    return evidence->checksum==a->checksum && evidence->owner_id==a->owner_id &&
        evidence->lease_id==a->lease_id && evidence->resource_epoch==a->resource_epoch &&
        evidence->ack_owner_id==a->ack_owner_id && evidence->ack_epoch==a->ack_epoch &&
        evidence->ack_count==a->ack_count && evidence->rpmh.ledger_id==a->rpmh.ledger_id &&
        evidence->rpmh.request_count==a->rpmh.request_count &&
        evidence->rpmh.checksum==a->rpmh.checksum?0:-1;
}

static inline int ceph_zap_resource_uncertain(const struct ceph_zap_resource_evidence *s,
    const struct ceph_owner_evidence_gate *owner_gate,
    const struct ceph_owner_evidence_snapshot *owner)
{
    if(!s || !owner)return 1;
    if(owner_gate) {
        if(owner_gate->state==CE_OEG_REFUSED)return 0;
        if(owner_gate->state==CE_OEG_UNCERTAIN ||
            (owner_gate->state==CE_OEG_ACCEPTED && owner_gate->retained))return 1;
    }
    return owner->complete!=1 || owner->simulated || owner->ack_state==CE_OA_UNCERTAIN ||
        owner->ack_state==CE_OA_SIMULATED || owner->retention==CE_ORT_UNCERTAIN ||
        owner->rpmh.state==CE_OR_UNCERTAIN || owner->rpmh.state==CE_OR_FOREIGN ||
        owner->rpmh.state==CE_OR_STALE || owner->rpmh.state==CE_OR_SIMULATED ||
        s->complete!=1 || s->simulated || s->memory.complete!=1 || s->memory.simulated ||
        s->crypto.complete!=1 || s->crypto.state==CE_ZR_UNCERTAIN ||
        s->crypto.state==CE_ZR_FOREIGN || s->crypto.state==CE_ZR_SIMULATED ||
        s->crypto.state==CE_ZR_STALE || !s->captured_ticks ||
        s->checksum!=ceph_zap_resource_checksum(s);
}

static inline int ceph_zap_resource_valid(const struct ceph_zap_resource_evidence *s,
    const struct ceph_zap_claim *claim,const struct ceph_owner_evidence_gate *owner_gate,
    const struct ceph_owner_evidence_snapshot *owner,
    const struct ceph_zap_resource_expectation *expected,uint64_t now_ticks)
{
    if(!s || !claim || !expected || ceph_zap_resource_owner_gate(owner_gate,owner) ||
        expected->magic!=CE_ZAP_RESOURCE_EXPECTATION_MAGIC ||
        expected->version!=CE_ZAP_RESOURCE_VERSION || expected->bytes!=sizeof(*expected) ||
        expected->reserved0 || !expected->max_age_ticks || !expected->crypto_ledger_id ||
        !expected->request_count || !expected->ledger_checksum || !expected->ib_bytes ||
        !expected->ab_bytes)return -1;
    if(sizeof(*s)!=CE_ZAP_RESOURCE_EVIDENCE_BYTES ||
        sizeof(s->memory)!=CE_ZAP_RESOURCE_MEMORY_BYTES ||
        sizeof(s->crypto)!=CE_ZAP_RESOURCE_CRYPTO_BYTES ||
        s->magic!=CE_ZAP_RESOURCE_MAGIC || s->version!=CE_ZAP_RESOURCE_VERSION ||
        s->bytes!=sizeof(*s) || s->complete!=1 || s->simulated || s->reserved0 || s->reserved1 ||
        s->owner_evidence_checksum!=owner->checksum || !s->captured_ticks ||
        s->captured_ticks<owner->captured_ticks || owner->captured_ticks>now_ticks ||
        now_ticks-owner->captured_ticks>expected->max_age_ticks ||
        s->captured_ticks>now_ticks || now_ticks>UINT64_MAX-expected->max_age_ticks ||
        now_ticks-s->captured_ticks>expected->max_age_ticks ||
        s->checksum!=ceph_zap_resource_checksum(s))return -1;

    const struct ceph_zap_resource_memory *m=&s->memory;
    const struct ceph_zap_memory_view *v=&claim->memory;
    if(!m->handle || m->handle!=claim->handle || m->mdt_pa!=v->mdt_pa ||
        m->image_pa!=v->image_pa || m->mdt_capacity!=v->mdt_capacity ||
        m->image_capacity!=v->image_capacity || m->mdt_bytes!=v->mdt_bytes ||
        m->logical_bytes!=v->logical_bytes || v->entry_offset ||
        !m->mdt_pa || !m->image_pa || m->mdt_capacity!=16384 ||
        m->image_capacity!=1048576 || m->mdt_bytes!=6860 || m->logical_bytes!=4096 ||
        m->secure_pa_reachable!=1 || m->complete!=1 || m->simulated || m->reserved ||
        m->cpu_mdt_cache!=CE_ZRC_DEVICE_NGNRNE || m->cpu_image_cache!=CE_ZRC_NORMAL_NC ||
        m->secure_cache!=CE_ZRC_DEVICE_NGNRNE ||
        (m->xpu_granule!=4096 && m->xpu_granule!=16384) ||
        (m->mdt_pa&(m->xpu_granule-1)) || (m->image_pa&(m->xpu_granule-1)) ||
        (m->mdt_capacity&(m->xpu_granule-1)) || (m->image_capacity&(m->xpu_granule-1)) ||
        m->mdt_pa>(1ULL<<40)-m->mdt_capacity || m->image_pa>(1ULL<<40)-m->image_capacity ||
        (m->mdt_pa<m->image_pa+m->image_capacity &&
            m->image_pa<m->mdt_pa+m->mdt_capacity))return -1;

    const struct ceph_zap_resource_crypto *c=&s->crypto;
    const uint64_t bcms[CE_ZAP_RESOURCE_BCM_COUNT]={CE_ZAP_RESOURCE_BCM_CE0,
        CE_ZAP_RESOURCE_BCM_SN11,CE_ZAP_RESOURCE_BCM_SN0,CE_ZAP_RESOURCE_BCM_SH0,
        CE_ZAP_RESOURCE_BCM_MC0,CE_ZAP_RESOURCE_BCM_ACV};
    if(c->reserved || c->reserved2 || (c->source!=CE_ZRS_INHERITED_UEFI &&
        c->source!=CE_ZRS_NATIVE_XNU) || c->state!=CE_ZR_COMPLETE || c->complete!=1 ||
        c->owner_id!=owner->owner_id || c->lease_id!=owner->lease_id ||
        c->resource_epoch!=owner->resource_epoch || c->master!=CE_ZAP_RESOURCE_CRYPTO_MASTER ||
        c->slave!=CE_ZAP_RESOURCE_CRYPTO_SLAVE || c->bcm_count!=CE_ZAP_RESOURCE_BCM_COUNT ||
        c->ib_bytes!=expected->ib_bytes || c->ab_bytes!=expected->ab_bytes ||
        c->ledger_id!=expected->crypto_ledger_id || c->request_count!=expected->request_count ||
        c->ledger_checksum!=expected->ledger_checksum ||
        c->ack_owner_id!=owner->ack_owner_id || c->ack_epoch!=owner->ack_epoch ||
        c->ack_count!=owner->ack_count)return -1;
    for(unsigned i=0;i<CE_ZAP_RESOURCE_BCM_COUNT;++i)
        if(c->bcm[i]!=bcms[i])return -1;
    return 0;
}

static inline int ceph_zap_resource_same(const struct ceph_zap_resource_evidence *a,
    const struct ceph_zap_resource_evidence *b)
{
    if(!a || !b)return 0;
    return a->checksum==b->checksum && a->owner_evidence_checksum==b->owner_evidence_checksum &&
        a->captured_ticks==b->captured_ticks && a->memory.handle==b->memory.handle &&
        a->memory.mdt_pa==b->memory.mdt_pa && a->memory.image_pa==b->memory.image_pa &&
        a->crypto.owner_id==b->crypto.owner_id && a->crypto.lease_id==b->crypto.lease_id &&
        a->crypto.resource_epoch==b->crypto.resource_epoch &&
        a->crypto.ledger_id==b->crypto.ledger_id &&
        a->crypto.request_count==b->crypto.request_count &&
        a->crypto.ledger_checksum==b->crypto.ledger_checksum &&
        a->crypto.ack_epoch==b->crypto.ack_epoch && a->crypto.ack_count==b->crypto.ack_count;
}

/* The owner gate is mandatory. A self-sealed record, an uncertain/refused
 * owner or an output alias cannot create an accepted resource gate. */
static inline int ceph_zap_resource_admit(struct ceph_zap_resource_gate *gate,
    const struct ceph_zap_claim *claim,const struct ceph_zap_resource_evidence *snapshot,
    const struct ceph_owner_evidence_gate *owner_gate,
    const struct ceph_owner_evidence_snapshot *owner,
    const struct ceph_zap_resource_expectation *expected,uint64_t now_ticks,
    struct ceph_zap_resource_evidence *out)
{
    if(sizeof(*gate)!=CE_ZAP_RESOURCE_GATE_BYTES ||
        sizeof(*expected)!=CE_ZAP_RESOURCE_EXPECTATION_BYTES ||
        !gate || !claim || !snapshot || !owner_gate || !owner || !expected || !out ||
        ((uintptr_t)gate&7u) || ((uintptr_t)out&7u))return -1;
    const void *bases[]={gate,claim,snapshot,owner_gate,owner,expected,out};
    const size_t bytes[]={sizeof(*gate),sizeof(*claim),sizeof(*snapshot),sizeof(*owner_gate),
        sizeof(*owner),sizeof(*expected),sizeof(*out)};
    for(unsigned i=0;i<7;++i)for(unsigned j=i+1;j<7;++j)
        if(ceph_zap_resource_overlap(bases[i],bytes[i],bases[j],bytes[j]))return -1;
    if(gate->state!=CE_ZRG_NEW)return -1;
    struct ceph_zap_resource_evidence candidate=*snapshot;
    if(ceph_zap_resource_owner_gate(owner_gate,owner) ||
        ceph_zap_resource_valid(&candidate,claim,owner_gate,owner,expected,now_ticks)) {
        int uncertain=ceph_zap_resource_uncertain(&candidate,owner_gate,owner);
        gate->version=CE_ZAP_RESOURCE_VERSION;
        gate->state=uncertain?CE_ZRG_UNCERTAIN:CE_ZRG_REFUSED;
        gate->retained=(uint32_t)uncertain;
        return -1;
    }
    gate->version=CE_ZAP_RESOURCE_VERSION;gate->accepted=candidate;
    gate->state=CE_ZRG_ACCEPTED;gate->retained=1;
    *out=candidate;return 0;
}
#endif
