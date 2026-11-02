/* SM8150 BCM metadata and bandwidth vote encoder. No RPMh write operations.
 * Source: msm_bus_fabric_rpmh.c bcm_db/tcs_cmd_gen and arb_rpmh.c division.
 * Input AB/IB are bytes/sec at a specific node AFTER route aggregation/QoS.
 * This is not a generic interconnect aggregator or a licence to replace votes.
 */
#ifndef CEPHEUS_BCM_H
#define CEPHEUS_BCM_H
#include "cepheus_arc.h"
struct ceph_bcm_resource {uint32_t address,unit;uint16_t width;uint8_t vcd;};
static inline int ceph_bcm_lookup(const struct ceph_arc_db *db,const char *name,struct ceph_bcm_resource *out)
{
    if(!out)return -1;
    *out=(struct ceph_bcm_resource){0};
    if(!db || !db->read32 || db->bytes<144 || db->bytes>0x100000 || (db->bytes&3) || !name || !name[0])return -1;
    uint32_t key[2]={0};unsigned n=0;
    for(;n<8 && name[n];++n)key[n/4]|=(uint32_t)(uint8_t)name[n]<<(8*(n%4));
    if(n==8 && name[8])return -1;
    if(db->read32(db->ctx,0)!=1 || db->read32(db->ctx,4)!=0x0c0330db || (db->read32(db->ctx,140)&1))return -1;
    struct ceph_bcm_resource found={0};unsigned matches=0;
    for(unsigned i=0;i<8;++i) {
        uint32_t h=8+16*i;unsigned slave=ceph_arc_u16(db,h);
        if(!slave)break;
        uint32_t entries=144+ceph_arc_u16(db,h+2),aux=144+ceph_arc_u16(db,h+4);
        unsigned count=ceph_arc_u16(db,h+6);
        if(slave<3 || slave>5 || count>256 || (entries&3) || !ceph_arc_range(db,entries,count*24) || !ceph_arc_range(db,aux,0))return -1;
        for(unsigned j=0;j<count;++j) {
            uint32_t e=entries+24*j;
            if(db->read32(db->ctx,e)!=key[0] || db->read32(db->ctx,e+4)!=key[1])continue;
            if(++matches!=1 || slave!=5)return -1;
            uint32_t off=aux+ceph_arc_u16(db,e+22);unsigned size=ceph_arc_u16(db,e+20);
            if(size!=8 || (off&3) || !ceph_arc_range(db,off,8))return -1;
            found.address=db->read32(db->ctx,e+16);found.unit=db->read32(db->ctx,off);
            uint32_t info=db->read32(db->ctx,off+4);found.width=(uint16_t)info;found.vcd=(uint8_t)(info>>16);
            if((found.address>>16)!=5 || (found.address&3) || !found.unit || !found.width || found.vcd>=16 || (info>>24))return -1;
        }
    }
    if(!matches)return -2;
    *out=found;return 0;
}
static inline uint64_t ceph_bcm_div(uint64_t n,uint32_t d)
{return n && n<d?1:n/d;} /* caller validates nonzero divisor */
static inline int ceph_bcm_normalize(const struct ceph_bcm_resource *r,uint64_t ab,uint64_t ib,
    uint32_t buswidth,uint32_t ports,uint64_t *x,uint64_t *y)
{
    if(!r || !r->width || !r->unit || !buswidth || !ports || !x || !y ||
        buswidth>UINT32_MAX/ports || ab>UINT64_MAX/r->width || ib>UINT64_MAX/r->width)return -1;
    uint64_t a=ceph_bcm_div(ab*r->width,buswidth*ports),b=ceph_bcm_div(ib*r->width,buswidth);
    *x=ceph_bcm_div(a,r->unit);*y=ceph_bcm_div(b,r->unit);return 0;
}
struct ceph_bcm_vote {struct ceph_bcm_resource resource;uint64_t x,y;};
struct ceph_bcm_command {uint32_t address,data;};
static inline int ceph_bcm_commands(const struct ceph_bcm_vote *votes,unsigned count,
    struct ceph_bcm_command *out,unsigned capacity,uint32_t *wait_mask)
{
    if(!votes || !out || !wait_mask || !count || count>8 || capacity<count)return -1;
    unsigned order[8];
    for(unsigned i=0;i<count;++i) {
        const struct ceph_bcm_resource *r=&votes[i].resource;
        if((r->address>>16)!=5 || (r->address&3) || r->vcd>=16 || !r->width || !r->unit)return -1;
        for(unsigned j=0;j<i;++j)if(votes[j].resource.address==r->address)return -1;
        order[i]=i;
        for(unsigned j=i;j>0 && votes[order[j-1]].resource.vcd>r->vcd;--j) {
            order[j]=order[j-1];order[j-1]=i;
        }
    }
    uint32_t wait=0;
    for(unsigned i=0;i<count;++i) {
        const struct ceph_bcm_vote *v=&votes[order[i]];
        unsigned commit=i+1==count || votes[order[i+1]].resource.vcd!=v->resource.vcd;
        uint32_t x=v->x>0x3fff?0x3fff:(uint32_t)v->x,y=v->y>0x3fff?0x3fff:(uint32_t)v->y;
        out[i]=(struct ceph_bcm_command){v->resource.address,(commit<<30)|((x||y)?1u<<29:0)|(x<<14)|y};
        if(commit)wait|=1u<<i;
    }
    *wait_mask=wait;return (int)count;
}
#endif
