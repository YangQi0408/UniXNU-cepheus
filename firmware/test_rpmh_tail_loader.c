/* Actual loader adapter -> actual core Bind/EBS/Copy, with boot services mocked. */
typedef struct {VOID *ImageBase;UINT64 ImageSize;} EFI_LOADED_IMAGE_PROTOCOL;
typedef struct {
  EFI_STATUS (*LocateProtocol)(EFI_GUID *,VOID *,VOID **);
  EFI_STATUS (*LocateHandleBuffer)(int,EFI_GUID *,VOID *,UINTN *,EFI_HANDLE **);
  EFI_STATUS (*HandleProtocol)(EFI_HANDLE,EFI_GUID *,VOID **);
} TAIL_LOADER_SERVICES;
static TAIL_LOADER_SERVICES *tail_loader_bs;
static EFI_GUID gEfiLoadedImageProtocolGuid;
#define ByProtocol 2
static void FreePool(void *p) {assert(!CoreFreePool(p));}
#define gBS tail_loader_bs
#include "CepheusRpmhTailHandoff.h"
#undef gBS

static unsigned loader_case,loader_binds,loader_copies;
static EFI_LOADED_IMAGE_PROTOCOL loader_images[4];
static EFI_STATUS LoaderBind(CONST UINT64 bases[3])
{assert(tail_loader_bs);loader_binds++;return CeTailBind(bases);}
static EFI_STATUS LoaderCopy(struct ceph_rpmh_tail *p)
{
  assert(!tail_loader_bs&&!gBS&&mCeTail.Finished);loader_copies++;
  EFI_STATUS status=CeTailCopy(p);
  if(loader_case==6)p->checksum^=1;
  return loader_case==7?EFI_NOT_READY:status;
}
static CE_TAIL_PROTOCOL loader_protocol={1,sizeof(CE_TAIL_PROTOCOL),LoaderBind,LoaderCopy};
static EFI_STATUS LoaderLocate(EFI_GUID *guid,VOID *registration,VOID **output)
{
  assert(tail_loader_bs&&!registration&&output);
  if(guid==&mCeTailHandoffGuid) {
    if(loader_case==1)return EFI_NOT_FOUND;
    *output=&loader_protocol;return 0;
  }
  if(guid==&mCeTailRpmhGuid) {*output=(VOID *)(UINTN)(mCeTail.Image[0].Base+0xe0f8);return 0;}
  assert(guid==&mCeTailCmdDbGuid);*output=(VOID *)(UINTN)(mCeTail.Image[2].Base+0x6078);return 0;
}
static EFI_STATUS LoaderHandles(int kind,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **handles)
{
  assert(tail_loader_bs&&kind==ByProtocol&&guid==&gEfiLoadedImageProtocolGuid&&!key);
  *count=loader_case==3?4:3;
  assert(!CoreAllocatePool(EfiBootServicesData,*count*sizeof(EFI_HANDLE),(VOID **)handles));
  for(unsigned i=0;i<*count;i++)(*handles)[i]=&loader_images[i==3?1:i];
  return 0;
}
static EFI_STATUS LoaderImage(EFI_HANDLE handle,EFI_GUID *guid,VOID **loaded)
{assert(tail_loader_bs&&guid==&gEfiLoadedImageProtocolGuid);*loaded=handle;return 0;}
static TAIL_LOADER_SERVICES loader_services={LoaderLocate,LoaderHandles,LoaderImage};

static void tail_loader_tests(void)
{
  for(unsigned which=0;which<8;which++) {
    tail_fixture();loader_case=which;loader_binds=loader_copies=0;
    mCepheusRpmhTailAttempted=0;ZeroMem(&mCepheusRpmhTailProtocol,sizeof(mCepheusRpmhTailProtocol));
    loader_protocol.Version=which==2?2:1;
    for(unsigned i=0;i<3;i++)loader_images[i]=(EFI_LOADED_IMAGE_PROTOCOL){tail_images[i],mCeTail.Image[i].Bytes};
    if(which==4)loader_images[0].ImageSize--;
    if(which==5)*(UINT8 *)tail_images[0]^=1;
    tail_loader_bs=&loader_services;CepheusRpmhTailPrepare();
    assert(mCepheusRpmhTailAttempted&&(mCepheusRpmhTailHandoff.flags&CE_TAIL_PREPARED));
    unsigned binds=loader_binds;CepheusRpmhTailPrepare();assert(loader_binds==binds);
    /* Successful EBS always runs, even if pre-bind failed. */
    exit_moved_array=tail_object(48);memcpy((VOID *)(UINTN)exit_moved_array,(VOID *)(UINTN)tail_array,48);
    memset(&exit_bs,0xa5,sizeof(exit_bs));memset(&exit_st,0xa5,sizeof(exit_st));
    gBS=&exit_bs;mExitBootServicesCalled=0;
    assert(!CoreExitBootServices(NULL,42));tail_loader_bs=NULL;
    struct ceph_rpmh_tail_handoff copied;CepheusRpmhTailHandoff((UINTN)&copied,0);
    assert(!ceph_rpmh_tail_handoff_valid(&copied));
    if(which==0) {
      assert(copied.flags==15&&loader_binds==1&&loader_copies==1&&!copied.record.error);
      assert(copied.record.requests.resource[0].drv[0].local[0].data==11);
      for(unsigned i=0;i<sizeof(copied);i++) {
        struct ceph_rpmh_tail_handoff corrupt=copied;((UINT8 *)&corrupt)[i]^=1;
        assert(ceph_rpmh_tail_handoff_valid(&corrupt));
      }
    } else {
      assert(!(copied.flags&CE_TAIL_COPIED));
      assert(which>=6?loader_copies==1:loader_copies==0);
      assert(!memcmp(&copied.record,(UINT8[sizeof(copied.record)]){0},sizeof(copied.record)));
    }
    tail_cleanup();
  }
  assert(ceph_rpmh_tail_profile("cephrpmhtail=1")==1);
  assert(ceph_rpmh_tail_profile("sweetusb=2 cephrpmhtail=1\t")==1);
  assert(!ceph_rpmh_tail_profile("cephscmpair=1"));
  assert(ceph_rpmh_tail_profile("cephrpmhtail=1 cephscmpair=1")==-1);
  assert(ceph_rpmh_tail_profile("cephrpmhtail=0")==-1);
  assert(ceph_rpmh_tail_profile("cephrpmhtail=1 cephrpmhtail=1")==-1);
  puts("Actual loader -> core Bind -> actual EBS -> core Copy: valid record, transport failure/duplicate/image/copy corruption and profile PASS");
}
