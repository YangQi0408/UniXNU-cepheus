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
static int link_identity_valid(struct ceph_gmu_hfi_link *l)
{
    if(!l->workspace || l->workspace->state!=CE_WS_SHARED ||
        l->workspace->publication.group!=l->identity.group || l->workspace->leased!=CE_GPU_WORKSPACE_VIEWS ||
        l->workspace->views[2].cpu!=l->hfi_view.cpu || l->workspace->views[2].pa!=l->hfi_view.pa ||
        l->workspace->views[2].bytes!=l->hfi_view.bytes ||
        l->workspace->layout.start.hfi_va!=l->hfi_va || l->workspace->layout.start.hfi_bytes!=20480)return -1;
    volatile uint32_t *table=l->hfi_view.cpu;
    struct ceph_hfi_queue *queues[]={&l->tx,&l->rx,&l->debug};
    for(unsigned i=0;i<3;++i)
        if(queues[i]->words!=table+56+i*1024 || queues[i]->read_index!=table+16+i*12 ||
            queues[i]->write_index!=table+17+i*12 || queues[i]->capacity!=1024 ||
            queues[i]->major!=2 || queues[i]->io.ctx!=l)return -1;
    struct ceph_gpu_group_status status;
    if(cepheus_gpu_memory_group_status(l->identity.group,&status) || status.pool_terminal)return -1;
    if(status.state!=CE_GPU_GROUP_SHARED &&
        !(status.state==CE_GPU_GROUP_RETIRING && l->session.busy &&
          (l->session.active_header&0xfffffu)==0x321u))return -1;
    const struct ceph_gpu_publication *identities[]={&l->workspace->publication,&status.identity};
    for(unsigned n=0;n<2;++n) {
        const struct ceph_gpu_publication *p=identities[n];
        if(p->group!=l->identity.group || p->ttbr0!=l->identity.ttbr0 || p->ttbr1!=l->identity.ttbr1 ||
            p->smr!=l->identity.smr || p->cb!=l->identity.cb || p->slot!=l->identity.slot ||
            p->asid!=l->identity.asid)return -1;
    }
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)
        if(l->workspace->handles[i]!=l->identity.handles[i] ||
            l->workspace->publication.handles[i]!=l->identity.handles[i] ||
            status.identity.handles[i]!=l->identity.handles[i])return -1;
    return 0;
}
static int link_validate(void *ctx)
{
    struct ceph_gmu_hfi_link *l=ctx;
    if(!l || !l->bound || l->lease_failed)return -1;
    if(link_identity_valid(l))goto failed;
    struct ceph_gmu_hfi_transport before=l->transport;
    if(!before.validate || !before.doorbell || !before.now_ns || !before.pause || !before.fault ||
        before.validate(before.ctx) || !l->bound || l->lease_failed ||
        l->transport.ctx!=before.ctx || l->transport.validate!=before.validate ||
        l->transport.doorbell!=before.doorbell || l->transport.now_ns!=before.now_ns ||
        l->transport.pause!=before.pause || l->transport.fault!=before.fault || link_identity_valid(l))goto failed;
    return 0;
failed:
    l->lease_failed=1;return -1;
}
static int link_acquire(void *ctx){if(link_validate(ctx))return -1;link_barrier();return link_validate(ctx);}
static int link_publish(void *ctx){if(link_validate(ctx))return -1;link_barrier();return link_validate(ctx);}
static int link_notify(void *ctx){struct ceph_gmu_hfi_link *l=ctx;if(link_validate(ctx))return -1;link_barrier();
    int e=l->transport.doorbell(l->transport.ctx);if(e){l->lease_failed=1;return e;}return link_validate(ctx);}
static int link_rx_notify(void *ctx){return link_validate(ctx);}
static int link_now(void *ctx,uint64_t *now){struct ceph_gmu_hfi_link *l=ctx;uint64_t value;
    if(!now || link_validate(ctx))return -1;
    int e=l->transport.now_ns(l->transport.ctx,&value);if(e){l->lease_failed=1;return e;}
    if(link_validate(ctx))return -1;
    *now=value;return 0;}
static int link_pause(void *ctx){struct ceph_gmu_hfi_link *l=ctx;if(link_validate(ctx))return -1;
    int e=l->transport.pause(l->transport.ctx);if(e){l->lease_failed=1;return e;}return link_validate(ctx);}
int cepheus_gmu_hfi_poll_debug(struct ceph_gmu_hfi_link *l)
{
    if(!l || link_validate(l))return CE_HFI_IO;
    if(l->debug_error)return l->debug_error;
    for(unsigned messages=0;messages<32;++messages) {
        uint32_t data[255];unsigned count=0;
        int e=ceph_hfi_read(&l->debug,data,255,&count);
        if(e==CE_HFI_EMPTY)return 0;
        if(e){l->debug_error=e;return e;}
        unsigned id=data[0]&255u,type=(data[0]>>16)&15u;
        if(id==101 && type==0 && count==4) {
            if(l->debug_messages==UINT32_MAX){l->debug_error=CE_HFI_PROTOCOL;return l->debug_error;}
            ++l->debug_messages;for(unsigned i=0;i<4;++i)l->last_debug[i]=data[i];
        } else {
            l->error_words=count<18?count:18;
            for(unsigned i=0;i<l->error_words;++i)l->last_error[i]=data[i];
            l->debug_error=id==100 && type==0 && count==18?CE_HFI_FW_ERROR:CE_HFI_PROTOCOL;
            return l->debug_error;
        }
    }
    /* A perpetually producing firmware cannot monopolize a kernel call.
     * More than32 records in this diagnostic poll is terminal, not clean. */
    unsigned r,w;int e=ceph_hfi_indices(&l->debug,&r,&w);
    if(e || r!=w){l->debug_error=e?e:CE_HFI_PROTOCOL;return l->debug_error;}
    return 0;
}
static int link_fault(void *ctx){struct ceph_gmu_hfi_link *l=ctx;
    if(link_validate(ctx))return -1;
    if(l->transport.fault(l->transport.ctx)){l->lease_failed=1;return -1;}
    if(link_validate(ctx))return -1;
    return cepheus_gmu_hfi_poll_debug(l);}

int cepheus_gmu_hfi_bind(struct ceph_gmu_hfi_link *l,struct ceph_gmu_workspace *w,const struct ceph_gmu_hfi_transport *b)
{
    if(!l || l->bound || !w || w->state!=CE_WS_SHARED || w->leased!=CE_GPU_WORKSPACE_VIEWS ||
        !b || !b->validate || !b->doorbell || !b->now_ns || !b->pause || !b->fault ||
        !w->views[2].cpu || ((uintptr_t)w->views[2].cpu&3u) || w->views[2].bytes<20480 ||
        (uintptr_t)w->views[2].cpu>UINTPTR_MAX-20480 ||
        w->layout.start.hfi_bytes!=20480)return -1;
    struct ceph_gpu_group_status status;
    if(cepheus_gpu_memory_group_status(w->publication.group,&status) || status.state!=CE_GPU_GROUP_SHARED || status.pool_terminal)return -1;
    uint32_t expected[54];
    if(ceph_hfi_table(expected,54,w->layout.start.hfi_va,w->layout.start.hfi_bytes))return -1;
    volatile uint32_t *table=w->views[2].cpu;
    link_barrier();
    /* Initial bind before HFI firmware consumption only; do not discard data
     * or overwrite live indices to force an inherited queue to look empty. */
    struct ceph_gmu_hfi_link candidate={0};candidate.workspace=w;
    candidate.identity=w->publication;candidate.transport=*b;candidate.bound=1;
    candidate.hfi_view=w->views[2];candidate.hfi_va=w->layout.start.hfi_va;
    candidate.tx=(struct ceph_hfi_queue){table+56,table+16,table+17,1024,2,
        {&candidate,link_acquire,link_publish,link_publish,link_notify,link_validate},0};
    candidate.rx=(struct ceph_hfi_queue){table+56+1024,table+28,table+29,1024,2,
        {&candidate,link_acquire,link_publish,link_publish,link_rx_notify,link_validate},0};
    candidate.debug=(struct ceph_hfi_queue){table+56+2048,table+40,table+41,1024,2,
        {&candidate,link_acquire,link_publish,link_publish,link_rx_notify,link_validate},0};
    for(unsigned i=0;i<54;++i) {
        if(link_validate(&candidate))return -1;
        uint32_t value=table[i];link_barrier();
        if(link_validate(&candidate) || value!=expected[i])return -1;
    }
    if(link_validate(&candidate))return -1;
    *l=candidate;
    l->tx.io.ctx=l->rx.io.ctx=l->debug.io.ctx=l;
    l->session=(struct ceph_hfi_session){&l->tx,&l->rx,l,link_now,link_pause,link_fault,0,0,0,0};
    return 0;
}
void cepheus_gmu_hfi_invalidate(struct ceph_gmu_hfi_link *l)
{
    if(!l)return;
    l->bound=0;l->session.poisoned=1;l->tx.poisoned=l->rx.poisoned=l->debug.poisoned=1;
    l->workspace=NULL;l->tx.words=NULL;l->rx.words=NULL;l->debug.words=NULL;
    l->tx.read_index=l->tx.write_index=NULL;l->rx.read_index=l->rx.write_index=NULL;
    l->debug.read_index=l->debug.write_index=NULL;
}
