/* Fixed diagnostic calls before EBS; no protocol pointers cross into XNU. */
#ifndef CEPHEUS_SCM_PAIR_H
#define CEPHEUS_SCM_PAIR_H
#include "cepheus_scm_handoff.h"

STATIC struct ceph_scm_handoff mCepheusScm;
STATIC BOOLEAN mCepheusScmAttempted;

STATIC int
CepheusScmLease (void *Context)
{
  (VOID)Context;
  return DarwinCurrentEl () == 4 ? 0 : -1;
}

STATIC __attribute__((noinline)) int
CepheusScmInvoke (void *Context, struct ceph_scm_regs *Regs)
{
  struct ceph_scm_handoff *H = Context;
  struct ceph_scm_handoff_call *Call;
  UINT64 El, Cpu, Ticks;
  if ((H->count >= CE_SCM_QUERY_LIMIT) || CepheusScmLease (Context)) { return -1; }
  Call = &H->calls[H->count++];
  Call->in = *Regs;
  register uint64_t X0 __asm__("x0") = Regs->x[0], X1 __asm__("x1") = Regs->x[1];
  register uint64_t X2 __asm__("x2") = Regs->x[2], X3 __asm__("x3") = Regs->x[3];
  register uint64_t X4 __asm__("x4") = Regs->x[4], X5 __asm__("x5") = Regs->x[5];
  register uint64_t X6 __asm__("x6") = Regs->x[6], X7 __asm__("x7") = Regs->x[7];
  __asm__ volatile("mrs %8, CurrentEL\n\tmrs %9, MPIDR_EL1\n\tmrs %10, CNTPCT_EL0\n\tsmc #0"
    : "+r"(X0), "+r"(X1), "+r"(X2), "+r"(X3), "+r"(X4), "+r"(X5), "+r"(X6), "+r"(X7),
      "=&r"(El), "=&r"(Cpu), "=&r"(Ticks)
    : : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
  *Regs = (struct ceph_scm_regs){{X0, X1, X2, X3, X4, X5, X6, X7}};
  Call->out = *Regs;
  Call->current_el = El;
  Call->mpidr = Cpu;
  Call->ticks = Ticks;
  Call->complete = 1;
  return 0;
}

STATIC VOID
CepheusScmCapture (VOID)
{
  struct ceph_scm_discovery Result = {0};
  struct ceph_scm_discovery_ops Ops = {&mCepheusScm, CepheusScmLease, CepheusScmInvoke};
  if (mCepheusScmAttempted) { return; }
  mCepheusScmAttempted = TRUE;
  ZeroMem (&mCepheusScm, sizeof (mCepheusScm));
  mCepheusScm.magic = CE_SCM_HANDOFF_MAGIC;
  mCepheusScm.version = 1;
  mCepheusScm.bytes = sizeof (mCepheusScm);
  mCepheusScm.current_el = DarwinCurrentEl ();
  mCepheusScm.mpidr = READ_SYSREG (mpidr_el1);
  mCepheusScm.begin_ticks = READ_SYSREG (cntpct_el0);
  mCepheusScm.error = ceph_scm_discover_pas13 (&Result, &Ops);
  mCepheusScm.end_ticks = READ_SYSREG (cntpct_el0);
  mCepheusScm.convention = Result.convention;
  mCepheusScm.available_mask = Result.available_mask;
  mCepheusScm.supported = Result.supported;
  mCepheusScm.terminal = Result.terminal;
  mCepheusScm.complete = 1;
}

STATIC VOID
CepheusScmHandoff (UINT64 Address, UINTN Attempt)
{
  mCepheusScm.ebs_attempts = (UINT32)Attempt + 1;
  mCepheusScm.ebs_done = 1;
  mCepheusScm.checksum = ceph_scm_handoff_checksum (&mCepheusScm);
  CopyMem ((VOID *)(UINTN)Address, &mCepheusScm, sizeof (mCepheusScm));
}
#endif
