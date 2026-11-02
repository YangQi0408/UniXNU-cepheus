#include "cepheus_gmu_hfi_link.h"

static void link_barrier(void)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dsb sy" ::: "memory");
#elif !defined(KERNEL)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
#error "GMU HFI link requires arm64"
#endif
}
static int link_validate(void *ctx)
{
    struct ceph_gmu_hfi_link *l=ctx;
    if(!l || !l->bound || !l->workspace || l->workspace->state!=CE_WS_SHARED ||
        l->workspace->publication.group!=l->identity.group || l->workspace->leased!=31)return -1;
    struct ceph_gpu_group_status status;
    if(cepheus_gpu_memory_group_status(l->identity.group,&status) || status.pool_terminal)return -1;
    if(status.state!=CE_GPU_GROUP_SHARED &&
        !(status.state==CE_GPU_GROUP_RETIRING && l->session.busy &&
          (l->session.active_header&0xfffffu)==0x321u))return -1;
    for(unsigned i=0;i<5;++i)
        if(l->workspace->handles[i]!=l->identity.handles[i] || status.identity.handles[i]!=l->identity.handles[i])return -1;
    return l->transport.validate(l->transport.ctx);
}
static int link_acquire(void *ctx){if(link_validate(ctx))return -1;link_barrier();return 0;}
static int link_publish(void *ctx){if(link_validate(ctx))return -1;link_barrier();return 0;}
static int link_notify(void *ctx){struct ceph_gmu_hfi_link *l=ctx;if(link_validate(ctx))return -1;link_barrier();return l->transport.doorbell(l->transport.ctx);}
static int link_rx_notify(void *ctx){return link_validate(ctx);}
static int link_now(void *ctx,uint64_t *now){struct ceph_gmu_hfi_link *l=ctx;return l->transport.now_ns(l->transport.ctx,now);}
static int link_pause(void *ctx){struct ceph_gmu_hfi_link *l=ctx;return l->transport.pause(l->transport.ctx);}
static int link_fault(void *ctx){struct ceph_gmu_hfi_link *l=ctx;if(link_validate(ctx))return -1;return l->transport.fault(l->transport.ctx);}

int cepheus_gmu_hfi_bind(struct ceph_gmu_hfi_link *l,struct ceph_gmu_workspace *w,const struct ceph_gmu_hfi_transport *b)
{
    if(!l || l->bound || !w || w->state!=CE_WS_SHARED || w->leased!=31 ||
        !b || !b->validate || !b->doorbell || !b->now_ns || !b->pause || !b->fault ||
        !w->views[2].cpu || ((uintptr_t)w->views[2].cpu&3u) || w->views[2].bytes<20480 ||
        w->layout.start.hfi_bytes!=20480)return -1;
    struct ceph_gpu_group_status status;
    if(cepheus_gpu_memory_group_status(w->publication.group,&status) || status.state!=CE_GPU_GROUP_SHARED || status.pool_terminal)return -1;
    uint32_t expected[54];
    if(ceph_hfi_table(expected,54,w->layout.start.hfi_va,w->layout.start.hfi_bytes))return -1;
    volatile uint32_t *table=w->views[2].cpu;
    link_barrier();
    /* Initial bind before HFI firmware consumption only; do not discard data
     * or overwrite live indices to force an inherited queue to look empty. */
    for(unsigned i=0;i<54;++i)if(table[i]!=expected[i])return -1;
    *l=(struct ceph_gmu_hfi_link){0};l->workspace=w;l->identity=w->publication;l->transport=*b;l->bound=1;
    l->tx=(struct ceph_hfi_queue){table+56,table+6+10,table+6+11,1024,2,
        {l,link_acquire,link_publish,link_publish,link_notify},0};
    l->rx=(struct ceph_hfi_queue){table+56+1024,table+18+10,table+18+11,1024,2,
        {l,link_acquire,link_publish,link_publish,link_rx_notify},0};
    l->session=(struct ceph_hfi_session){&l->tx,&l->rx,l,link_now,link_pause,link_fault,0,0,0,0};
    if(link_validate(l)){cepheus_gmu_hfi_invalidate(l);return -1;}
    return 0;
}
void cepheus_gmu_hfi_invalidate(struct ceph_gmu_hfi_link *l)
{
    if(!l)return;
    l->bound=0;l->session.poisoned=1;l->tx.poisoned=l->rx.poisoned=1;
    l->workspace=NULL;l->tx.words=NULL;l->rx.words=NULL;
    l->tx.read_index=l->tx.write_index=NULL;l->rx.read_index=l->rx.write_index=NULL;
}
