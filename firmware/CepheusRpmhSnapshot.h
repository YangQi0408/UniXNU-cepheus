/* Pre-EBS diagnostic. Post-EBS only seals/copies owned data; no pointer traversal. */
#ifndef CEPHEUS_RPMH_SNAPSHOT_FIRMWARE_H
#define CEPHEUS_RPMH_SNAPSHOT_FIRMWARE_H
#include "cepheus_rpmh_pool.h"
#include "CepheusRpmhSnapshotIdentity.h"

typedef struct {
  UINT64 Base, Bytes;
  CONST UINT8 *Header;
  UINTN HeaderBytes, BaseOffset, TextRva, TextWords;
  CONST UINT64 *Text;
  CONST UINT32 *Fixups;
} CE_RPMH_IMAGE;
typedef struct {
  CONST EFI_MEMORY_DESCRIPTOR *Map;
  UINTN MapBytes, Stride;
  CE_RPMH_IMAGE Image[4];
  UINT64 RpmhProtocol, CmdDbProtocol;
  UINT32 Address[2];
  BOOLEAN Prepared;
} CE_RPMH_CONTEXT;

STATIC CE_RPMH_CONTEXT mCepheusRpmhContext;
STATIC struct ceph_rpmh_snapshot mCepheusRpmhSnapshot;
STATIC struct ceph_rpmh_snapshot_scratch mCepheusRpmhScratch;
STATIC BOOLEAN mCepheusRpmhAttempted;
STATIC EFI_GUID mCeRpmhGuid = {0xe1d0b573,0xf29f,0x495d,{0x93,0x52,0xca,0x0d,0xd4,0x2a,0x0a,0x74}};
STATIC EFI_GUID mCeCmdDbGuid = {0x1c34f691,0xd33d,0x4e14,{0xb8,0xf8,0x32,0xc6,0xc0,0x29,0xe9,0x5b}};

STATIC int
CepheusRpmhMapValid (CE_RPMH_CONTEXT *C)
{
  if ((C->Map == NULL) || (C->Stride < sizeof (EFI_MEMORY_DESCRIPTOR)) ||
      (C->Stride > 256) || (C->MapBytes == 0) || (C->MapBytes % C->Stride) ||
      (C->Stride & 7) || ((UINTN)C->Map & 7) ||
      (C->MapBytes / C->Stride > 2048)) { return -1; }
  for (UINTN I = 0; I < C->MapBytes; I += C->Stride) {
    CONST EFI_MEMORY_DESCRIPTOR *D = (CONST VOID *)((CONST UINT8 *)C->Map + I);
    if ((D->PhysicalStart & 4095) || (D->NumberOfPages > (MAX_UINT64 - D->PhysicalStart) / 4096)) { return -1; }
    UINT64 End = D->PhysicalStart + D->NumberOfPages * 4096;
    for (UINTN J = 0; J < I; J += C->Stride) {
      CONST EFI_MEMORY_DESCRIPTOR *P = (CONST VOID *)((CONST UINT8 *)C->Map + J);
      UINT64 PreviousEnd = P->PhysicalStart + P->NumberOfPages * 4096;
      if ((D->NumberOfPages != 0) && (P->NumberOfPages != 0) &&
          (D->PhysicalStart < PreviousEnd) && (P->PhysicalStart < End)) { return -1; }
    }
  }
  return 0;
}

STATIC int
CepheusRpmhRamRange (CE_RPMH_CONTEXT *C, UINT64 Address, UINT32 Bytes, BOOLEAN DataOnly)
{
  if ((Bytes == 0) || (Address > MAX_UINT64 - Bytes)) { return -1; }
  UINT64 End = Address + Bytes;
  for (UINT64 Cursor = Address; Cursor < End;) {
    UINT64 Next = Cursor;
    for (UINTN I = 0; I < C->MapBytes; I += C->Stride) {
      CONST EFI_MEMORY_DESCRIPTOR *D = (CONST VOID *)((CONST UINT8 *)C->Map + I);
      UINT64 Limit = D->PhysicalStart + D->NumberOfPages * 4096;
      if ((D->PhysicalStart <= Cursor) && (Cursor < Limit)) {
        if (((D->Type != EfiBootServicesData) && (DataOnly || (D->Type != EfiBootServicesCode))) ||
            ((D->Attribute & (EFI_MEMORY_WB | EFI_MEMORY_RP)) != EFI_MEMORY_WB) ||
            ((D->VirtualStart != 0) && (D->VirtualStart != D->PhysicalStart))) { return -1; }
        Next = Limit < End ? Limit : End;
        break;
      }
    }
    if (Next == Cursor) { return -1; }
    Cursor = Next;
  }
  return 0;
}

STATIC int
CepheusRpmhRead (void *Context, uint64_t Address, void *Output, uint32_t Bytes)
{
  CE_RPMH_CONTEXT *C = Context;
  if (CepheusRpmhRamRange (C, Address, Bytes, FALSE)) { return -1; }
  volatile CONST UINT8 *Source = (CONST VOID *)(UINTN)Address;
  UINT8 *Target = Output;
  for (UINT32 I = 0; I < Bytes; I++) { Target[I] = Source[I]; }
  return 0;
}

STATIC int
CepheusRpmhPool (void *Context, uint64_t Address, uint32_t Bytes)
{
  CE_RPMH_CONTEXT *C = Context;
  UINT64 Total = (((UINT64)Bytes + 11) & ~UINT64_C(7)) + 40;
  if ((Address < 28) || (Total > MAX_UINT32) ||
      CepheusRpmhRamRange (C, Address - 28, (UINT32)Total, TRUE)) { return -1; }
  struct ceph_rpmh_snapshot_ops Ops = {C, CepheusRpmhRead, NULL};
  return ceph_rpmh_pool_check (&Ops, Address, Bytes);
}

STATIC int
CepheusRpmhImageValid (CE_RPMH_CONTEXT *C, CE_RPMH_IMAGE *Image)
{
  if ((Image->Base == 0) || (Image->Base & 4095) || (Image->Bytes == 0) ||
      (Image->Bytes > MAX_UINT32) || (Image->Base > MAX_UINT64 - Image->Bytes) ||
      CepheusRpmhRamRange (C, Image->Base, (UINT32)Image->Bytes, FALSE)) { return -1; }
  UINT8 Word[8];
  for (UINTN I = 0; I < Image->HeaderBytes; I++) {
    if ((I >= Image->BaseOffset) && (I < Image->BaseOffset + 8)) { continue; }
    if (CepheusRpmhRead (C, Image->Base + I, Word, 1) || (Word[0] != Image->Header[I])) { return -1; }
  }
  if (CepheusRpmhRead (C, Image->Base + Image->BaseOffset, Word, 8) ||
      (ceph_rpmh_u64 (Word) != Image->Base)) { return -1; }
  UINTN Fixup = 0;
  for (UINTN I = 0; I < Image->TextWords; I++) {
    if (CepheusRpmhRead (C, Image->Base + Image->TextRva + I * 8, Word, 8)) { return -1; }
    UINT64 Expected = Image->Text[I];
    if (Image->Fixups[Fixup] == I) { Expected += Image->Base; Fixup++; }
    if (ceph_rpmh_u64 (Word) != Expected) { return -1; }
  }
  return 0;
}

STATIC VOID
CepheusRpmhPrepare (VOID)
{
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  CE_RPMH_CONTEXT *C = &mCepheusRpmhContext;
  VOID *Protocol = NULL;
  if (mCepheusRpmhAttempted) { return; }
  mCepheusRpmhAttempted = TRUE;
  ZeroMem (C, sizeof (*C));
  C->Image[0] = (CE_RPMH_IMAGE){0, CE_RPMH_IMAGE_BYTES, mCeRpmhHeader, sizeof (mCeRpmhHeader), CE_RPMH_BASE_OFFSET, CE_RPMH_TEXT_RVA, sizeof (mCeRpmhText)/8, mCeRpmhText, mCeRpmhFixups};
  C->Image[1] = (CE_RPMH_IMAGE){0, CE_DAL_IMAGE_BYTES, mCeDalHeader, sizeof (mCeDalHeader), CE_DAL_BASE_OFFSET, CE_DAL_TEXT_RVA, sizeof (mCeDalText)/8, mCeDalText, mCeDalFixups};
  C->Image[2] = (CE_RPMH_IMAGE){0, CE_CMDDB_IMAGE_BYTES, mCeCmdDbHeader, sizeof (mCeCmdDbHeader), CE_CMDDB_BASE_OFFSET, CE_CMDDB_TEXT_RVA, sizeof (mCeCmdDbText)/8, mCeCmdDbText, mCeCmdDbFixups};
  C->Image[3] = (CE_RPMH_IMAGE){0, CE_CORE_IMAGE_BYTES, mCeCoreHeader, sizeof (mCeCoreHeader), CE_CORE_BASE_OFFSET, CE_CORE_TEXT_RVA, sizeof (mCeCoreText)/8, mCeCoreText, mCeCoreFixups};
  if (EFI_ERROR (gBS->LocateProtocol (&mCeRpmhGuid, NULL, &Protocol))) { return; }
  C->RpmhProtocol = (UINTN)Protocol;
  Protocol = NULL;
  if (EFI_ERROR (gBS->LocateProtocol (&mCeCmdDbGuid, NULL, &Protocol))) { return; }
  C->CmdDbProtocol = (UINTN)Protocol;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &Count, &Handles))) { return; }
  if (Count <= 512) {
    for (UINTN I = 0; I < Count; I++) {
      EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
      if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)) || (Loaded == NULL)) { continue; }
      UINT64 Base = (UINTN)Loaded->ImageBase;
      if ((Loaded->ImageSize == CE_RPMH_IMAGE_BYTES) && (Base <= MAX_UINT64 - 0xe0f8) && (Base + 0xe0f8 == C->RpmhProtocol)) { C->Image[0].Base = Base; }
      if ((Loaded->ImageSize == CE_CMDDB_IMAGE_BYTES) && (Base <= MAX_UINT64 - 0x6078) && (Base + 0x6078 == C->CmdDbProtocol)) { C->Image[2].Base = Base; }
      /* The DAL/core candidates are bound to live function pointers below. */
      if ((Loaded->ImageSize == CE_DAL_IMAGE_BYTES) && (Base <= MAX_UINT64 - 0x3390)) {
        C->Image[1].Base = C->Image[1].Base == 0 ? Base : MAX_UINT64;
      }
      if ((Loaded->ImageSize == CE_CORE_IMAGE_BYTES) && ((UINTN)gBS->AllocatePool >= 0x5af8) &&
          (Base == (UINTN)gBS->AllocatePool - 0x5af8) && ((UINTN)gBS->FreePool == Base + 0x5ea0)) { C->Image[3].Base = Base; }
    }
  }
  FreePool (Handles);
  C->Prepared = TRUE;
}

/* Pre-EBS only. Exact code binding precedes the two read-only CmdDb calls. */
STATIC VOID
CepheusRpmhBind (CONST EFI_MEMORY_DESCRIPTOR *Map, UINTN Bytes, UINTN Stride)
{
  CE_RPMH_CONTEXT *C = &mCepheusRpmhContext;
  UINT8 Word[8];
  C->Map = Map; C->MapBytes = Bytes; C->Stride = Stride;
  if (!C->Prepared || CepheusRpmhMapValid (C)) { C->Prepared = FALSE; return; }
  for (UINTN I = 0; I < 4; I++) {
    if (CepheusRpmhImageValid (C, &C->Image[I])) { C->Prepared = FALSE; return; }
  }
  if (CepheusRpmhRead (C, C->Image[0].Base + 0xe610, Word, 8) ||
      (ceph_rpmh_u64 (Word) != C->Image[1].Base + 0x1f0d0) ||
      CepheusRpmhRead (C, C->Image[1].Base + 0x1f160, Word, 8) ||
      (ceph_rpmh_u64 (Word) != C->Image[1].Base + 0x3390) ||
      CepheusRpmhRead (C, C->CmdDbProtocol + 0x10, Word, 8) ||
      (ceph_rpmh_u64 (Word) != C->Image[2].Base + 0x1444)) { C->Prepared = FALSE; return; }
  typedef EFI_STATUS (EFIAPI *GET_ADDRESS)(VOID *, CONST CHAR8 *, UINT32 *);
  GET_ADDRESS GetAddress = (GET_ADDRESS)(UINTN)(C->Image[2].Base + 0x1444);
  if (EFI_ERROR (GetAddress ((VOID *)(UINTN)C->CmdDbProtocol, "cx.lvl", &C->Address[0])) ||
      EFI_ERROR (GetAddress ((VOID *)(UINTN)C->CmdDbProtocol, "mx.lvl", &C->Address[1])) ||
      ((C->Address[0] >> 16) != 3) || ((C->Address[1] >> 16) != 3) ||
      ((C->Address[0] | C->Address[1]) & 3) || (C->Address[0] == C->Address[1])) { C->Prepared = FALSE; }
}

STATIC VOID
CepheusRpmhCapture (CONST EFI_MEMORY_DESCRIPTOR *Map, UINTN Bytes, UINTN Stride)
{
  CE_RPMH_CONTEXT *C = &mCepheusRpmhContext;
  struct ceph_rpmh_snapshot *S = &mCepheusRpmhSnapshot;
  ZeroMem (S, sizeof (*S));
  S->magic = CE_RPMH_SNAPSHOT_MAGIC; S->version = 1; S->bytes = sizeof (*S);
  S->current_el = DarwinCurrentEl (); S->mpidr = READ_SYSREG (mpidr_el1);
  S->begin_ticks = READ_SYSREG (cntpct_el0); S->phase = 1;
  S->error = CE_RPMH_SNAP_BIND;
  C->Map = Map; C->MapBytes = Bytes; C->Stride = Stride;
  if (C->Prepared && (S->current_el == 4)) {
    /* No firmware calls while local interrupts are masked. This is not a
     * foreign-writer lease and subsequent EBS callbacks can change votes. */
    UINT64 SavedDaif;
    __asm__ volatile ("mrs %0, daif\n\tmsr daifset, #0xf\n\tdsb sy\n\tisb" : "=r"(SavedDaif) : : "memory");
    S->error = CE_RPMH_SNAP_MAP;
    if (!CepheusRpmhMapValid (C)) {
      for (UINTN I = 0; I < 4; I++) {
        if (CepheusRpmhImageValid (C, &C->Image[I])) { break; }
        S->identity_mask |= 1u << I;
      }
      S->error = CE_RPMH_SNAP_BIND;
      if (S->identity_mask == 15) {
        UINT8 Word[8];
        if (!CepheusRpmhRead (C, C->Image[0].Base + 0xe610, Word, 8) &&
            (ceph_rpmh_u64 (Word) == C->Image[1].Base + 0x1f0d0) &&
            !CepheusRpmhRead (C, C->Image[1].Base + 0x1f160, Word, 8) &&
            (ceph_rpmh_u64 (Word) == C->Image[1].Base + 0x3390) &&
            !CepheusRpmhRead (C, C->Image[1].Base + 0x1f168, Word, 8) &&
            (ceph_rpmh_u64 (Word) == C->Image[1].Base + 0x33c8)) {
          struct ceph_rpmh_snapshot_ops Ops = {C, CepheusRpmhRead, CepheusRpmhPool};
          S->error = ceph_rpmh_snapshot_read (S, &mCepheusRpmhScratch, &Ops, C->Image[0].Base + 0xe644, C->Address);
        }
      }
    }
    __asm__ volatile ("dsb sy\n\tmsr daif, %0\n\tisb" : : "r"(SavedDaif) : "memory");
  }
  if (S->error) { ZeroMem (S->resource, sizeof (S->resource)); if (S->resource_count > CE_RPMH_RESOURCE_LIMIT) { S->resource_count = 0; } }
  S->end_ticks = READ_SYSREG (cntpct_el0); S->complete = 1;
}

STATIC VOID
CepheusRpmhHandoff (UINT64 Destination, UINTN Attempt)
{
  struct ceph_rpmh_snapshot *S = &mCepheusRpmhSnapshot;
  S->ebs_attempts = (UINT32)Attempt + 1; S->ebs_done = 1;
  S->checksum = ceph_rpmh_snapshot_checksum (S);
  CopyMem ((VOID *)(UINTN)Destination, S, sizeof (*S));
}
#endif
