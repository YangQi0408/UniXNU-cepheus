/* Loaded-image discovery before EBS; only core-owned diagnostic copy after. */
#ifndef CEPHEUS_RPMH_TAIL_HANDOFF_FIRMWARE_H
#define CEPHEUS_RPMH_TAIL_HANDOFF_FIRMWARE_H
#include "CepheusRpmhTailProtocol.h"
#include "cepheus_rpmh_tail_handoff.h"

STATIC BOOLEAN mCepheusRpmhTailAttempted;
STATIC CE_TAIL_PROTOCOL mCepheusRpmhTailProtocol;
STATIC struct ceph_rpmh_tail_handoff mCepheusRpmhTailHandoff;
STATIC EFI_GUID mCeTailHandoffGuid = CE_RPMH_TAIL_PROTOCOL_GUID;
STATIC EFI_GUID mCeTailRpmhGuid = {0xe1d0b573,0xf29f,0x495d,{0x93,0x52,0xca,0x0d,0xd4,0x2a,0x0a,0x74}};
STATIC EFI_GUID mCeTailCmdDbGuid = {0x1c34f691,0xd33d,0x4e14,{0xb8,0xf8,0x32,0xc6,0xc0,0x29,0xe9,0x5b}};

STATIC VOID
CepheusRpmhTailPrepare (VOID)
{
  if (mCepheusRpmhTailAttempted) { return; }
  mCepheusRpmhTailAttempted = TRUE;
  struct ceph_rpmh_tail_handoff *H = &mCepheusRpmhTailHandoff;
  ZeroMem (H, sizeof (*H));
  H->magic = CE_RPMH_TAIL_HANDOFF_MAGIC; H->version = 1; H->bytes = sizeof (*H);
  H->flags = CE_TAIL_PREPARED; H->bind_status = H->copy_status = EFI_NOT_READY;
  CE_TAIL_PROTOCOL *Protocol = NULL;
  EFI_STATUS Status = gBS->LocateProtocol (&mCeTailHandoffGuid, NULL, (VOID **)&Protocol);
  H->bind_status = Status;
  if (EFI_ERROR (Status)) { return; }
  if (!Protocol || Protocol->Version != 1 || Protocol->Bytes != sizeof (*Protocol) ||
      !Protocol->Bind || !Protocol->Copy) { H->bind_status = EFI_COMPROMISED_DATA; return; }
  /* Copy both callbacks while boot services are live. The post-EBS method
   * belongs to the packaged core; a protocol GUID is not authentication. */
  CopyMem (&mCepheusRpmhTailProtocol, Protocol, sizeof (*Protocol));
  VOID *Rpmh = NULL, *CmdDb = NULL;
  Status = gBS->LocateProtocol (&mCeTailRpmhGuid, NULL, &Rpmh);
  if (!EFI_ERROR (Status)) { Status = gBS->LocateProtocol (&mCeTailCmdDbGuid, NULL, &CmdDb); }
  H->bind_status = Status;
  if (EFI_ERROR (Status)) { return; }
  if (!Rpmh || !CmdDb) { H->bind_status = EFI_COMPROMISED_DATA; return; }
  EFI_HANDLE *Handles = NULL; UINTN Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &Count, &Handles);
  H->bind_status = Status;
  if (EFI_ERROR (Status)) { return; }
  UINT64 Bases[3] = {0}; UINTN Matches[3] = {0};
  if (Handles && Count && Count <= 512) {
    for (UINTN I = 0; I < Count; I++) {
      EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
      if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)) || !Loaded) { continue; }
      UINT64 Base = (UINTN)Loaded->ImageBase;
      if (!Base || (Base & 4095) || Base > MAX_UINT64 - Loaded->ImageSize) { continue; }
      if (Loaded->ImageSize == 65536 && Base + 0xe0f8 == (UINTN)Rpmh) { Bases[0] = Base; Matches[0]++; }
      if (Loaded->ImageSize == 327680) { Bases[1] = Base; Matches[1]++; }
      if (Loaded->ImageSize == 32768 && Base + 0x6078 == (UINTN)CmdDb) { Bases[2] = Base; Matches[2]++; }
    }
  }
  if (Handles) { FreePool (Handles); }
  if (Matches[0] != 1 || Matches[1] != 1 || Matches[2] != 1) { H->bind_status = EFI_COMPROMISED_DATA; return; }
  /* The core checks every pinned relocated image byte and DAL pointers. */
  H->bind_status = mCepheusRpmhTailProtocol.Bind (Bases);
  if (!EFI_ERROR (H->bind_status)) { H->flags |= CE_TAIL_BOUND; }
}

STATIC VOID
CepheusRpmhTailHandoff (UINT64 Destination, UINTN Attempt)
{
  struct ceph_rpmh_tail_handoff *H = &mCepheusRpmhTailHandoff;
  H->flags |= CE_TAIL_EBS; H->ebs_attempts = (UINT32)Attempt + 1;
  if (H->flags & CE_TAIL_BOUND) {
    H->copy_status = mCepheusRpmhTailProtocol.Copy (&H->record);
    if (!EFI_ERROR (H->copy_status)) {
      if (ceph_rpmh_tail_valid (&H->record) || H->record.requests.ebs_attempts != H->ebs_attempts) {
        H->copy_status = EFI_COMPROMISED_DATA;
      } else { H->flags |= CE_TAIL_COPIED; }
    }
    if (H->copy_status) { ZeroMem (&H->record, sizeof (H->record)); }
  }
  H->checksum = ceph_rpmh_tail_handoff_checksum (H);
  CopyMem ((VOID *)(UINTN)Destination, H, sizeof (*H));
}
#endif
