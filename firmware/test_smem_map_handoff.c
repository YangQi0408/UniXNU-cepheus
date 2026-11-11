#include "map-probe-host-tests.c"
#include <setjmp.h>
STATIC UINT64 Ticks;
STATIC UINT64 CeTicks (VOID) { return ++Ticks; }
#define READ_SYSREG(n) CeTicks ()
#include "CepheusSmemMapHandoff.h"
STATIC ARM_MEMORY_REGION_DESCRIPTOR_EX Map[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
STATIC _Alignas(4096) UINT8 Tree[8192];
ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap (VOID) { return Map; }

STATIC VOID HandoffFixture (VOID)
{
  Fixture (); ZeroMem (Map, sizeof (Map)); Map[0] = Profile;
  mCepheusSmemAttempted = FALSE; Ticks = 0;
  ZeroMem (&mCepheusSmem, sizeof (mCepheusSmem));
  memset (Tree, 0xa5, sizeof (Tree));
}
STATIC BOOLEAN Publish (VOID)
{ return CepheusSmemHandoff ((UINTN)Tree, sizeof (Tree), 128, 0); }

STATIC jmp_buf Jump;
STATIC unsigned EbsCalls, MapCalls, EbsSuccessAt, MapFailAt;
STATIC EFI_STATUS EbsError;
STATIC EFI_STATUS EFIAPI
ExitMock (EFI_HANDLE Image, UINTN Key)
{
  CHECK (Image == (VOID *)1 && Key == MapCalls && Pars == 512 && Closes == 1);
  ++EbsCalls;
  return EbsCalls == EbsSuccessAt ? EFI_SUCCESS : EbsError;
}
STATIC EFI_STATUS EFIAPI
MapMock (UINTN *Bytes, EFI_MEMORY_DESCRIPTOR *Map, UINTN *Key, UINTN *Stride, UINT32 *Version)
{
  (VOID)Map;
  ++MapCalls; *Bytes = 64; *Key = MapCalls; *Stride = sizeof (EFI_MEMORY_DESCRIPTOR); *Version = 1;
  return MapCalls == MapFailAt ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
VOID EFIAPI CpuDeadLoop (VOID) { longjmp (Jump, 2); }
STATIC VOID
DarwinEnter (UINT64 Args, UINT64 Entry, UINT64 Memory, UINT64 Bytes, UINT64 Video, UINT64 VideoBytes)
{
  (VOID)Args; (VOID)Entry; (VOID)Memory; (VOID)Bytes; (VOID)Video; (VOID)VideoBytes;
  if (ceph_smem_map_valid (&mCepheusSmem) || mCepheusSmem.ebs_attempts != EbsCalls ||
      memcmp (Tree + 128, &mCepheusSmem, sizeof (mCepheusSmem))) { abort (); }
  longjmp (Jump, 1);
}
#include "smem-ebs-loop.c"
STATIC int RunEbsLoop (VOID)
{
  int Result = setjmp (Jump);
  if (!Result) { ActualEbsLoop (); abort (); }
  return Result;
}

int main (void)
{
  HandoffFixture (); CHECK (!CepheusSmemCapture ());
  CHECK (Pars == 512 && Creates == 1 && Closes == 1 && !mSmemExitEvent);
  CHECK (CepheusSmemCapture () == EFI_ALREADY_STARTED && Pars == 512);
  CHECK (Publish () && !ceph_smem_map_valid (&mCepheusSmem));
  CHECK (ceph_smem_map_diagnostic_pass (&mCepheusSmem));
  CHECK (!memcmp (Tree + 128, &mCepheusSmem, sizeof (mCepheusSmem)));
  for (unsigned I = 0; I < sizeof (Tree); I++) {
    if (I < 128 || I >= 128 + sizeof (mCepheusSmem)) { CHECK (Tree[I] == 0xa5); }
  }
  CHECK (!Publish () && Pars == 512 && Closes == 1);
  /* Raw mocked producer output is independently consumed by the XNU helper. */
  CHECK (fwrite (&mCepheusSmem, 1, sizeof (mCepheusSmem), stdout) == sizeof (mCepheusSmem));
  for (unsigned Page = 1; Page <= 512; Page++) {
    HandoffFixture (); FaultPage = Page; ParBits = 1;
    CHECK (!CepheusSmemCapture () && mCepheusSmem.probe_status == EFI_NO_MAPPING);
    CHECK (Publish () && !ceph_smem_map_valid (&mCepheusSmem));
    CHECK (!ceph_smem_map_diagnostic_pass (&mCepheusSmem) && mCepheusSmem.pages == Page);
    CHECK (mCepheusSmem.par[Page - 1] & 1);
  }
  HandoffFixture (); Map[1] = Profile;
  CHECK (!CepheusSmemCapture () && mCepheusSmem.prepare_status == EFI_INVALID_PARAMETER);
  CHECK (Publish () && !ceph_smem_map_valid (&mCepheusSmem));
  CHECK (!ceph_smem_map_diagnostic_pass (&mCepheusSmem) && !Creates && !Pars);
  HandoffFixture (); TableStatus = EFI_NOT_FOUND;
  CHECK (!CepheusSmemCapture () && Publish () && !ceph_smem_map_diagnostic_pass (&mCepheusSmem));
  CHECK (mCepheusSmem.probe_status == EFI_NOT_FOUND && Closes == 1);
  HandoffFixture (); CloseStatus = EFI_DEVICE_ERROR;
  CHECK (CepheusSmemCapture () == EFI_DEVICE_ERROR && mSmemExitEvent);
  CHECK (!Publish () && Tree[128] == 0xa5);
  CloseStatus = EFI_SUCCESS; CHECK (!CepheusSmemMapClose () && !mSmemExitEvent);
  HandoffFixture (); ExitAt = 1;
  CHECK (CepheusSmemCapture () == EFI_ACCESS_DENIED && !Closes && !Publish ());
  for (unsigned Mode = 0; Mode < 7; Mode++) {
    HandoffFixture (); CHECK (!CepheusSmemCapture ());
    UINT64 Address = (UINTN)Tree; UINTN Bytes = sizeof (Tree), Offset = 128, Attempt = 0;
    switch (Mode) {
      case 0: Address++; break;
      case 1: Bytes = 100; break;
      case 2: Offset++; break;
      case 3: Address = CE_SMEM_MAP_BASE; break;
      case 4: Address = MAX_UINT64 - 4095; break;
      case 5: Offset = MAX_UINTN; break;
      default: Attempt = 3; break;
    }
    CHECK (!CepheusSmemHandoff (Address, Bytes, Offset, Attempt) && Tree[128] == 0xa5);
  }
  for (unsigned Success = 1; Success <= 3; Success++) {
    HandoffFixture (); CHECK (!CepheusSmemCapture ());
    EbsCalls = MapCalls = MapFailAt = 0; EbsSuccessAt = Success; EbsError = EFI_INVALID_PARAMETER;
    Bs.GetMemoryMap = MapMock; Bs.ExitBootServices = ExitMock;
    int Result = RunEbsLoop ();
    CHECK (Result == 1 && EbsCalls == Success && MapCalls == Success && Pars == 512 && Closes == 1);
  }
  for (unsigned Mode = 0; Mode < 4; Mode++) {
    HandoffFixture (); CHECK (!CepheusSmemCapture ());
    EbsCalls = MapCalls = 0; EbsSuccessAt = 4; EbsError = EFI_INVALID_PARAMETER;
    MapFailAt = Mode < 2 ? Mode + 1 : 0;
    if (Mode == 2) { EbsError = EFI_DEVICE_ERROR; }
    Bs.GetMemoryMap = MapMock; Bs.ExitBootServices = ExitMock;
    int Result = RunEbsLoop ();
    CHECK (Result == 2 && !mCepheusSmem.complete && Tree[128] == 0xa5 && Pars == 512);
  }
  fprintf (stderr, "SMEM handoff: actual capture/copy, 512 partial faults, cleanup/EBS/bounds/one-shot PASS; mocked services/AT\n");
  return 0;
}
