/* Diagnostic boot transcript only. Never admits PAS loading or resource use. */
#ifndef CEPHEUS_SCM_HANDOFF_H
#define CEPHEUS_SCM_HANDOFF_H
#include <stddef.h>
#include <stdint.h>
#include "cepheus_scm_discovery.h"

#define CE_SCM_HANDOFF_PROPERTY "cepheus-scm-handoff"
#define CE_SCM_HANDOFF_MAGIC UINT64_C(0x31464f484d435343)
#define CE_SCM_HANDOFF_BYTES 2656u

/* 0: unrelated boot; 1: exact isolated profile; -1: malformed/mixed pair. */
static inline int ceph_scm_pair_profile(const char *line)
{
    unsigned pairs=0, ceph=0, exact=0;
    if(!line)return 0;
    for(unsigned i=0;line[i];) {
        while(line[i]==' ' || line[i]=='\t')++i;
        unsigned start=i;
        while(line[i] && line[i]!=' ' && line[i]!='\t')++i;
        unsigned n=i-start;
        const char *p=line+start;
        if(n<4 || p[0]!='c' || p[1]!='e' || p[2]!='p' || p[3]!='h')continue;
        ++ceph;
        const char key[]="cephscmpair";
        unsigned j=0;
        while(j<sizeof(key)-1 && j<n && p[j]==key[j])++j;
        if(j!=sizeof(key)-1)continue;
        ++pairs;
        if(n==sizeof(key)+1 && p[j]=='=' && p[j+1]=='1')++exact;
    }
    return !pairs?0:(pairs==1 && ceph==1 && exact==1?1:-1);
}

struct ceph_scm_handoff_call {
    struct ceph_scm_regs in, out;
    uint64_t current_el, mpidr, ticks;
    int32_t invoke_error;
    uint32_t complete;
};
struct ceph_scm_handoff {
    uint64_t magic;
    uint32_t version, bytes, complete, count;
    int32_t error;
    uint32_t convention, available_mask, supported, terminal, reserved;
    uint64_t current_el, mpidr, begin_ticks, end_ticks;
    uint32_t ebs_attempts, ebs_done;
    uint64_t checksum;
    struct ceph_scm_handoff_call calls[CE_SCM_QUERY_LIMIT];
};
_Static_assert(sizeof(struct ceph_scm_handoff_call)==160,"SCM record ABI");
_Static_assert(sizeof(struct ceph_scm_handoff)==CE_SCM_HANDOFF_BYTES,"SCM handoff ABI");
_Static_assert(offsetof(struct ceph_scm_handoff,checksum)==88,"SCM checksum ABI");

static inline uint64_t ceph_scm_handoff_checksum(const struct ceph_scm_handoff *h)
{
    const uint8_t *p=(const uint8_t *)h;
    uint64_t sum=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(*h);++i) {
        uint8_t b=i>=88 && i<96?0:p[i];
        sum=(sum^b)*UINT64_C(1099511628211);
    }
    return sum;
}

struct ceph_scm_handoff_replay {
    const struct ceph_scm_handoff *handoff;
    unsigned next;
    int mismatch;
};
static inline int ceph_scm_handoff_lease(void *context)
{ (void)context;return 0; }
static inline int ceph_scm_handoff_invoke(void *context,struct ceph_scm_regs *regs)
{
    struct ceph_scm_handoff_replay *s=context;
    if(s->next>=s->handoff->count){s->mismatch=1;return -1;}
    const struct ceph_scm_handoff_call *r=&s->handoff->calls[s->next++];
    for(unsigned i=0;i<8;++i)if(regs->x[i]!=r->in.x[i]){s->mismatch=1;return -1;}
    *regs=r->out;
    return r->invoke_error;
}
/* Copy the ADT property into an aligned object before validation. A checksum
 * detects damage, not an authenticated firmware identity or a trusted lease. */
static inline int ceph_scm_handoff_valid(const struct ceph_scm_handoff *h)
{
    if(!h || h->magic!=CE_SCM_HANDOFF_MAGIC || h->version!=1 || h->bytes!=sizeof(*h) ||
        h->complete!=1 || h->terminal!=1 || h->reserved || !h->count ||
        h->count>CE_SCM_QUERY_LIMIT || h->convention>CE_SCM_ARMV8_64 ||
        h->supported>1 || (h->available_mask&~0x1fu) ||
        (h->error!=0 && h->error!=-1) || h->current_el!=4 ||
        h->ebs_done!=1 || !h->ebs_attempts || h->ebs_attempts>3 ||
        h->checksum!=ceph_scm_handoff_checksum(h))return -1;
    for(unsigned i=0;i<CE_SCM_QUERY_LIMIT;++i) {
        const struct ceph_scm_handoff_call *r=&h->calls[i];
        if(i<h->count) {
            if(r->complete!=1 || r->current_el!=4)return -1;
        } else {
            const uint8_t *p=(const uint8_t *)r;
            for(unsigned j=0;j<sizeof(*r);++j)if(p[j])return -1;
        }
    }
    /* Replay the production policy in RAM to reject invented or reordered
     * calls and summary mismatches. This never invokes an SMC. */
    struct ceph_scm_handoff_replay replay={h,0,0};
    struct ceph_scm_discovery result={0};
    struct ceph_scm_discovery_ops ops={&replay,ceph_scm_handoff_lease,ceph_scm_handoff_invoke};
    int error=ceph_scm_discover_pas13(&result,&ops);
    if(replay.mismatch || replay.next!=h->count || result.calls!=h->count ||
        error!=h->error || (unsigned)result.convention!=h->convention ||
        result.available_mask!=h->available_mask || result.supported!=h->supported)return -1;
    return 0;
}
#endif
