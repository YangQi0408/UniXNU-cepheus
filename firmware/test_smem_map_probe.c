#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "map-probe-host.c"

#define CHECK(c) do { if (!(c)) {fprintf (stderr, "failed line %u\n", __LINE__); return 1;} } while (0)
STATIC EFI_BOOT_SERVICES Bs;
STATIC EFI_DXE_SERVICES Ds;
EFI_BOOT_SERVICES *gBS = &Bs;
EFI_DXE_SERVICES *gDS = &Ds;
EFI_GUID gEfiEventExitBootServicesGuid, gEfiHobListGuid;
STATIC _Alignas(8) UINT8 Hobs[8192];
STATIC CE_SMEM_CPU_MAP Cpu;
STATIC ARM_MEMORY_REGION_DESCRIPTOR_EX Profile;
STATIC CE_SMEM_MAP_EVIDENCE Evidence;
STATIC EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor;
STATIC EFI_STATUS EventStatus, TableStatus, GcdStatus, CloseStatus;
STATIC unsigned Creates, Closes, Tables, Gets, Pars, CpuCalls, FaultPage, ChangeAt;
STATIC unsigned ExitAt, ChangeAfterPar;
STATIC UINT64 ParBits;

VOID *EFIAPI CopyMem (VOID *Destination, CONST VOID *Source, UINTN Bytes)
{ return memmove (Destination, Source, (size_t)Bytes); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Bytes)
{ return memset (Buffer, 0, (size_t)Bytes); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Bytes)
{ return memcmp (A, B, (size_t)Bytes); }

CE_SMEM_CPU_MAP CeSmemCpuMap (VOID)
{
  ++CpuCalls;
  if (CpuCalls == ChangeAt) { Cpu.Ttbr0 ^= 4096; }
  return Cpu;
}
UINT64 CeSmemPar (UINT64 Address)
{
  ++Pars;
  if (Pars == ExitAt) { CeSmemExit (NULL, NULL); }
  if (Pars == ChangeAfterPar) { Cpu.Mair ^= 1; }
  return ((0x44ULL << 56) | Address) ^ (Pars == FaultPage ? ParBits : 0);
}
STATIC EFI_STATUS EFIAPI
Create (UINT32 Type, EFI_TPL Tpl, EFI_EVENT_NOTIFY Notify, CONST VOID *Context,
        CONST EFI_GUID *Group, EFI_EVENT *Event)
{
  ++Creates;
  if (Type != EVT_NOTIFY_SIGNAL || Tpl != TPL_NOTIFY || Notify != CeSmemExit ||
      Context || Group != &gEfiEventExitBootServicesGuid) { abort (); }
  if (!EFI_ERROR (EventStatus)) { *Event = (VOID *)1; }
  return EventStatus;
}
STATIC EFI_STATUS EFIAPI Close (EFI_EVENT Event)
{
  ++Closes;
  if (Event != (VOID *)1) { abort (); }
  return CloseStatus;
}
EFI_STATUS EFIAPI EfiGetSystemConfigurationTable (EFI_GUID *Guid, VOID **Table)
{
  ++Tables;
  if (Guid != &gEfiHobListGuid) { abort (); }
  if (!EFI_ERROR (TableStatus)) { *Table = Hobs; }
  return TableStatus;
}
STATIC EFI_STATUS EFIAPI Get (EFI_PHYSICAL_ADDRESS Address,
                             EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Output)
{
  ++Gets;
  if (Address != CE_SMEM_PROBE_BASE) { abort (); }
  if (!EFI_ERROR (GcdStatus)) { *Output = Descriptor; }
  return GcdStatus;
}
STATIC EFI_HOB_HANDOFF_INFO_TABLE *Phit (VOID) { return (VOID *)Hobs; }
STATIC EFI_HOB_RESOURCE_DESCRIPTOR *Resource (VOID)
{ return (VOID *)(Hobs + sizeof (EFI_HOB_HANDOFF_INFO_TABLE)); }
STATIC VOID EndAt (UINTN Offset)
{
  Phit ()->EfiEndOfHobList = (UINTN)Hobs + Offset;
  EFI_HOB_GENERIC_HEADER End = {EFI_HOB_TYPE_END_OF_HOB_LIST, sizeof (End), 0};
  memcpy (Hobs + Offset, &End, sizeof (End));
}
STATIC VOID Fixture (VOID)
{
  mSmemExitEvent = NULL;
  mSmemPrepared = mSmemClosed = mSmemAttempted = mSmemExited = FALSE;
  Creates = Closes = Tables = Gets = Pars = CpuCalls = FaultPage = ChangeAt = 0;
  ExitAt = ChangeAfterPar = 0; ParBits = 0;
  EventStatus = TableStatus = GcdStatus = CloseStatus = EFI_SUCCESS;
  memset (&Bs, 0, sizeof (Bs)); memset (&Ds, 0, sizeof (Ds));
  gBS = &Bs; gDS = &Ds;
  Bs.CreateEventEx = Create; Bs.CloseEvent = Close;
  Ds.Hdr.Signature = DXE_SERVICES_SIGNATURE; Ds.Hdr.HeaderSize = sizeof (Ds);
  Ds.GetMemorySpaceDescriptor = Get;
  Cpu = (CE_SMEM_CPU_MAP){4, 0x80000000, 5, 32, 0x9ff8d000, 0, 0xff4400, 5};
  Profile = (ARM_MEMORY_REGION_DESCRIPTOR_EX){"SMEM", CE_SMEM_PROBE_BASE,
    CE_SMEM_PROBE_BYTES, AddMem, EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE, EfiReservedMemoryType,
    ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED};
  Descriptor = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){.BaseAddress = CE_SMEM_PROBE_BASE,
    .Length = CE_SMEM_PROBE_BYTES, .GcdMemoryType = EfiGcdMemoryTypeReserved};
  memset (Hobs, 0, sizeof (Hobs));
  Phit ()->Header = (EFI_HOB_GENERIC_HEADER){EFI_HOB_TYPE_HANDOFF, sizeof (*Phit ()), 0};
  Phit ()->Version = EFI_HOB_HANDOFF_TABLE_VERSION;
  *Resource () = (EFI_HOB_RESOURCE_DESCRIPTOR){
    .Header = {EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, sizeof (*Resource ()), 0},
    .ResourceType = EFI_RESOURCE_MEMORY_RESERVED,
    .ResourceAttribute = EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE,
    .PhysicalStart = CE_SMEM_PROBE_BASE, .ResourceLength = CE_SMEM_PROBE_BYTES};
  EndAt (sizeof (*Phit ()) + sizeof (*Resource ()));
}
STATIC EFI_STATUS Probe (VOID)
{
  EFI_STATUS Status = CepheusSmemMapPrepare (&Profile);
  return EFI_ERROR (Status) ? Status : CepheusSmemMapProbe (&Evidence);
}
int main (void)
{
  Fixture (); CHECK (!Probe ());
  CHECK (Evidence.Pages == 512 && Pars == 512 && Creates == 1 && Gets == 1 && Tables == 1);
  CHECK (Evidence.ResourceComplete && Evidence.GcdComplete && Evidence.CpuStable);
  CE_SMEM_MAP_EVIDENCE Saved = Evidence;
  CHECK (CepheusSmemMapProbe (&Evidence) == EFI_ACCESS_DENIED && Pars == 512);
  CHECK (!memcmp (&Evidence, &Saved, sizeof (Saved)));
  CHECK (!CepheusSmemMapClose () && Closes == 1);
  CHECK (CepheusSmemMapProbe (&Evidence) == EFI_ACCESS_DENIED && Evidence.Pages == 512);
  CHECK (!memcmp (&Evidence, &Saved, sizeof (Saved)));
  CHECK (CepheusSmemMapPrepare (&Profile) == EFI_ALREADY_STARTED);
  for (unsigned Page = 1; Page <= 512; Page++) {
    Fixture (); FaultPage = Page; ParBits = 1;
    CHECK (Probe () == EFI_NO_MAPPING && Pars == Page && Evidence.Pages == Page);
    CHECK (!CepheusSmemMapClose ());
  }
  for (unsigned Bit = 12; Bit < 64; Bit++) {
    Fixture (); FaultPage = 3; ParBits = 1ULL << Bit;
    CHECK (Probe () == EFI_NO_MAPPING && Pars == 3);
  }
  Fixture (); ChangeAt = 17;
  CHECK (Probe () == EFI_ACCESS_DENIED && Pars < 512 && !Evidence.CpuStable);
  Fixture (); ChangeAfterPar = 512;
  CHECK (Probe () == EFI_ACCESS_DENIED && Pars == 512 && !Evidence.CpuStable);
  Fixture (); ExitAt = 1;
  CHECK (Probe () == EFI_ACCESS_DENIED && Pars == 1);
  CHECK (CepheusSmemMapClose () == EFI_ACCESS_DENIED && !Closes);
  Fixture (); CHECK (!CepheusSmemMapPrepare (&Profile)); CeSmemExit (NULL, NULL);
  CHECK (CepheusSmemMapProbe (&Evidence) == EFI_ACCESS_DENIED && !Tables && !Pars);
  Fixture (); EventStatus = EFI_OUT_OF_RESOURCES;
  CHECK (Probe () == EventStatus && Creates == 1 && !Tables);
  Fixture (); TableStatus = EFI_NOT_FOUND;
  CHECK (Probe () == TableStatus && !Gets && !Pars);
  Fixture (); GcdStatus = EFI_NOT_FOUND;
  CHECK (Probe () == GcdStatus && !Pars);
  for (unsigned Mode = 0; Mode < 7; Mode++) {
    Fixture ();
    switch (Mode) {
      case 0: Descriptor.BaseAddress += 4096; break;
      case 1: Descriptor.Length -= 4096; break;
      case 2: Descriptor.GcdMemoryType = EfiGcdMemoryTypeSystemMemory; break;
      case 3: Descriptor.GcdMemoryType = EfiGcdMemoryTypeMemoryMappedIo; break;
      case 4: Descriptor.ImageHandle = (VOID *)1; break;
      case 5: Descriptor.DeviceHandle = (VOID *)1; break;
      default: Descriptor.Attributes = EFI_MEMORY_RP; break;
    }
    CHECK (Probe () == EFI_COMPROMISED_DATA && !Pars && !Evidence.GcdComplete);
  }
  for (unsigned Mode = 0; Mode < 8; Mode++) {
    Fixture ();
    switch (Mode) {
      case 0: Phit ()->Version++; break;
      case 1: Phit ()->Header.Reserved = 1; break;
      case 2: Phit ()->EfiEndOfHobList++; break;
      case 3: Resource ()->Header.HobLength = 0; break;
      case 4: Resource ()->PhysicalStart += 4096; break;
      case 5: Resource ()->ResourceType = EFI_RESOURCE_SYSTEM_MEMORY; break;
      case 6: Resource ()->ResourceAttribute = EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE; break;
      default: Resource ()->ResourceLength = MAX_UINT64; break;
    }
    CHECK (Probe () == EFI_COMPROMISED_DATA && !Gets && !Pars);
  }
  Fixture (); EndAt (sizeof (*Phit ()));
  CHECK (Probe () == EFI_COMPROMISED_DATA && !Gets);
  Fixture ();
  UINTN Offset = sizeof (*Phit ()) + sizeof (*Resource ());
  memcpy (Hobs + Offset, Resource (), sizeof (*Resource ()));
  EndAt (Offset + sizeof (*Resource ()));
  CHECK (Probe () == EFI_COMPROMISED_DATA && Evidence.ResourceMatches == 2 && !Gets);
  Fixture ();
  Resource ()->Header.HobType = EFI_HOB_TYPE_RESOURCE_DESCRIPTOR2;
  Resource ()->Header.HobLength = sizeof (EFI_HOB_RESOURCE_DESCRIPTOR_V2);
  EndAt (sizeof (*Phit ()) + sizeof (EFI_HOB_RESOURCE_DESCRIPTOR_V2));
  CHECK (Probe () == EFI_UNSUPPORTED && !Gets);
  Fixture ();
  EFI_HOB_MEMORY_ALLOCATION A = {.Header = {EFI_HOB_TYPE_MEMORY_ALLOCATION, sizeof (A), 0},
    .AllocDescriptor = {.MemoryBaseAddress = CE_SMEM_PROBE_BASE, .MemoryLength = 4096}};
  memcpy (Hobs + Offset, &A, sizeof (A)); EndAt (Offset + sizeof (A));
  CHECK (Probe () == EFI_COMPROMISED_DATA && !Gets);
  for (unsigned Mode = 0; Mode < 8; Mode++) {
    Fixture ();
    switch (Mode) {
      case 0: Profile.Address++; break;
      case 1: Profile.Length--; break;
      case 2: Profile.Name[0] = 'X'; break;
      case 3: Profile.HobOption = NoHob; break;
      case 4: Profile.ResourceType = EFI_RESOURCE_SYSTEM_MEMORY; break;
      case 5: Profile.ResourceAttribute = 0; break;
      case 6: Profile.MemoryType = EfiConventionalMemory; break;
      default: Profile.ArmAttributes = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK; break;
    }
    CHECK (Probe () == EFI_INVALID_PARAMETER && !Creates && !Tables);
  }
  const UINT64 TcrBits[] = {1ULL << 7, 1ULL << 14, 1ULL << 37,
    1ULL << 39, 1ULL << 40, 1ULL << 59};
  for (unsigned I = 0; I < sizeof (TcrBits) / sizeof (*TcrBits); I++) {
    Fixture (); Cpu.Tcr |= TcrBits[I];
    CHECK (Probe () == EFI_UNSUPPORTED && !Creates);
  }
  Fixture (); Cpu.El = 8; CHECK (Probe () == EFI_UNSUPPORTED && !Creates);
  Fixture (); Cpu.Mmfr0 = 6; CHECK (Probe () == EFI_UNSUPPORTED && !Creates);
  Fixture (); Cpu.Mmfr0 |= 1ULL << 28; CHECK (Probe () == EFI_UNSUPPORTED && !Creates);
  Fixture (); Cpu.Sctlr = 1; CHECK (Probe () == EFI_UNSUPPORTED && !Creates);
  Fixture (); CHECK (!CepheusSmemMapPrepare (&Profile)); CloseStatus = EFI_DEVICE_ERROR;
  CHECK (CepheusSmemMapClose () == EFI_DEVICE_ERROR && Closes == 1 && mSmemExitEvent);
  CHECK (CepheusSmemMapProbe (&Evidence) == EFI_ACCESS_DENIED && !Pars);
  CloseStatus = EFI_SUCCESS;
  CHECK (!CepheusSmemMapClose () && Closes == 2 && !mSmemExitEvent);
  Fixture (); CHECK (CepheusSmemMapProbe ((VOID *)(UINTN)CE_SMEM_PROBE_BASE) == EFI_INVALID_PARAMETER);
  CHECK (CepheusSmemMapPrepare ((VOID *)(UINTN)CE_SMEM_PROBE_BASE) == EFI_INVALID_PARAMETER);
  /* Argument checks reject wrapping pointers before dereference. Accessible output
   * is a caller precondition; arbitrary inaccessible output is not probed. */
  CHECK (CepheusSmemMapProbe ((VOID *)(MAX_UINTN - 3)) == EFI_INVALID_PARAMETER);
  CHECK (CepheusSmemMapPrepare ((VOID *)(MAX_UINTN - 3)) == EFI_INVALID_PARAMETER);
  puts ("SMEM map probe: 512 page faults, CPU/EBS changes, HOB/GCD/profile rejection PASS; services/AT mocked, no SMEM loads");
  return 0;
}
