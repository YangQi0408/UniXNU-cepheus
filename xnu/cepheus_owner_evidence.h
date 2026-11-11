/* Fail-closed resource-owner evidence. Pure copied records only: this header
 * performs no MMIO, SMC, firmware call, idle transition or GPU submission. */
#ifndef CEPHEUS_OWNER_EVIDENCE_H
#define CEPHEUS_OWNER_EVIDENCE_H
#include <stddef.h>
#include <stdint.h>

#define CE_OWNER_EVIDENCE_MAGIC UINT64_C(0x3145564f4e574e4f)
#define CE_OWNER_EXPECTATION_MAGIC UINT64_C(0x3150584f4e574e4f)
#define CE_OWNER_EVIDENCE_VERSION 1u
#define CE_OWNER_TBU_COUNT 2u

#define CE_OWNER_TBU0_BASE UINT64_C(0x2cc5000)
#define CE_OWNER_TBU1_BASE UINT64_C(0x2cc9000)
#define CE_OWNER_TBU_BYTES UINT64_C(0x1000)
#define CE_OWNER_TBU0_STATUS_REG UINT64_C(0x2cc2200)
#define CE_OWNER_TBU1_STATUS_REG UINT64_C(0x2cc2208)
/* These are local SID bits inside each 0x400-entry TBU window. The GX user
 * streams are SIDs {0,1,0x400,0x401}; the same two local bits are therefore
 * required in both TBU windows. They are not absolute SID numbers. */
#define CE_OWNER_TBU0_LOCAL_SID_MASK ((UINT64_C(1)<<0)|(UINT64_C(1)<<1))
#define CE_OWNER_TBU1_LOCAL_SID_MASK ((UINT64_C(1)<<0)|(UINT64_C(1)<<1))

enum ceph_owner_domain { CE_OD_NONE=0, CE_OD_XNU=1 };
enum ceph_owner_writer_bits {
    CE_OW_GX=1u<<0, CE_OW_TBU=1u<<1, CE_OW_CP=1u<<2,
    CE_OW_CPU_DEVICE=1u<<3, CE_OW_RPMH=1u<<4,
    CE_OW_ALL=CE_OW_GX|CE_OW_TBU|CE_OW_CP|CE_OW_CPU_DEVICE|CE_OW_RPMH
};
enum ceph_owner_tbu_readiness {
    CE_OTR_NONE=0, CE_OTR_OWNER_CONFIRMED=1, CE_OTR_ECATS_DEBUG=2
};
enum ceph_owner_ack_state {
    CE_OA_NONE=0, CE_OA_COMPLETE=1, CE_OA_UNCERTAIN=2, CE_OA_SIMULATED=3
};
enum ceph_owner_rpmh_source {
    CE_ORS_NONE=0, CE_ORS_INHERITED_UEFI=1, CE_ORS_NATIVE_XNU=2
};
enum ceph_owner_rpmh_state {
    CE_OR_NONE=0, CE_OR_COMPLETE=1, CE_OR_UNCERTAIN=2,
    CE_OR_FOREIGN=3, CE_OR_SIMULATED=4, CE_OR_STALE=5
};
enum ceph_owner_retention { CE_ORT_NONE=0, CE_ORT_BOOT=1, CE_ORT_UNCERTAIN=2 };
enum ceph_owner_evidence_gate_state {
    CE_OEG_NEW=0, CE_OEG_ACCEPTED=1, CE_OEG_REFUSED=2, CE_OEG_UNCERTAIN=3
};

struct ceph_owner_tbu_evidence {
    uint64_t base, bytes;
    uint32_t sid_start, sid_count;
    uint32_t readiness_source, reserved0;
    uint64_t readiness_token;
    uint64_t debug_status_reg;
    uint32_t debug_status, reserved1;
};
struct ceph_owner_rpmh_evidence {
    uint32_t source, state, complete, reserved;
    uint64_t owner_id, ledger_id, resource_epoch, request_count, checksum;
};
struct ceph_owner_evidence_snapshot {
    uint64_t magic;
    uint32_t version, bytes;
    uint32_t complete, simulated;
    uint32_t owner_domain, writer_mask;
    uint32_t ack_state, ack_count;
    uint32_t retention, reserved0;
    uint64_t owner_id, lease_id, resource_epoch, captured_ticks;
    uint64_t ack_epoch, ack_owner_id;
    uint64_t sid_mask[2];
    struct ceph_owner_tbu_evidence tbu[CE_OWNER_TBU_COUNT];
    struct ceph_owner_rpmh_evidence rpmh;
    uint64_t checksum;
};
struct ceph_owner_evidence_expectation {
    uint64_t magic;
    uint32_t version, bytes;
    uint32_t owner_domain, writer_mask;
    uint32_t rpmh_source, ack_count;
    uint64_t owner_id, lease_id, resource_epoch;
    uint64_t now_ticks, max_age_ticks;
    uint64_t rpmh_ledger_id, rpmh_request_count, rpmh_checksum;
};
struct ceph_owner_evidence_gate {
    _Alignas(8) uint32_t version;
    uint32_t state, retained, reserved;
    struct ceph_owner_evidence_snapshot accepted;
};

static inline uint64_t ceph_owner_evidence_mix(uint64_t sum,uint64_t value)
{
    for(unsigned i=0;i<8;++i) {
        sum=(sum^(value&255u))*UINT64_C(1099511628211);
        value>>=8;
    }
    return sum;
}
static inline uint64_t ceph_owner_evidence_checksum(const struct ceph_owner_evidence_snapshot *s)
{
    if(!s)return 0;
    uint64_t sum=UINT64_C(14695981039346656037);
    const uint32_t u32[]={s->version,s->bytes,s->complete,s->simulated,s->owner_domain,
        s->writer_mask,s->ack_state,s->ack_count,s->retention,s->reserved0};
    for(unsigned i=0;i<sizeof(u32)/sizeof(u32[0]);++i)sum=ceph_owner_evidence_mix(sum,u32[i]);
    const uint64_t u64[]={s->magic,s->owner_id,s->lease_id,s->resource_epoch,s->captured_ticks,
        s->ack_epoch,s->ack_owner_id,s->sid_mask[0],s->sid_mask[1]};
    for(unsigned i=0;i<sizeof(u64)/sizeof(u64[0]);++i)sum=ceph_owner_evidence_mix(sum,u64[i]);
    for(unsigned i=0;i<CE_OWNER_TBU_COUNT;++i) {
        const struct ceph_owner_tbu_evidence *t=&s->tbu[i];
        const uint64_t values[]={t->base,t->bytes,t->sid_start,t->sid_count,t->readiness_source,
            t->reserved0,t->readiness_token,t->debug_status_reg,t->debug_status,t->reserved1};
        for(unsigned n=0;n<sizeof(values)/sizeof(values[0]);++n)sum=ceph_owner_evidence_mix(sum,values[n]);
    }
    const struct ceph_owner_rpmh_evidence *r=&s->rpmh;
    const uint64_t rpmh[]={r->source,r->state,r->complete,r->reserved,r->owner_id,r->ledger_id,
        r->resource_epoch,r->request_count,r->checksum};
    for(unsigned i=0;i<sizeof(rpmh)/sizeof(rpmh[0]);++i)sum=ceph_owner_evidence_mix(sum,rpmh[i]);
    return sum;
}
static inline void ceph_owner_evidence_seal(struct ceph_owner_evidence_snapshot *s)
{
    if(s)s->checksum=ceph_owner_evidence_checksum(s);
}
static inline int ceph_owner_evidence_overlap(const void *a,size_t an,const void *b,size_t bn)
{
    if(!a || !b || !an || !bn)return 0;
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && x<y+bn && y<x+an;
}
static inline int ceph_owner_evidence_tbu_valid(const struct ceph_owner_tbu_evidence *t,
    unsigned index,uint64_t lease_id)
{
    if(!t || t->reserved0 || t->reserved1 || t->readiness_source!=CE_OTR_OWNER_CONFIRMED ||
        t->readiness_token!=lease_id || !t->readiness_token)return 0;
    if(index==0)return t->base==CE_OWNER_TBU0_BASE && t->bytes==CE_OWNER_TBU_BYTES &&
        t->sid_start==0 && t->sid_count==0x400 && t->debug_status_reg==CE_OWNER_TBU0_STATUS_REG;
    return index==1 && t->base==CE_OWNER_TBU1_BASE && t->bytes==CE_OWNER_TBU_BYTES &&
        t->sid_start==0x400 && t->sid_count==0x400 && t->debug_status_reg==CE_OWNER_TBU1_STATUS_REG;
}
static inline int ceph_owner_evidence_matches(const struct ceph_owner_evidence_snapshot *s,
    const struct ceph_owner_evidence_expectation *e)
{
    if(!s || !e || e->magic!=CE_OWNER_EXPECTATION_MAGIC || e->version!=CE_OWNER_EVIDENCE_VERSION ||
        e->bytes!=sizeof(*e) || e->owner_domain!=CE_OD_XNU || e->writer_mask!=CE_OW_ALL ||
        !e->owner_id || !e->lease_id || !e->resource_epoch || !e->max_age_ticks ||
        !e->ack_count || (e->rpmh_source!=CE_ORS_INHERITED_UEFI &&
        e->rpmh_source!=CE_ORS_NATIVE_XNU) || !e->rpmh_ledger_id || !e->rpmh_request_count ||
        !e->rpmh_checksum)return 0;
    if(s->magic!=CE_OWNER_EVIDENCE_MAGIC || s->version!=CE_OWNER_EVIDENCE_VERSION ||
        s->bytes!=sizeof(*s) || s->complete!=1 || s->simulated || s->owner_domain!=e->owner_domain ||
        s->writer_mask!=e->writer_mask || s->retention!=CE_ORT_BOOT || s->reserved0 ||
        s->owner_id!=e->owner_id || s->lease_id!=e->lease_id ||
        s->resource_epoch!=e->resource_epoch || s->ack_state!=CE_OA_COMPLETE ||
        s->ack_count!=e->ack_count || s->ack_owner_id!=e->owner_id ||
        s->ack_epoch!=e->resource_epoch || s->sid_mask[0]!=CE_OWNER_TBU0_LOCAL_SID_MASK ||
        s->sid_mask[1]!=CE_OWNER_TBU1_LOCAL_SID_MASK || !s->captured_ticks ||
        s->captured_ticks>e->now_ticks ||
        e->now_ticks-s->captured_ticks>e->max_age_ticks)return 0;
    for(unsigned i=0;i<CE_OWNER_TBU_COUNT;++i)
        if(!ceph_owner_evidence_tbu_valid(&s->tbu[i],i,e->lease_id))return 0;
    if(s->tbu[0].sid_start+s->tbu[0].sid_count!=s->tbu[1].sid_start ||
        s->tbu[1].sid_start+s->tbu[1].sid_count!=0x800)return 0;
    const struct ceph_owner_rpmh_evidence *r=&s->rpmh;
    return r->source==e->rpmh_source && r->state==CE_OR_COMPLETE && r->complete==1 &&
        !r->reserved && r->owner_id==e->owner_id && r->ledger_id==e->rpmh_ledger_id &&
        r->resource_epoch==e->resource_epoch && r->request_count==e->rpmh_request_count &&
        r->checksum==e->rpmh_checksum && s->checksum==ceph_owner_evidence_checksum(s);
}
/* These states mean the copied record cannot distinguish a live owner from a
 * stale or foreign writer. Keep the gate retained even when admission fails;
 * a later boot must not turn uncertainty into permission to release resources. */
static inline int ceph_owner_evidence_uncertain(const struct ceph_owner_evidence_snapshot *s)
{
    if(!s)return 0;
    return s->complete!=1 || s->simulated || s->ack_state==CE_OA_UNCERTAIN ||
        s->ack_state==CE_OA_SIMULATED || s->rpmh.state==CE_OR_SIMULATED ||
        s->retention==CE_ORT_UNCERTAIN || s->rpmh.state==CE_OR_UNCERTAIN ||
        s->rpmh.state==CE_OR_FOREIGN || s->rpmh.state==CE_OR_STALE ||
        s->captured_ticks==0 || s->checksum!=ceph_owner_evidence_checksum(s);
}
/* One attempt, permanent terminal state. A real copied snapshot is consumed
 * even when refused; uncertainty retains the gate and forbids retry for boot. */
static inline int ceph_owner_evidence_admit(struct ceph_owner_evidence_gate *gate,
    const struct ceph_owner_evidence_snapshot *snapshot,
    const struct ceph_owner_evidence_expectation *expected,
    struct ceph_owner_evidence_snapshot *out)
{
    if(!gate || !snapshot || !expected || !out || ((uintptr_t)gate&7u) ||
        ((uintptr_t)snapshot&7u) || ((uintptr_t)expected&7u) || ((uintptr_t)out&7u))return -1;
    const void *bases[]={gate,snapshot,expected,out};
    const size_t bytes[]={sizeof(*gate),sizeof(*snapshot),sizeof(*expected),sizeof(*out)};
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)
        if(ceph_owner_evidence_overlap(bases[i],bytes[i],bases[j],bytes[j]))return -1;
    if(gate->state!=CE_OEG_NEW)return -1;
    struct ceph_owner_evidence_snapshot candidate=*snapshot;
    struct ceph_owner_evidence_expectation want=*expected;
    if(gate->version && gate->version!=CE_OWNER_EVIDENCE_VERSION) {
        gate->state=CE_OEG_REFUSED;return -1;
    }
    gate->version=CE_OWNER_EVIDENCE_VERSION;
    if(!ceph_owner_evidence_matches(&candidate,&want)) {
        int uncertain=ceph_owner_evidence_uncertain(&candidate);
        gate->state=uncertain?CE_OEG_UNCERTAIN:CE_OEG_REFUSED;
        gate->retained=(uint32_t)uncertain;
        return -1;
    }
    gate->accepted=candidate;gate->state=CE_OEG_ACCEPTED;gate->retained=1;
    *out=candidate;return 0;
}
#endif
