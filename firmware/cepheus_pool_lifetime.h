/* Core-owned bookkeeping only. No object dereference, pinning, or writer lease. */
#ifndef CEPHEUS_POOL_LIFETIME_H
#define CEPHEUS_POOL_LIFETIME_H

#define CE_POOL_SLOTS 16384u
#define CE_POOL_PROBES 256u

typedef struct {
  UINT64 Address;
  UINT64 Bytes;
  UINT64 Generation;
  UINT32 Type;
  UINT32 State;
} CE_POOL_ALLOCATION;

typedef struct {
  CE_POOL_ALLOCATION Slot[CE_POOL_SLOTS];
  UINT64 Serial;
  BOOLEAN Failed;
} CE_POOL_LIFETIME;

STATIC UINTN
CePoolHash (UINT64 Address)
{
  UINT64 Key = Address >> 3;
  Key ^= Key >> 17;
  Key *= 0x9e3779b97f4a7c15ULL;
  return (UINTN)(Key >> 32) & (CE_POOL_SLOTS - 1);
}

STATIC VOID
CePoolAllocated (CE_POOL_LIFETIME *L, UINT64 Address, UINT64 Bytes, UINT32 Type)
{
  UINTN Available = CE_POOL_SLOTS;
  if (L->Failed) { return; }
  if ((Address == 0) || (Address & 7) || (Bytes > MAX_UINT64 - Address) ||
      (L->Serial == MAX_UINT64)) { L->Failed = TRUE; return; }
  UINTN Start = CePoolHash (Address);
  for (UINTN I = 0; I < CE_POOL_PROBES; I++) {
    UINTN Index = (Start + I) & (CE_POOL_SLOTS - 1);
    CE_POOL_ALLOCATION *A = &L->Slot[Index];
    if ((A->State == 1) && (A->Address == Address)) { L->Failed = TRUE; return; }
    if ((A->State != 1) && (Available == CE_POOL_SLOTS)) { Available = Index; }
    if (A->State == 0) { break; }
  }
  if (Available == CE_POOL_SLOTS) { L->Failed = TRUE; return; }
  L->Serial++;
  L->Slot[Available] = (CE_POOL_ALLOCATION){Address, Bytes, L->Serial, Type, 1};
}

STATIC VOID
CePoolFreed (CE_POOL_LIFETIME *L, UINT64 Address)
{
  if (L->Failed) { return; }
  UINTN Start = CePoolHash (Address);
  for (UINTN I = 0; I < CE_POOL_PROBES; I++) {
    CE_POOL_ALLOCATION *A = &L->Slot[(Start + I) & (CE_POOL_SLOTS - 1)];
    if (A->State == 0) { return; }
    if ((A->State == 1) && (A->Address == Address)) {
      /* Invalidate before the real allocator reads, clears, or guards the block. */
      A->State = 2;
      return;
    }
  }
}

STATIC int
CePoolLookup (CONST CE_POOL_LIFETIME *L, UINT64 Address, UINT64 Bytes,
              UINT32 Type, UINT64 Generation, CE_POOL_ALLOCATION *Output)
{
  if (Output == NULL) { return -1; }
  *Output = (CE_POOL_ALLOCATION){0};
  if (L->Failed || (Address == 0) || (Address & 7) || (Bytes == 0) ||
      (Bytes > MAX_UINT64 - Address)) { return -1; }
  UINTN Start = CePoolHash (Address);
  for (UINTN I = 0; I < CE_POOL_PROBES; I++) {
    CONST CE_POOL_ALLOCATION *A = &L->Slot[(Start + I) & (CE_POOL_SLOTS - 1)];
    if (A->State == 0) { return -1; }
    if ((A->State == 1) && (A->Address == Address)) {
      if ((A->Type != Type) || (Bytes > A->Bytes) ||
          ((Generation != 0) && (Generation != A->Generation))) { return -1; }
      *Output = *A;
      return 0;
    }
  }
  return -1;
}

STATIC VOID
CePoolRangeChanged (CE_POOL_LIFETIME *L, UINT64 Start, UINT64 Pages)
{
  if (L->Failed) { return; }
  if ((Pages == 0) || (Start & 4095) || (Pages > (MAX_UINT64 - Start) / 4096)) {
    L->Failed = TRUE;
    return;
  }
  UINT64 End = Start + Pages * 4096;
  for (UINTN I = 0; I < CE_POOL_SLOTS; I++) {
    CE_POOL_ALLOCATION *A = &L->Slot[I];
    if (A->State != 1) { continue; }
    /* Include allocator head/alignment/tail, conservatively even for tail guards. */
    if ((A->Address < 24) || (A->Address > MAX_UINT64 - 23) ||
        (A->Bytes > MAX_UINT64 - A->Address - 23)) {
      L->Failed = TRUE;
      return;
    }
    UINT64 Begin = A->Address - 24;
    UINT64 Limit = A->Address + A->Bytes + 23;
    if ((Begin < End) && (Start < Limit)) { A->State = 2; }
  }
}
#endif
