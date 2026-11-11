#include "CepheusSmemMapProbe.h"
#include <Guid/EventGroup.h>
#include <Guid/HobList.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC EFI_EVENT mSmemExitEvent;
STATIC BOOLEAN mSmemPrepared, mSmemClosed, mSmemAttempted, mSmemExited;
STATIC UINT64 mSmemCpu;

STATIC VOID EFIAPI
CeSmemExit (EFI_EVENT Event, VOID *Context)
{
  (VOID)Event; (VOID)Context;
  mSmemClosed = TRUE;
  mSmemExited = TRUE;
}

STATIC CE_SMEM_CPU_MAP
CeSmemCpuMap (VOID)
{
  CE_SMEM_CPU_MAP C = {0};
  __asm__ volatile ("mrs %0, CurrentEL\n\tmrs %1, MPIDR_EL1"
                    : "=r"(C.El), "=r"(C.Cpu) : : "memory");
  if (C.El == 4) {
    __asm__ volatile ("mrs %0, sctlr_el1\n\tmrs %1, tcr_el1\n\tmrs %2, ttbr0_el1\n\t"
                      "mrs %3, ttbr1_el1\n\tmrs %4, mair_el1\n\tmrs %5, id_aa64mmfr0_el1"
                      : "=r"(C.Sctlr), "=r"(C.Tcr), "=r"(C.Ttbr0), "=r"(C.Ttbr1),
                        "=r"(C.Mair), "=r"(C.Mmfr0) : : "memory");
  }
  return C;
}

/* PAR is shared CPU state: mask local interrupts and restore both PAR and DAIF.
 * No load from Address, cache maintenance or translation-table update occurs. */
STATIC UINT64
CeSmemPar (UINT64 Address)
{
  UINT64 SavedDaif, SavedPar, Par;
  __asm__ volatile ("mrs %0, daif\n\tmsr daifset, #0xf\n\tmrs %1, par_el1\n\t"
                    "at s1e1r, %3\n\tisb\n\tmrs %2, par_el1\n\t"
                    "msr par_el1, %1\n\tmsr daif, %0\n\tisb"
                    : "=&r"(SavedDaif), "=&r"(SavedPar), "=&r"(Par)
                    : "r"(Address) : "memory");
  return Par;
}

STATIC BOOLEAN
CeSmemCpuValid (CONST CE_SMEM_CPU_MAP *C)
{
  /* The existing Cepheus diagnostic path is EL1. Other regimes are unsupported,
   * not translated using the wrong EL. SM8150 profile is <=48-bit PA, no LPA2. */
  return C->El == 4 && C->Cpu == mSmemCpu && (C->Sctlr & 5) == 5 &&
    (C->Mmfr0 & 15) <= 5 && ((C->Mmfr0 >> 28) & 15) == 0 &&
    ((C->Tcr >> 14) & 3) == 0 && !(C->Tcr & (1ULL << 7)) &&
    (C->Tcr & 63) >= 16 && (C->Tcr & 63) <= 32 &&
    !(C->Tcr & ((1ULL << 59) | (1ULL << 37) | (1ULL << 39) | (1ULL << 40)));
}

STATIC BOOLEAN
CeSmemOverlap (UINT64 Base, UINT64 Bytes)
{
  return Bytes && Base <= MAX_UINT64 - Bytes && Base < CE_SMEM_PROBE_BASE + CE_SMEM_PROBE_BYTES &&
    CE_SMEM_PROBE_BASE < Base + Bytes;
}

EFI_STATUS
CepheusSmemMapPrepare (CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *Profile)
{
  if (mSmemPrepared || mSmemClosed || mSmemAttempted) { return EFI_ALREADY_STARTED; }
  if (!Profile || (UINTN)Profile > MAX_UINTN - sizeof (*Profile) ||
      CeSmemOverlap ((UINTN)Profile, sizeof (*Profile)) ||
      !gBS || !gBS->CreateEventEx || !gBS->CloseEvent ||
      !gDS || gDS->Hdr.Signature != DXE_SERVICES_SIGNATURE ||
      gDS->Hdr.HeaderSize < OFFSET_OF (EFI_DXE_SERVICES, GetMemorySpaceDescriptor) +
        sizeof (gDS->GetMemorySpaceDescriptor) || !gDS->GetMemorySpaceDescriptor ||
      CompareMem (Profile->Name, "SMEM\0", 5) || Profile->Address != CE_SMEM_PROBE_BASE ||
      Profile->Length != CE_SMEM_PROBE_BYTES || Profile->HobOption != AddMem ||
      Profile->ResourceType != EFI_RESOURCE_MEMORY_RESERVED ||
      Profile->ResourceAttribute != EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE ||
      Profile->MemoryType != EfiReservedMemoryType ||
      Profile->ArmAttributes != ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED) {
    return EFI_INVALID_PARAMETER;
  }
  CE_SMEM_CPU_MAP Cpu = CeSmemCpuMap ();
  if (Cpu.El != 4) { return EFI_UNSUPPORTED; }
  mSmemCpu = Cpu.Cpu;
  if (!CeSmemCpuValid (&Cpu)) { return EFI_UNSUPPORTED; }
  EFI_STATUS Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, CeSmemExit, NULL,
                                         &gEfiEventExitBootServicesGuid, &mSmemExitEvent);
  if (EFI_ERROR (Status)) { return Status; }
  mSmemPrepared = TRUE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CeSmemResource (CE_SMEM_MAP_EVIDENCE *Output)
{
  VOID *HobList = NULL;
  EFI_STATUS Status = EfiGetSystemConfigurationTable (&gEfiHobListGuid, &HobList);
  if (EFI_ERROR (Status) || !HobList || ((UINTN)HobList & 7)) { return EFI_NOT_FOUND; }
  if ((UINTN)HobList > MAX_UINTN - sizeof (EFI_HOB_HANDOFF_INFO_TABLE) ||
      CeSmemOverlap ((UINTN)HobList, sizeof (EFI_HOB_HANDOFF_INFO_TABLE))) {
    return EFI_COMPROMISED_DATA;
  }
  EFI_HOB_HANDOFF_INFO_TABLE Phit;
  CopyMem (&Phit, HobList, sizeof (Phit));
  UINT64 Begin = (UINTN)HobList, End = Phit.EfiEndOfHobList;
  if (Phit.Header.HobType != EFI_HOB_TYPE_HANDOFF ||
      Phit.Header.HobLength != sizeof (Phit) || Phit.Header.Reserved ||
      Phit.Version != EFI_HOB_HANDOFF_TABLE_VERSION || (End & 7) ||
      End < Begin || End - Begin > 1024 * 1024 || End > MAX_UINTN - 8 ||
      End - Begin < sizeof (Phit) || CeSmemOverlap (Begin, End + 8 - Begin)) {
    return EFI_COMPROMISED_DATA;
  }
  for (UINT64 Cursor = Begin; Cursor <= End;) {
    EFI_HOB_GENERIC_HEADER Header;
    CopyMem (&Header, (CONST VOID *)(UINTN)Cursor, sizeof (Header));
    if (++Output->Hobs > 16384 || Header.Reserved || Header.HobLength < sizeof (Header) ||
        (Header.HobLength & 7) || Header.HobLength > End + 8 - Cursor) {
      return EFI_COMPROMISED_DATA;
    }
    if (Header.HobType == EFI_HOB_TYPE_END_OF_HOB_LIST) {
      if (Cursor != End || Header.HobLength != sizeof (Header) || Output->ResourceMatches != 1) {
        return EFI_COMPROMISED_DATA;
      }
      Output->ResourceComplete = TRUE;
      return EFI_SUCCESS;
    }
    if (Header.HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR ||
        Header.HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR2) {
      UINTN Needed = Header.HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR ?
        sizeof (EFI_HOB_RESOURCE_DESCRIPTOR) : sizeof (EFI_HOB_RESOURCE_DESCRIPTOR_V2);
      if (Header.HobLength != Needed) { return EFI_COMPROMISED_DATA; }
      EFI_HOB_RESOURCE_DESCRIPTOR R;
      CopyMem (&R, (CONST VOID *)(UINTN)Cursor, sizeof (R));
      if (!R.ResourceLength || R.PhysicalStart > MAX_UINT64 - R.ResourceLength) {
        return EFI_COMPROMISED_DATA;
      }
      if (CeSmemOverlap (R.PhysicalStart, R.ResourceLength)) {
        Output->Resource = R;
        if (++Output->ResourceMatches != 1 || R.PhysicalStart != CE_SMEM_PROBE_BASE ||
            R.ResourceLength != CE_SMEM_PROBE_BYTES ||
            R.ResourceType != EFI_RESOURCE_MEMORY_RESERVED ||
            R.ResourceAttribute != EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE) {
          return EFI_COMPROMISED_DATA;
        }
        /* V2 mapping fields have not been profiled; do not silently ignore them. */
        if (Header.HobType != EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) { return EFI_UNSUPPORTED; }
      }
    } else if (Header.HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
      if (Header.HobLength < sizeof (EFI_HOB_MEMORY_ALLOCATION)) { return EFI_COMPROMISED_DATA; }
      EFI_HOB_MEMORY_ALLOCATION A;
      CopyMem (&A, (CONST VOID *)(UINTN)Cursor, sizeof (A));
      if (!A.AllocDescriptor.MemoryLength ||
          A.AllocDescriptor.MemoryBaseAddress > MAX_UINT64 - A.AllocDescriptor.MemoryLength ||
          CeSmemOverlap (A.AllocDescriptor.MemoryBaseAddress, A.AllocDescriptor.MemoryLength)) {
        return EFI_COMPROMISED_DATA;
      }
    }
    Cursor += Header.HobLength;
  }
  return EFI_COMPROMISED_DATA;
}

EFI_STATUS
CepheusSmemMapProbe (CE_SMEM_MAP_EVIDENCE *Output)
{
  if (!Output || (UINTN)Output > MAX_UINTN - sizeof (*Output) ||
      CeSmemOverlap ((UINTN)Output, sizeof (*Output))) { return EFI_INVALID_PARAMETER; }
  if (!mSmemPrepared || mSmemClosed || mSmemAttempted || !gBS || !gDS) {
    return EFI_ACCESS_DENIED;
  }
  mSmemAttempted = TRUE;
  ZeroMem (Output, sizeof (*Output));
  Output->Status = EFI_ACCESS_DENIED;
  Output->Before = CeSmemCpuMap ();
  if (!CeSmemCpuValid (&Output->Before)) { return Output->Status; }
  EFI_STATUS Status = CeSmemResource (Output);
  if (!EFI_ERROR (Status) && !mSmemClosed) {
    Status = gDS->GetMemorySpaceDescriptor (CE_SMEM_PROBE_BASE, &Output->Gcd);
    if (!EFI_ERROR (Status)) {
      CONST EFI_GCD_MEMORY_SPACE_DESCRIPTOR *D = &Output->Gcd;
      if (D->BaseAddress != CE_SMEM_PROBE_BASE || D->Length != CE_SMEM_PROBE_BYTES ||
          D->GcdMemoryType != EfiGcdMemoryTypeReserved || D->ImageHandle || D->DeviceHandle ||
          D->Attributes & EFI_MEMORY_RP) {
        Status = EFI_COMPROMISED_DATA;
      } else { Output->GcdComplete = TRUE; }
    }
  }
  for (UINT32 I = 0; !EFI_ERROR (Status) && I < CE_SMEM_PROBE_PAGES; I++) {
    CE_SMEM_CPU_MAP Cpu = CeSmemCpuMap ();
    if (mSmemClosed || CompareMem (&Cpu, &Output->Before, sizeof (Cpu))) {
      Status = EFI_ACCESS_DENIED; break;
    }
    UINT64 Page = CE_SMEM_PROBE_BASE + (UINT64)I * 4096;
    UINT64 Par = CeSmemPar (Page);
    Output->Par[Output->Pages++] = Par;
    if (Par & 1 || (Par >> 56) != 0x44 || Par & 0x00ff000000000000ULL ||
        (Par & 0x0000fffffffff000ULL) != Page) {
      Status = EFI_NO_MAPPING;
    }
  }
  Output->After = CeSmemCpuMap ();
  Output->CpuStable = !CompareMem (&Output->Before, &Output->After, sizeof (Output->Before));
  if (mSmemClosed || !Output->CpuStable) { Status = EFI_ACCESS_DENIED; }
  Output->Status = Status;
  return Status;
}

EFI_STATUS
CepheusSmemMapClose (VOID)
{
  if (!mSmemPrepared || !mSmemExitEvent || !gBS) { return EFI_NOT_STARTED; }
  /* On ExitBootServices, service access is forbidden and the driver must not unload. */
  if (mSmemExited) { return EFI_ACCESS_DENIED; }
  mSmemClosed = TRUE;
  EFI_STATUS Status = gBS->CloseEvent (mSmemExitEvent);
  if (!EFI_ERROR (Status)) { mSmemExitEvent = NULL; }
  return Status;
}
