/* Explicit pre-EBS diagnostic only. No SMEM load or GPU authorization. */
#ifndef CEPHEUS_SMEM_MAP_PROBE_H
#define CEPHEUS_SMEM_MAP_PROBE_H
#include <PiDxe.h>
#include <Library/PlatformMemoryMapLib.h>

#define CE_SMEM_PROBE_PAGES 512u
#define CE_SMEM_PROBE_BASE 0x86000000ULL
#define CE_SMEM_PROBE_BYTES 0x200000ULL

typedef struct {
  UINT64 El, Cpu, Sctlr, Tcr, Ttbr0, Ttbr1, Mair, Mmfr0;
} CE_SMEM_CPU_MAP;

typedef struct {
  EFI_STATUS Status;
  CE_SMEM_CPU_MAP Before, After;
  EFI_HOB_RESOURCE_DESCRIPTOR Resource;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Gcd;
  UINT64 Par[CE_SMEM_PROBE_PAGES];
  UINT32 Hobs, Pages, ResourceMatches;
  BOOLEAN ResourceComplete, GcdComplete, CpuStable;
} CE_SMEM_MAP_EVIDENCE;

/* Profile must come from the identity-bound compiled platform table, not
 * a guessed address or the unaudited UEFI Config ShLib. Prepare creates an
 * ExitBootServices event; Close is mandatory before unloading this module.
 * Trusted EFI table pointers and ordinary accessible output RAM, disjoint
 * from firmware tables and module state, are caller preconditions.
 * Probe is one-shot, retains partial evidence, and never grants permission
 * to read SMEM. Stage-1 success is not EL2/XPU/bus-abort containment. */
EFI_STATUS CepheusSmemMapPrepare (CONST ARM_MEMORY_REGION_DESCRIPTOR_EX *Profile);
EFI_STATUS CepheusSmemMapProbe (CE_SMEM_MAP_EVIDENCE *Output);
EFI_STATUS CepheusSmemMapClose (VOID);
#endif
