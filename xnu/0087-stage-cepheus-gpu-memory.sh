#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
mkdir -p iokit/Kernel/arm
install -m 0644 "$here/cepheus_gpu_memory.cpp" "$here/cepheus_gpu_memory.h" "$here/cepheus_gpu_memory_state.h" "$here/cepheus_gpu_firmware.h" iokit/Kernel/arm/
install -m 0644 "$here/cepheus_gpu_memory.h" "$here/cepheus_gpu_compile.c" "$here/cepheus_gpu_compile.h" bsd/dev/
for header in cepheus_hfi.h cepheus_hfi_boot.h cepheus_hfi_messages.h cepheus_hfi_session.h \
    cepheus_arc.h cepheus_gmu_firmware.h cepheus_gmu_upload.h cepheus_smmu_plan.h \
    cepheus_smmu_install.h cepheus_gpu_power.h cepheus_gpu_power_txn.h cepheus_gpu_submit.h cepheus_gmu_start.h \
    cepheus_gpu_ring.h cepheus_a640_pm4.h cepheus_sqe.h cepheus_gpu_bw.h cepheus_bcm.h cepheus_gpu_firmware.h cepheus_gpu_fault.h cepheus_gpu_vm.h; do
    install -m 0644 "$here/$header" bsd/dev/
done
