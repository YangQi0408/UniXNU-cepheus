/* DarwinBoot-owned copy; no firmware services or borrowed pointers after EBS. */
#ifndef CEPHEUS_SMEM_MAP_HANDOFF_FIRMWARE_H
#define CEPHEUS_SMEM_MAP_HANDOFF_FIRMWARE_H
#include "CepheusSmemMapProbe.h"
#include "cepheus_smem_map_handoff.h"
#include "CepheusSmemMapIdentity.h"

STATIC BOOLEAN mCepheusSmemAttempted;
STATIC struct ceph_smem_map_handoff mCepheusSmem;
STATIC CE_SMEM_MAP_EVIDENCE mCepheusSmemEvidence;

STATIC CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *
CepheusSmemProfile (VOID)
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

STATIC __attribute__((noinline)) EFI_STATUS
CepheusSmemCapture (VOID)
{
  if (mCepheusSmemAttempted) { return EFI_ALREADY_STARTED; }
  mCepheusSmemAttempted = TRUE;
  struct ceph_smem_map_handoff *H = &mCepheusSmem;
  CE_SMEM_MAP_EVIDENCE *E = &mCepheusSmemEvidence;
  ZeroMem (H, sizeof (*H)); ZeroMem (E, sizeof (*E));
  H->magic = CE_SMEM_MAP_MAGIC; H->version = 1; H->bytes = sizeof (*H);
  H->base = CE_SMEM_MAP_BASE; H->span = CE_SMEM_MAP_SPAN;
  CopyMem (H->probe_source_sha256, mCeSmemProbeIdentity, 32);
  CopyMem (H->profile_source_sha256, mCeSmemProfileIdentity, 32);
  CopyMem (H->profile_header_sha256, mCeSmemHeaderIdentity, 32);
  H->begin_ticks = READ_SYSREG (cntpct_el0);
  H->probe_status = E->Status = EFI_NOT_STARTED;
  H->prepare_status = CepheusSmemMapPrepare (CepheusSmemProfile ());
  if (!EFI_ERROR (H->prepare_status)) {
    H->probe_status = CepheusSmemMapProbe (E);
    H->close_status = CepheusSmemMapClose ();
  }
  H->end_ticks = READ_SYSREG (cntpct_el0);
  H->map_status = E->Status;
  _Static_assert (sizeof (H->before) == sizeof (E->Before), "CPU copy ABI");
  _Static_assert (sizeof (H->resource) == sizeof (E->Resource), "HOB copy ABI");
  CopyMem (&H->before, &E->Before, sizeof (H->before));
  CopyMem (&H->after, &E->After, sizeof (H->after));
  CopyMem (&H->resource, &E->Resource, sizeof (H->resource));
  H->gcd.base = E->Gcd.BaseAddress; H->gcd.bytes = E->Gcd.Length;
  H->gcd.capabilities = E->Gcd.Capabilities; H->gcd.attributes = E->Gcd.Attributes;
  H->gcd.image = (UINTN)E->Gcd.ImageHandle; H->gcd.device = (UINTN)E->Gcd.DeviceHandle;
  H->gcd.type = E->Gcd.GcdMemoryType;
  H->hobs = E->Hobs; H->pages = E->Pages; H->resource_matches = E->ResourceMatches;
  H->resource_complete = E->ResourceComplete; H->gcd_complete = E->GcdComplete;
  H->cpu_stable = E->CpuStable;
  CopyMem (H->par, E->Par, sizeof (H->par));
  /* Diagnostic failures may be handed off, but cleanup failures may not boot.
   * Unload retries Close without dropping a still-referenced event callback. */
  return H->close_status;
}

STATIC __attribute__((noinline)) BOOLEAN
CepheusSmemHandoff (UINT64 TreeAddress, UINTN TreeBytes, UINTN Offset, UINTN Attempt)
{
  if (!mCepheusSmemAttempted || mCepheusSmem.complete || Attempt >= 3) { return FALSE; }
  struct ceph_smem_map_handoff *H = &mCepheusSmem;
  H->tree_address = TreeAddress; H->tree_bytes = TreeBytes; H->property_offset = Offset;
  H->ebs_attempts = (UINT32)Attempt + 1; H->ebs_done = H->complete = 1;
  H->checksum = ceph_smem_map_checksum (H);
  if (ceph_smem_map_valid (H)) { return FALSE; }
  CopyMem ((VOID *)(UINTN)(TreeAddress + Offset), H, sizeof (*H));
  return TRUE;
}
#endif
