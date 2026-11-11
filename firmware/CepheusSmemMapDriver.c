#include "CepheusSmemMapProbe.h"
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>

STATIC CE_SMEM_MAP_EVIDENCE mSmemEvidence;
STATIC BOOLEAN mSmemDriverAttempted;

STATIC CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *
CeSmemProfile (VOID)
{
  CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *Map = GetPlatformMemoryMap ();
  CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *Selected = NULL;
  if (!Map) { return NULL; }
  for (UINTN I = 0; I < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT; I++) {
    if (!Map[I].Length) { return Selected; }
    if (!CompareMem (Map[I].Name, "SMEM\0", 5)) {
      if (Selected) { return NULL; }
      Selected = &Map[I];
    }
  }
  return NULL;
}

STATIC VOID
CeSmemDumpCpu (CONST CHAR8 *Name, CONST CE_SMEM_CPU_MAP *C)
{
  AsciiPrint ("CEPHSMEMMAP cpu=%a el=%lx cpu=%lx sctlr=%lx tcr=%lx ttbr0=%lx ttbr1=%lx mair=%lx mmfr0=%lx\n",
              Name, C->El, C->Cpu, C->Sctlr, C->Tcr, C->Ttbr0, C->Ttbr1, C->Mair, C->Mmfr0);
}

STATIC VOID
CeSmemDump (EFI_STATUS Prepare, EFI_STATUS Probe, EFI_STATUS Close)
{
  CONST EFI_HOB_RESOURCE_DESCRIPTOR *R = &mSmemEvidence.Resource;
  CONST EFI_GCD_MEMORY_SPACE_DESCRIPTOR *D = &mSmemEvidence.Gcd;
  AsciiPrint ("CEPHSMEMMAP begin version=1 base=%lx bytes=%lx prepare=%lx\n",
              CE_SMEM_PROBE_BASE, CE_SMEM_PROBE_BYTES, (UINT64)Prepare);
  CeSmemDumpCpu ("before", &mSmemEvidence.Before);
  AsciiPrint ("CEPHSMEMMAP resource base=%lx bytes=%lx type=%x attr=%x matches=%u hobs=%u complete=%u\n",
          R->PhysicalStart, R->ResourceLength, R->ResourceType, R->ResourceAttribute,
          mSmemEvidence.ResourceMatches, mSmemEvidence.Hobs, (UINT32)mSmemEvidence.ResourceComplete);
  AsciiPrint ("CEPHSMEMMAP gcd base=%lx bytes=%lx type=%u caps=%lx attr=%lx image=%lx device=%lx complete=%u\n",
          D->BaseAddress, D->Length, (UINT32)D->GcdMemoryType, D->Capabilities, D->Attributes,
          (UINT64)(UINTN)D->ImageHandle, (UINT64)(UINTN)D->DeviceHandle, (UINT32)mSmemEvidence.GcdComplete);
  for (UINT32 I = 0; I < mSmemEvidence.Pages && I < CE_SMEM_PROBE_PAGES; I++) {
    AsciiPrint ("CEPHSMEMMAP page index=%u va=%lx par=%lx\n",
                I, CE_SMEM_PROBE_BASE + (UINT64)I * 4096, mSmemEvidence.Par[I]);
  }
  CeSmemDumpCpu ("after", &mSmemEvidence.After);
  AsciiPrint ("CEPHSMEMMAP end probe=%lx close=%lx pages=%u stable=%u\n",
              (UINT64)Probe, (UINT64)Close, mSmemEvidence.Pages, (UINT32)mSmemEvidence.CpuStable);
}

EFI_STATUS EFIAPI
CepheusSmemMapDriverUnload (EFI_HANDLE ImageHandle)
{
  (VOID)ImageHandle;
  EFI_STATUS Status = CepheusSmemMapClose ();
  return Status == EFI_NOT_STARTED ? EFI_SUCCESS : Status;
}

EFI_STATUS EFIAPI
CepheusSmemMapDriverEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  (VOID)SystemTable;
  if (mSmemDriverAttempted) { return EFI_ALREADY_STARTED; }
  EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
  EFI_STATUS Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) { return Status; }
  STATIC CONST CHAR16 Options[] = L"cephsmemmap=1";
  if (!Loaded || !Loaded->LoadOptions || Loaded->LoadOptionsSize != sizeof (Options) ||
      CompareMem (Loaded->LoadOptions, Options, sizeof (Options))) { return EFI_ACCESS_DENIED; }
  mSmemDriverAttempted = TRUE;
  EFI_STATUS Prepare = CepheusSmemMapPrepare (CeSmemProfile ());
  EFI_STATUS Probe = EFI_NOT_STARTED, Close = EFI_NOT_STARTED;
  if (!EFI_ERROR (Prepare)) {
    Probe = CepheusSmemMapProbe (&mSmemEvidence);
    Close = CepheusSmemMapClose ();
  }
  /* Console output uses Boot Services. No logging after the EBS event. */
  if (Close == EFI_ACCESS_DENIED) { return EFI_SUCCESS; }
  CeSmemDump (Prepare, Probe, Close);
  /* Keep the module loaded if event cleanup failed; Unload can retry cleanup.
   * Never return an error while an event still references this module's code. */
  if (!EFI_ERROR (Prepare) && EFI_ERROR (Close)) { return EFI_SUCCESS; }
  return EFI_ERROR (Prepare) ? Prepare : Probe;
}
