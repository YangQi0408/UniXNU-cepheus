#include "cepheus_cp_workspace.h"
#include "cepheus_gpu_compile.h"

int cepheus_cp_workspace_release(struct ceph_cp_workspace *w)
{
    if(!w || (w->state!=CE_CP_WS_BUILDING && w->state!=CE_CP_WS_READY))return -1;
    int e=0;
    if(w->firmware_view) {
        if(cepheus_gpu_memory_view_put(w->firmware))e=-1;
        else w->firmware_view=0;
    }
    if(w->firmware && !w->firmware_view) {
        if(cepheus_gpu_memory_release_cpu(w->firmware))e=-1;
        else w->firmware=0;
    }
    for(unsigned i=CE_CP_BUFFERS;i>0;--i) {
        unsigned n=i-1;
        if(w->leased&(1u<<n)) {
            if(cepheus_gpu_memory_view_put(w->handles[n])){e=-1;continue;}
            w->leased&=~(1u<<n);w->views[n]=(struct ceph_gpu_memory_view){0};
        }
        if(w->handles[n]) {
            if(cepheus_gpu_memory_release_cpu(w->handles[n]))e=-1;
            else w->handles[n]=0;
        }
    }
    w->sqe=(struct ceph_sqe_image){0};w->state=e?CE_CP_WS_RETAINED:CE_CP_WS_RELEASED;return e;
}
int cepheus_cp_workspace_prepare(struct ceph_cp_workspace *w,const uint8_t *file,size_t bytes)
{
    if(!w || w->state!=CE_CP_WS_NEW || w->leased || w->firmware || w->firmware_view)return -1;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)if(w->handles[i])return -1;
    w->state=CE_CP_WS_BUILDING;
    if(cepheus_gpu_firmware_snapshot(1,file,bytes,&w->firmware))goto failed;
    static const size_t sizes[]={32768,4096,32768,4096,4096};
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        if(cepheus_gpu_memory_alloc(sizes[i],&w->handles[i]) ||
            cepheus_gpu_memory_view_get(w->handles[i],&w->views[i]))goto failed;
        w->leased|=1u<<i;
    }
    struct ceph_gpu_firmware_view view;
    if(cepheus_gpu_firmware_get(w->firmware,1,&view))goto failed;
    w->firmware_view=1;
    if(ceph_sqe_copy(view.data,view.bytes,w->views[CE_CP_SQE].cpu,w->views[CE_CP_SQE].bytes,&w->sqe) ||
        w->sqe.bytes!=32300 || w->sqe.version!=0x16ee190)goto failed;
    /* SHA-verified source snapshot remains pinned through byte comparison.
     * Trailing zero bytes in32KiB SQE backing remain from pool allocation. */
    const volatile uint8_t *copy=w->views[CE_CP_SQE].cpu;
    for(size_t i=0;i<w->sqe.bytes;++i)if(copy[i]!=view.data[i+4])goto failed;
    if(cepheus_gpu_memory_view_put(w->firmware))goto failed;
    w->firmware_view=0;
    if(cepheus_gpu_memory_release_cpu(w->firmware))goto failed;
    w->firmware=0;w->state=CE_CP_WS_READY;return 0;
failed:
    (void)cepheus_cp_workspace_release(w);return -1;
}
