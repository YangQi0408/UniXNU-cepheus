/* Actual staged CoreExitBootServices; only external services are substituted. */
typedef struct {UINT32 CRC32,HeaderSize;} EFI_TABLE_HEADER;
typedef struct {UINT8 Data[64];} EFI_BOOT_SERVICES;
static EFI_BOOT_SERVICES exit_bs;
static struct {
  EFI_TABLE_HEADER Hdr;
  void *BootServices,*ConIn,*ConsoleInHandle,*ConOut,*ConsoleOutHandle,*StdErr,*StandardErrorHandle;
} exit_st;
static __typeof__(exit_st) *gDxeCoreST=&exit_st;
static BOOLEAN mExitBootServicesCalled;
static EFI_GUID gEfiEventBeforeExitBootServicesGuid,gEfiEventExitBootServicesGuid,
                gEventExitBootServicesFailedGuid;
static unsigned exit_step,exit_callbacks,exit_before,exit_failed;
static UINT64 exit_moved_array;
static EFI_STATUS ExitTimer(void *timer,UINT64 period)
{assert(timer&&period==0);exit_step=1;return 0;}
static struct {EFI_STATUS (*SetTimerPeriod)(void *,UINT64);} exit_timer={ExitTimer};
static __typeof__(exit_timer) *gTimer=&exit_timer;
static EFI_STATUS CoreTerminateMemoryMap(UINTN key)
{assert(exit_step==1);exit_step=2;return key==42?0:EFI_INVALID_PARAMETER;}
static VOID CoreNotifySignalList(EFI_GUID *guid)
{
  assert(!mCeTail.Finished);
  if(guid==&gEfiEventBeforeExitBootServicesGuid) {exit_before++;return;}
  if(guid==&gEventExitBootServicesFailedGuid) {exit_failed++;return;}
  assert(guid==&gEfiEventExitBootServicesGuid&&gMemoryMapTerminated&&exit_step==2);
  exit_callbacks++;exit_step=3;
  assert(!CoreFreePool((void *)(UINTN)(tail_array-4)));
  tw64(mCeTail.Image[0].Base+0xe648,exit_moved_array);
  tw32(tail_cx+24,11);
}
#define EFI_PROGRESS_CODE 1
#define EFI_SOFTWARE_EFI_BOOT_SERVICE 2
#define EFI_SW_BS_PC_EXIT_BOOT_SERVICES 4
#define REPORT_STATUS_CODE(a,b) assert(exit_step==3)
static VOID MemoryProtectionExitBootServicesCallback(void)
{assert(exit_step==3&&!mCeTail.Finished);exit_step=4;}
static VOID SaveAndSetDebugTimerInterrupt(BOOLEAN enabled)
{assert(!enabled&&exit_step==4);exit_step=5;}
static EFI_STATUS ExitDisable(void *cpu)
{assert(cpu&&exit_step==5);exit_step=6;return 0;}
static struct {EFI_STATUS (*DisableInterrupt)(void *);} exit_cpu={ExitDisable};
static __typeof__(exit_cpu) *gCpu=&exit_cpu;
static VOID CalculateEfiHdrCrc(EFI_TABLE_HEADER *header)
{
  assert(header==&exit_st.Hdr&&exit_step==6&&gBS&&!mCeTail.Finished);
  assert(!exit_st.BootServices&&!exit_st.ConIn&&!exit_st.ConsoleInHandle&&!exit_st.ConOut&&
         !exit_st.ConsoleOutHandle&&!exit_st.StdErr&&!exit_st.StandardErrorHandle);
  header->CRC32=123;exit_step=7;
}
#include "ExitUnderTest.c"

static void tail_exit_tests(void)
{
  tail_fixture();UINT64 bases[3];for(unsigned i=0;i<3;i++)bases[i]=(UINTN)tail_images[i];
  assert(!CeTailBind(bases));
  exit_moved_array=tail_object(48);memcpy((void *)(UINTN)exit_moved_array,(void *)(UINTN)tail_array,48);
  memset(&exit_bs,0xa5,sizeof(exit_bs));memset(&exit_st,0xa5,sizeof(exit_st));
  gBS=&exit_bs;mExitBootServicesCalled=0;exit_callbacks=exit_before=exit_failed=0;
  assert(CoreExitBootServices(NULL,0)==EFI_INVALID_PARAMETER);
  assert(!mCeTail.Finished&&mCeTail.Attempts==1&&exit_before==1&&exit_failed==1&&!exit_callbacks);
  assert(!CoreExitBootServices(NULL,42));
  assert(exit_step==7&&exit_callbacks==1&&exit_before==1&&!gBS&&fake_runtime.AtRuntime);
  assert(mCeTail.Finished&&!mCeTailRecord.error&&!ceph_rpmh_tail_valid(&mCeTailRecord));
  assert(mCeTailRecord.requests.ebs_attempts==2&&mCeTailRecord.requests.resource[0].drv[0].local[0].data==11);
  assert(!memcmp(&exit_bs,(UINT8[sizeof(exit_bs)]){0},sizeof(exit_bs))&&exit_st.Hdr.CRC32==123);
  struct ceph_rpmh_tail saved=mCeTailRecord;CoreCepheusRpmhTail();
  assert(!memcmp(&saved,&mCeTailRecord,sizeof(saved)));tail_cleanup();
  puts("Actual CoreExitBootServices: failed-map retry, callback move/free, cleanup-before-tail and single capture PASS");
}
