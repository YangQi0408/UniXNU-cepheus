/* Preserve transport errors separately from the core's phase2 diagnostic. */
#ifndef CEPHEUS_RPMH_TAIL_HANDOFF_H
#define CEPHEUS_RPMH_TAIL_HANDOFF_H
#include "cepheus_rpmh_tail.h"
#define CE_RPMH_TAIL_HANDOFF_PROPERTY "cepheus-rpmh-tail"
#define CE_RPMH_TAIL_HANDOFF_MAGIC UINT64_C(0x31464f4854504852)
#define CE_RPMH_TAIL_HANDOFF_BYTES 1128u
enum { CE_TAIL_PREPARED=1, CE_TAIL_BOUND=2, CE_TAIL_COPIED=4, CE_TAIL_EBS=8 };
struct ceph_rpmh_tail_handoff {
    uint64_t magic;
    uint32_t version, bytes, flags, ebs_attempts;
    uint64_t bind_status, copy_status, checksum;
    struct ceph_rpmh_tail record;
};
_Static_assert(sizeof(struct ceph_rpmh_tail_handoff)==CE_RPMH_TAIL_HANDOFF_BYTES,"RPMh tail handoff ABI");
_Static_assert(offsetof(struct ceph_rpmh_tail_handoff,checksum)==40,"RPMh tail handoff checksum ABI");
static inline int ceph_rpmh_tail_profile(const char *line)
{
    unsigned profiles=0, ceph=0, exact=0;
    if(!line)return 0;
    for(unsigned i=0;line[i];) {
        while(line[i]==' ' || line[i]=='\t')++i;
        unsigned start=i;
        while(line[i] && line[i]!=' ' && line[i]!='\t')++i;
        unsigned n=i-start;
        const char *p=line+start, key[]="cephrpmhtail";
        if(n<4 || p[0]!='c' || p[1]!='e' || p[2]!='p' || p[3]!='h')continue;
        ++ceph;
        unsigned j=0;
        while(j<sizeof(key)-1 && j<n && p[j]==key[j])++j;
        if(j!=sizeof(key)-1)continue;
        ++profiles;
        if(n==sizeof(key)+1 && p[j]=='=' && p[j+1]=='1')++exact;
    }
    return !profiles?0:(profiles==1 && ceph==1 && exact==1?1:-1);
}
static inline uint64_t ceph_rpmh_tail_handoff_checksum(const struct ceph_rpmh_tail_handoff *h)
{
    const uint8_t *p=(const uint8_t *)h;
    uint64_t sum=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(*h);++i)
        sum=(sum^(i>=40 && i<48?0:p[i]))*UINT64_C(1099511628211);
    return sum;
}
static inline int ceph_rpmh_tail_handoff_valid(const struct ceph_rpmh_tail_handoff *h)
{
    if(!h || h->magic!=CE_RPMH_TAIL_HANDOFF_MAGIC || h->version!=1 || h->bytes!=sizeof(*h) ||
        (h->flags&~15u) || (h->flags&(CE_TAIL_PREPARED|CE_TAIL_EBS))!=(CE_TAIL_PREPARED|CE_TAIL_EBS) ||
        !h->ebs_attempts || h->ebs_attempts>3 || h->checksum!=ceph_rpmh_tail_handoff_checksum(h))return -1;
    if((h->flags&CE_TAIL_BOUND)!=0 ? h->bind_status!=0 : h->bind_status==0)return -1;
    if(h->flags&CE_TAIL_COPIED) {
        if(!(h->flags&CE_TAIL_BOUND) || h->copy_status || ceph_rpmh_tail_valid(&h->record) ||
            h->record.requests.ebs_attempts!=h->ebs_attempts)return -1;
    } else {
        if(!h->copy_status)return -1;
        const uint8_t *p=(const uint8_t *)&h->record;
        for(unsigned i=0;i<sizeof(h->record);++i)if(p[i])return -1;
    }
    return 0;
}
#endif
