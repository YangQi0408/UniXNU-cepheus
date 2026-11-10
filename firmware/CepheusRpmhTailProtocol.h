/* Opt-in core diagnostic protocol, not a hardware resource lease. */
#ifndef CEPHEUS_RPMH_TAIL_PROTOCOL_H
#define CEPHEUS_RPMH_TAIL_PROTOCOL_H
#include "cepheus_rpmh_tail.h"
#define CE_RPMH_TAIL_PROTOCOL_GUID {0x2401bd96,0x43d7,0x4349,{0x9e,0x0a,0xca,0x00,0xce,0x30,0x04,0x01}}
typedef struct {
  UINT32 Version, Bytes;
  EFI_STATUS (EFIAPI *Bind)(CONST UINT64 [3]);
  EFI_STATUS (EFIAPI *Copy)(struct ceph_rpmh_tail *);
} CE_TAIL_PROTOCOL;
#endif
