/* Core-owned current map capture. Runs with local interrupts masked, no services. */
#ifndef CEPHEUS_RPMH_TAIL_MAP_H
#define CEPHEUS_RPMH_TAIL_MAP_H
extern int CoreCepheusTailProbe (UINT64, UINT64);
extern BOOLEAN CoreCepheusTailRejectMutation (VOID);
extern MEMORY_MAP mMapStack[];

/* Only pages returned by the actual descriptor allocator, not arbitrary RAM. */
typedef struct { UINT64 Start, End; BOOLEAN Live; } CE_TAIL_MAP_SPAN;
STATIC CE_TAIL_MAP_SPAN mCeTailMapSpan[128];
STATIC BOOLEAN mCeTailMapFailed;

STATIC VOID
CeTailMapAllocated (UINT64 Start, UINT64 Bytes)
{
  if (mCeTailMapFailed) { return; }
  if (!Start || (Start & 4095) || !Bytes || (Bytes & 4095) || Start > MAX_UINT64 - Bytes) {
    mCeTailMapFailed = TRUE; return;
  }
  UINTN Available = ARRAY_SIZE (mCeTailMapSpan);
  for (UINTN I = 0; I < ARRAY_SIZE (mCeTailMapSpan); I++) {
    CE_TAIL_MAP_SPAN *P = &mCeTailMapSpan[I];
    if (P->Live && Start < P->End && P->Start < Start + Bytes) { mCeTailMapFailed = TRUE; return; }
    if (!P->Live && Available == ARRAY_SIZE (mCeTailMapSpan)) { Available = I; }
  }
  if (Available == ARRAY_SIZE (mCeTailMapSpan)) { mCeTailMapFailed = TRUE; return; }
  mCeTailMapSpan[Available] = (CE_TAIL_MAP_SPAN){Start, Start + Bytes, TRUE};
}

STATIC VOID
CeTailMapRangeChanged (UINT64 Start, UINT64 Pages)
{
  if (!Pages || (Start & 4095) || Pages > (MAX_UINT64 - Start)/4096) {
    mCeTailMapFailed = TRUE; return;
  }
  UINT64 End = Start + Pages * 4096;
  for (UINTN I = 0; I < ARRAY_SIZE (mCeTailMapSpan); I++) {
    CE_TAIL_MAP_SPAN *P = &mCeTailMapSpan[I];
    if (P->Live && Start < P->End && P->Start < End) { P->Live = FALSE; }
  }
}

STATIC int
CeTailMapNode (UINT64 Address)
{
  if (mCeTailMapFailed) { return -1; }
  UINT64 Stack = (UINTN)mMapStack;
  if (Address >= Stack && Address - Stack < MAX_MAP_DEPTH * sizeof (MEMORY_MAP) &&
      (Address - Stack) % sizeof (MEMORY_MAP) == 0) { return 0; }
  for (UINTN I = 0; I < ARRAY_SIZE (mCeTailMapSpan); I++) {
    CONST CE_TAIL_MAP_SPAN *P = &mCeTailMapSpan[I];
    if (P->Live && Address >= P->Start && Address < P->End &&
        (Address - P->Start) % sizeof (MEMORY_MAP) == 0 && P->End - Address >= sizeof (MEMORY_MAP)) { return 1; }
  }
  return -1;
}

int
CoreCepheusTailMap (EFI_MEMORY_DESCRIPTOR *Output, UINTN Capacity, UINTN *Count)
{
  UINTN N = 0;
  if (!Output || !Count || Capacity > 2048 || !Capacity || mCeTailMapFailed ||
      CoreCepheusTailProbe ((UINTN)&gMemoryMap, sizeof (gMemoryMap))) { return -1; }
  *Count = 0;
  LIST_ENTRY Head;
  CopyMem (&Head, &gMemoryMap, sizeof (Head));
  if (CoreCepheusTailProbe ((UINTN)&gMemoryMap, sizeof (gMemoryMap))) { return -1; }
  LIST_ENTRY *Previous = &gMemoryMap;
  LIST_ENTRY *Next = Head.ForwardLink;
  while (Next != &gMemoryMap) {
    if (N == Capacity || (UINTN)Next < OFFSET_OF (MEMORY_MAP, Link)) { return -1; }
    UINTN Address = (UINTN)Next - OFFSET_OF (MEMORY_MAP, Link);
    int FromPages = CeTailMapNode (Address);
    if (FromPages < 0) { return -1; }
    if (CoreCepheusTailProbe (Address, sizeof (MEMORY_MAP))) { return -1; }
    MEMORY_MAP Raw;
    CopyMem (&Raw, (CONST VOID *)Address, sizeof (Raw));
    if (CoreCepheusTailProbe (Address, sizeof (MEMORY_MAP))) { return -1; }
    if (Raw.Signature != MEMORY_MAP_SIGNATURE || Raw.FromPages != FromPages || Raw.Link.BackLink != Previous ||
        Raw.Start & 4095 || Raw.End < Raw.Start || Raw.End == MAX_UINT64 ||
        ((Raw.End + 1) & 4095) ||
        (Raw.VirtualStart && Raw.VirtualStart != Raw.Start)) { return -1; }
    for (UINTN I = 0; I < N; I++) {
      CONST EFI_MEMORY_DESCRIPTOR *D = &Output[I];
      if (Raw.Start < D->PhysicalStart + D->NumberOfPages * 4096 && D->PhysicalStart <= Raw.End) { return -1; }
    }
    Output[N++] = (EFI_MEMORY_DESCRIPTOR){.Type = Raw.Type, .PhysicalStart = Raw.Start,
      .VirtualStart = Raw.VirtualStart, .NumberOfPages = (Raw.End - Raw.Start + 1)/4096,
      .Attribute = Raw.Attribute};
    Previous = Next; Next = Raw.Link.ForwardLink;
  }
  if (CoreCepheusTailProbe ((UINTN)&gMemoryMap, sizeof (gMemoryMap))) { return -1; }
  LIST_ENTRY Final;
  CopyMem (&Final, &gMemoryMap, sizeof (Final));
  if (CoreCepheusTailProbe ((UINTN)&gMemoryMap, sizeof (gMemoryMap)) ||
      Final.BackLink != Previous || Final.ForwardLink != Head.ForwardLink ||
      Final.BackLink != Head.BackLink || !N) { return -1; }
  *Count = N; return 0;
}
#endif
