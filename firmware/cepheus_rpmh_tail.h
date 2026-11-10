/* Exit-tail software requests. Never hardware ACK or exclusive resource ownership. */
#ifndef CEPHEUS_RPMH_TAIL_H
#define CEPHEUS_RPMH_TAIL_H
#include "cepheus_rpmh_snapshot.h"

#define CE_RPMH_TAIL_MAGIC UINT64_C(0x314c494154485052)
#define CE_RPMH_TAIL_BYTES 1080u
#define CE_RPMH_TAIL_BOUNDARIES 15u
struct ceph_rpmh_tail {
    uint64_t magic;
    uint32_t version, bytes, complete;
    int32_t error;
    uint32_t boundaries, reads;
    uint64_t allocation_serial, begin_ticks, end_ticks, checksum;
    struct ceph_rpmh_snapshot requests;
};
_Static_assert(sizeof(struct ceph_rpmh_tail)==CE_RPMH_TAIL_BYTES,"RPMh exit-tail ABI");
_Static_assert(offsetof(struct ceph_rpmh_tail,checksum)==56,"RPMh tail checksum ABI");

static inline uint64_t ceph_rpmh_tail_checksum(const struct ceph_rpmh_tail *s)
{
    const uint8_t *p=(const uint8_t *)s;
    uint64_t sum=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(*s);++i)
        sum=(sum^(i>=56 && i<64?0:p[i]))*UINT64_C(1099511628211);
    return sum;
}

/* Exactly this local, diagnostic boundary. No flag encodes hardware ownership. */
static inline int ceph_rpmh_tail_valid(const struct ceph_rpmh_tail *s)
{
    if(!s || s->magic!=CE_RPMH_TAIL_MAGIC || s->version!=1 || s->bytes!=sizeof(*s) ||
        s->complete!=1 || s->error>0 || s->error<CE_RPMH_SNAP_MISSING ||
        (s->boundaries&~CE_RPMH_TAIL_BOUNDARIES) || s->reads>65536 ||
        s->checksum!=ceph_rpmh_tail_checksum(s))return -1;
    const struct ceph_rpmh_snapshot *r=&s->requests;
    if(r->phase!=2 || r->error!=s->error || (r->identity_mask&~7u) ||
        r->begin_ticks!=s->begin_ticks || r->end_ticks!=s->end_ticks ||
        r->checksum!=ceph_rpmh_snapshot_checksum(r))return -1;
    struct ceph_rpmh_snapshot pre=*r;
    pre.phase=1;
    if(!pre.error)pre.identity_mask=15;
    pre.checksum=ceph_rpmh_snapshot_checksum(&pre);
    if(ceph_rpmh_snapshot_valid(&pre))return -1;
    if(!s->error && (s->boundaries!=CE_RPMH_TAIL_BOUNDARIES || !s->allocation_serial ||
        !s->reads || s->end_ticks<s->begin_ticks || r->identity_mask!=7))return -1;
    return 0;
}
#endif
