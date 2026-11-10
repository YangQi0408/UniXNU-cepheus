/* Included only in the opt-in staged Pool.c, after its real lock and lifetime table. */
#ifndef CEPHEUS_RPMH_TAIL_CORE_H
#define CEPHEUS_RPMH_TAIL_CORE_H
#include "cepheus_rpmh_pool.h"
#include "cepheus_rpmh_tail.h"
#include "CepheusRpmhTailProtocol.h"
#include "CepheusRpmhSnapshotIdentity.h"

typedef struct {
  UINT64 Base, Bytes;
  CONST UINT8 *Header;
  UINTN HeaderBytes, BaseOffset, TextRva, TextWords;
  CONST UINT64 *Text;
  CONST UINT32 *Fixups;
} CE_TAIL_IMAGE;
typedef struct {
  UINT64 Begin, End, Generation, Pool;
} CE_TAIL_POOL;
typedef struct {
  CE_TAIL_IMAGE Image[3];
  EFI_MEMORY_DESCRIPTOR Map[2048];
  UINTN MapCount;
  CE_TAIL_POOL Pool[1+2*CE_RPMH_DRV_LIMIT];
  UINTN PoolCount;
  UINT32 Address[2], Reads, Attempts;
  UINT64 Begin, Frequency, Serial, Last;
  BOOLEAN Bound, Active, Failed, Finished;
} CE_TAIL_CONTEXT;
STATIC CE_TAIL_CONTEXT mCeTail;
STATIC struct ceph_rpmh_tail mCeTailRecord;
STATIC struct ceph_rpmh_snapshot_scratch mCeTailScratch;

STATIC UINT64
CeTailRegister (UINTN Which)
{
  UINT64 Value;
  switch (Which) {
    case 0: __asm__ volatile ("mrs %0, CurrentEL" : "=r"(Value)); break;
    case 1: __asm__ volatile ("mrs %0, CNTPCT_EL0" : "=r"(Value)); break;
    case 2: __asm__ volatile ("mrs %0, CNTFRQ_EL0" : "=r"(Value)); break;
    case 3: __asm__ volatile ("mrs %0, MPIDR_EL1" : "=r"(Value)); break;
    default: __asm__ volatile ("mrs %0, SCTLR_EL1" : "=r"(Value)); break;
  }
  return Value;
}

STATIC UINT64
CeTailPar (UINT64 Address)
{
  UINT64 Value;
  __asm__ volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r"(Value) : "r"(Address) : "memory");
  return Value;
}

STATIC UINT64
CeTailMask (VOID)
{
  UINT64 Saved;
  __asm__ volatile ("mrs %0, daif\n\tmsr daifset, #0xf\n\tdsb sy\n\tisb" : "=r"(Saved) : : "memory");
  return Saved;
}

STATIC VOID
CeTailRestore (UINT64 Saved)
{
  __asm__ volatile ("dsb sy\n\tmsr daif, %0\n\tisb" : : "r"(Saved) : "memory");
}

STATIC int
CeTailBudget (VOID)
{
  UINT64 Now = CeTailRegister (1);
  if (mCeTail.Failed || !mCeTail.Active || (mCeTail.Reads >= 65536) ||
      (CeTailRegister (0) != 4) || (CeTailRegister (2) != mCeTail.Frequency) ||
      (Now < mCeTail.Begin) || (Now < mCeTail.Last) || (Now - mCeTail.Begin > mCeTail.Frequency / 2) ||
      mCepheusPoolLifetime.Failed || (mCepheusPoolLifetime.Serial != mCeTail.Serial)) {
    mCeTail.Failed = TRUE; return -1;
  }
  mCeTail.Last = Now;
  mCeTail.Reads++;
  return 0;
}

/* Current CPU stage-1 readable Normal WB identity translation, not EL2/XPU proof.
 * Scope is SM8150's <=48-bit PAR format. AT itself is not abort containment. */
int
CoreCepheusTailProbe (UINT64 Address, UINT64 Bytes)
{
  if (!Bytes || (Address > MAX_UINT64 - Bytes) || CeTailBudget ()) { return -1; }
  for (UINT64 Page = Address & ~UINT64_C(4095); Page < Address + Bytes;) {
    UINT64 Par = CeTailPar (Page);
    if ((Par & 1) || ((Par >> 56) != 0xff) || (Par & UINT64_C(0x00ff000000000000)) ||
        ((Par & UINT64_C(0x0000fffffffff000)) != Page)) { mCeTail.Failed = TRUE; return -1; }
    if (Page > MAX_UINT64 - 4096) { mCeTail.Failed = TRUE; return -1; }
    Page += 4096;
  }
  return 0;
}

extern int CoreCepheusTailMap (EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN *);

STATIC int
CeTailRange (UINT64 Address, UINT64 Bytes, BOOLEAN DataOnly)
{
  if (!Bytes || Address > MAX_UINT64 - Bytes) { return -1; }
  UINT64 End = Address + Bytes;
  for (UINT64 Cursor = Address; Cursor < End;) {
    UINT64 Next = Cursor;
    for (UINTN I = 0; I < mCeTail.MapCount; I++) {
      CONST EFI_MEMORY_DESCRIPTOR *D = &mCeTail.Map[I];
      UINT64 Limit = D->PhysicalStart + D->NumberOfPages * 4096;
      if ((D->PhysicalStart <= Cursor) && (Cursor < Limit)) {
        if (((D->Type != EfiBootServicesData) && (DataOnly || D->Type != EfiBootServicesCode)) ||
            ((D->Attribute & (EFI_MEMORY_WB | EFI_MEMORY_RP)) != EFI_MEMORY_WB)) { return -1; }
        Next = Limit < End ? Limit : End; break;
      }
    }
    if (Next == Cursor) { return -1; }
    Cursor = Next;
  }
  return CoreCepheusTailProbe (Address, Bytes);
}

STATIC int
CeTailRead (void *Context, uint64_t Address, void *Output, uint32_t Bytes)
{
  (VOID)Context;
  if (mCeTail.Failed || !Output || !Bytes || Address > MAX_UINT64 - Bytes) { return -1; }
  BOOLEAN Admitted = FALSE;
  for (UINTN I = 0; I < 3; I++) {
    CONST CE_TAIL_IMAGE *Image = &mCeTail.Image[I];
    if (Address >= Image->Base && Address + Bytes <= Image->Base + Image->Bytes) { Admitted = TRUE; }
  }
  for (UINTN I = 0; I < mCeTail.PoolCount; I++) {
    CONST CE_TAIL_POOL *P = &mCeTail.Pool[I];
    if (Address >= P->Begin && Address + Bytes <= P->End) {
      CE_POOL_ALLOCATION A;
      if (CePoolLookup (&mCepheusPoolLifetime, P->Pool, 1, 4, P->Generation, &A)) {
        mCeTail.Failed = TRUE; return -1;
      }
      Admitted = TRUE;
    }
  }
  if (!Admitted || CeTailRange (Address, Bytes, FALSE)) { mCeTail.Failed = TRUE; return -1; }
  volatile CONST UINT8 *Source = (CONST VOID *)(UINTN)Address;
  UINT8 *Target = Output;
  for (UINT32 I = 0; I < Bytes; I++) { Target[I] = Source[I]; }
  if (CoreCepheusTailProbe (Address, Bytes)) { return -1; }
  return 0;
}

STATIC int
CeTailPool (void *Context, uint64_t Address, uint32_t Bytes)
{
  (VOID)Context;
  CE_POOL_ALLOCATION A;
  if (mCeTail.Failed || Address < 28 || !Bytes ||
      CePoolLookup (&mCepheusPoolLifetime, Address - 4, (UINT64)Bytes + 4, 4, 0, &A) ||
      (A.Bytes != (UINT64)Bytes + 4)) { mCeTail.Failed = TRUE; return -1; }
  UINT64 Total = (((UINT64)Bytes + 11) & ~UINT64_C(7)) + 40;
  UINT64 Begin = Address - 28;
  if (Begin > MAX_UINT64 - Total || CeTailRange (Begin, Total, TRUE)) { mCeTail.Failed = TRUE; return -1; }
  for (UINTN I = 0; I < 3; I++) {
    CONST CE_TAIL_IMAGE *Image = &mCeTail.Image[I];
    if (Begin < Image->Base + Image->Bytes && Image->Base < Begin + Total) { mCeTail.Failed = TRUE; return -1; }
  }
  UINTN Found = mCeTail.PoolCount;
  for (UINTN I = 0; I < mCeTail.PoolCount; I++) {
    CE_TAIL_POOL *P = &mCeTail.Pool[I];
    if (P->Pool == A.Address) {
      if (P->Generation != A.Generation || P->Begin != Begin || P->End != Begin + Total) {
        mCeTail.Failed = TRUE; return -1;
      }
      Found = I; break;
    }
    if (Begin < P->End && P->Begin < Begin + Total) { mCeTail.Failed = TRUE; return -1; }
  }
  if (Found == mCeTail.PoolCount) {
    if (Found == ARRAY_SIZE (mCeTail.Pool)) { mCeTail.Failed = TRUE; return -1; }
    mCeTail.Pool[Found] = (CE_TAIL_POOL){Begin, Begin + Total, A.Generation, A.Address};
    mCeTail.PoolCount++;
  }
  struct ceph_rpmh_snapshot_ops Ops = {NULL, CeTailRead, NULL};
  if (ceph_rpmh_pool_check (&Ops, Address, Bytes)) { mCeTail.Failed = TRUE; return -1; }
  return 0;
}

STATIC int
CeTailImageValid (CE_TAIL_IMAGE *Image)
{
  UINT8 Block[256], Word[8];
  if (!Image->Base || (Image->Base & 4095) || Image->Base > MAX_UINT64 - Image->Bytes ||
      CeTailRange (Image->Base, Image->Bytes, FALSE)) { return -1; }
  for (UINTN Begin = 0; Begin < Image->HeaderBytes;) {
    UINTN Bytes = Image->HeaderBytes - Begin;
    if (Bytes > sizeof (Block)) { Bytes = sizeof (Block); }
    if (CeTailRead (NULL, Image->Base + Begin, Block, (UINT32)Bytes)) { return -1; }
    for (UINTN I = 0; I < Bytes; I++) {
      UINTN Offset = Begin + I;
      if (Offset >= Image->BaseOffset && Offset < Image->BaseOffset + 8) { continue; }
      if (Block[I] != Image->Header[Offset]) { return -1; }
    }
    Begin += Bytes;
  }
  if (CeTailRead (NULL, Image->Base + Image->BaseOffset, Word, 8) || ceph_rpmh_u64 (Word) != Image->Base) { return -1; }
  UINTN Fixup = 0;
  for (UINTN Begin = 0; Begin < Image->TextWords;) {
    UINTN Words = Image->TextWords - Begin;
    if (Words > sizeof (Block)/8) { Words = sizeof (Block)/8; }
    if (CeTailRead (NULL, Image->Base + Image->TextRva + Begin * 8, Block, (UINT32)(Words * 8))) { return -1; }
    for (UINTN I = 0; I < Words; I++) {
      UINT64 Expected = Image->Text[Begin + I];
      if (Image->Fixups[Fixup] == Begin + I) { Expected += Image->Base; Fixup++; }
      if (ceph_rpmh_u64 (Block + I * 8) != Expected) { return -1; }
    }
    Begin += Words;
  }
  return 0;
}

STATIC int
CeTailPointers (VOID)
{
  UINT8 Word[8];
  return CeTailRead (NULL, mCeTail.Image[0].Base + 0xe610, Word, 8) ||
      ceph_rpmh_u64 (Word) != mCeTail.Image[1].Base + 0x1f0d0 ||
      CeTailRead (NULL, mCeTail.Image[1].Base + 0x1f160, Word, 8) ||
      ceph_rpmh_u64 (Word) != mCeTail.Image[1].Base + 0x3390 ||
      CeTailRead (NULL, mCeTail.Image[1].Base + 0x1f168, Word, 8) ||
      ceph_rpmh_u64 (Word) != mCeTail.Image[1].Base + 0x33c8;
}

STATIC EFI_STATUS EFIAPI
CeTailBind (CONST UINT64 Bases[3])
{
  if (gMemoryMapTerminated || gEfiCurrentTpl != TPL_APPLICATION || !Bases ||
      mCeTail.Bound || mCeTail.Finished || mPoolMemoryLock.Lock == EfiLockAcquired ||
      gMemoryLock.Lock == EfiLockAcquired || CeTailRegister (0) != 4 ||
      (CeTailRegister (4) & 5) != 5) { return EFI_ACCESS_DENIED; }
  UINT64 Copied[3]; UINT32 Targets[2] = {0};
  CopyMem (Copied, Bases, sizeof (Copied));
  ZeroMem (&mCeTail, sizeof (mCeTail));
  mCeTail.Image[0] = (CE_TAIL_IMAGE){Copied[0], CE_RPMH_IMAGE_BYTES, mCeRpmhHeader, sizeof (mCeRpmhHeader), CE_RPMH_BASE_OFFSET, CE_RPMH_TEXT_RVA, sizeof (mCeRpmhText)/8, mCeRpmhText, mCeRpmhFixups};
  mCeTail.Image[1] = (CE_TAIL_IMAGE){Copied[1], CE_DAL_IMAGE_BYTES, mCeDalHeader, sizeof (mCeDalHeader), CE_DAL_BASE_OFFSET, CE_DAL_TEXT_RVA, sizeof (mCeDalText)/8, mCeDalText, mCeDalFixups};
  mCeTail.Image[2] = (CE_TAIL_IMAGE){Copied[2], CE_CMDDB_IMAGE_BYTES, mCeCmdDbHeader, sizeof (mCeCmdDbHeader), CE_CMDDB_BASE_OFFSET, CE_CMDDB_TEXT_RVA, sizeof (mCeCmdDbText)/8, mCeCmdDbText, mCeCmdDbFixups};
  for (UINTN I = 0; I < 3; I++) {
    if (!Copied[I] || (Copied[I] & 4095) || Copied[I] > MAX_UINT64 - mCeTail.Image[I].Bytes) { return EFI_INVALID_PARAMETER; }
    for (UINTN J = 0; J < I; J++) {
      if (Copied[I] < Copied[J] + mCeTail.Image[J].Bytes && Copied[J] < Copied[I] + mCeTail.Image[I].Bytes) { return EFI_INVALID_PARAMETER; }
    }
  }
  UINT64 Saved = CeTailMask ();
  mCeTail.Serial = mCepheusPoolLifetime.Serial;
  mCeTail.Frequency = CeTailRegister (2); mCeTail.Begin = CeTailRegister (1);
  mCeTail.Last = mCeTail.Begin;
  mCeTail.Active = TRUE;
  int Error = mCeTail.Frequency == 0 || mCeTail.Frequency > 1000000000 ||
      CoreCepheusTailMap (mCeTail.Map, ARRAY_SIZE (mCeTail.Map), &mCeTail.MapCount);
  for (UINTN I = 0; !Error && I < 3; I++) { Error = CeTailImageValid (&mCeTail.Image[I]); }
  if (!Error) { Error = CeTailPointers (); }
  UINT8 Function[8];
  if (!Error) {
    Error = CeTailRead (NULL, Copied[2] + 0x6088, Function, 8) ||
        ceph_rpmh_u64 (Function) != Copied[2] + 0x1444;
  }
  mCeTail.Active = FALSE; CeTailRestore (Saved);
  if (Error || mCeTail.Failed) { return EFI_COMPROMISED_DATA; }
  /* These are the same identity-bound, read-only CmdDb queries as the older
   * adapter, before EBS and with DAIF restored. Never call firmware at tail. */
  typedef EFI_STATUS (EFIAPI *GET_ADDRESS)(VOID *, CONST CHAR8 *, UINT32 *);
  GET_ADDRESS GetAddress = (GET_ADDRESS)(UINTN)(Copied[2] + 0x1444);
  if (EFI_ERROR (GetAddress ((VOID *)(UINTN)(Copied[2] + 0x6078), "cx.lvl", &Targets[0])) ||
      EFI_ERROR (GetAddress ((VOID *)(UINTN)(Copied[2] + 0x6078), "mx.lvl", &Targets[1])) ||
      (Targets[0] >> 16) != 3 || (Targets[1] >> 16) != 3 ||
      ((Targets[0] | Targets[1]) & 3) || Targets[0] == Targets[1]) { return EFI_COMPROMISED_DATA; }
  mCeTail.Address[0] = Targets[0]; mCeTail.Address[1] = Targets[1];
  mCeTail.Bound = TRUE; return EFI_SUCCESS;
}

VOID
CoreCepheusTailAttempt (VOID)
{
  if (mCeTail.Bound && !mCeTail.Finished && mCeTail.Attempts < 4) { mCeTail.Attempts++; }
}

BOOLEAN
CoreCepheusTailFrozen (VOID)
{
  return mCeTail.Active;
}

BOOLEAN
CoreCepheusTailRejectMutation (VOID)
{
  if (!mCeTail.Active) { return FALSE; }
  mCeTail.Failed = TRUE; return TRUE;
}

VOID
CoreCepheusRpmhTail (VOID)
{
  if (!mCeTail.Bound || mCeTail.Finished) { return; }
  mCeTail.Finished = TRUE;
  struct ceph_rpmh_tail *T = &mCeTailRecord;
  ZeroMem (T, sizeof (*T));
  T->magic = CE_RPMH_TAIL_MAGIC; T->version = 1; T->bytes = sizeof (*T);
  T->error = CE_RPMH_SNAP_BIND;
  struct ceph_rpmh_snapshot *S = &T->requests;
  S->magic = CE_RPMH_SNAPSHOT_MAGIC; S->version = 1; S->bytes = sizeof (*S);
  S->phase = 2; S->ebs_done = 1; S->ebs_attempts = mCeTail.Attempts;
  S->current_el = CeTailRegister (0); S->mpidr = CeTailRegister (3);
  UINT64 Saved = CeTailMask ();
  mCeTail.Failed = FALSE; mCeTail.Reads = 0; mCeTail.PoolCount = 0;
  mCeTail.Serial = mCepheusPoolLifetime.Serial;
  mCeTail.Frequency = CeTailRegister (2); mCeTail.Begin = CeTailRegister (1);
  mCeTail.Last = mCeTail.Begin;
  T->begin_ticks = S->begin_ticks = mCeTail.Begin;
  T->allocation_serial = mCeTail.Serial;
  if (gMemoryMapTerminated && gRuntime->AtRuntime && gBS == NULL &&
      gEfiCurrentTpl == TPL_APPLICATION && mPoolMemoryLock.Lock != EfiLockAcquired &&
      gMemoryLock.Lock != EfiLockAcquired && S->current_el == 4 &&
      (CeTailRegister (4) & 5) == 5 && mCeTail.Attempts && mCeTail.Attempts <= 3 &&
      mCeTail.Frequency && mCeTail.Frequency <= 1000000000) {
    mCeTail.Active = TRUE; T->boundaries = 1;
    T->error = CE_RPMH_SNAP_MAP;
    if (!CoreCepheusTailMap (mCeTail.Map, ARRAY_SIZE (mCeTail.Map), &mCeTail.MapCount)) {
      T->boundaries |= 6; T->error = CE_RPMH_SNAP_BIND;
      for (UINTN I = 0; I < 3; I++) {
        if (CeTailImageValid (&mCeTail.Image[I])) { break; }
        S->identity_mask |= 1u << I;
      }
      if (S->identity_mask == 7 && !CeTailPointers ()) {
        T->boundaries |= 8;
        struct ceph_rpmh_snapshot_ops Ops = {NULL, CeTailRead, CeTailPool};
        T->error = ceph_rpmh_snapshot_read (S, &mCeTailScratch, &Ops, mCeTail.Image[0].Base + 0xe644, mCeTail.Address);
      }
    }
    if (mCeTail.Failed || mCepheusPoolLifetime.Serial != mCeTail.Serial) {
      if (!T->error) { T->error = CE_RPMH_SNAP_CHANGED; }
    }
  }
  UINT64 End = CeTailRegister (1);
  if (End < mCeTail.Begin || End < mCeTail.Last ||
      End - mCeTail.Begin > mCeTail.Frequency / 2 || CeTailRegister (2) != mCeTail.Frequency ||
      CeTailRegister (0) != S->current_el || mCepheusPoolLifetime.Failed ||
      mCepheusPoolLifetime.Serial != mCeTail.Serial || mCeTail.Failed) {
    if (!T->error) { T->error = CE_RPMH_SNAP_CHANGED; }
  }
  if (T->error) { ZeroMem (S->resource, sizeof (S->resource)); }
  if (S->resource_count > CE_RPMH_RESOURCE_LIMIT) { S->resource_count = 0; }
  S->error = T->error; S->complete = 1;
  T->reads = mCeTail.Reads; T->end_ticks = S->end_ticks = End;
  S->checksum = ceph_rpmh_snapshot_checksum (S);
  T->complete = 1; T->checksum = ceph_rpmh_tail_checksum (T);
  mCeTail.Active = FALSE; CeTailRestore (Saved);
}

STATIC EFI_STATUS EFIAPI
CeTailCopy (struct ceph_rpmh_tail *Output)
{
  if (!Output || !mCeTail.Finished || !mCeTailRecord.complete) { return EFI_NOT_READY; }
  CopyMem (Output, &mCeTailRecord, sizeof (*Output)); return EFI_SUCCESS;
}

STATIC CE_TAIL_PROTOCOL mCeTailProtocol = {1, sizeof (CE_TAIL_PROTOCOL), CeTailBind, CeTailCopy};
STATIC EFI_GUID mCeTailGuid = CE_RPMH_TAIL_PROTOCOL_GUID;
VOID
CoreCepheusTailInstall (VOID)
{
  EFI_HANDLE Handle = NULL;
  CoreInstallProtocolInterface (&Handle, &mCeTailGuid, EFI_NATIVE_INTERFACE, &mCeTailProtocol);
}
#endif
