#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
mkdir -p iokit/Kernel/arm
install -m 0644 "$here/cepheus_gpu_memory.cpp" "$here/cepheus_gpu_memory.h" iokit/Kernel/arm/
install -m 0644 "$here/cepheus_gpu_memory.h" "$here/cepheus_gpu_compile.c" "$here/cepheus_gpu_compile.h" bsd/dev/
for header in cepheus_hfi.h cepheus_hfi_boot.h cepheus_hfi_messages.h cepheus_hfi_session.h \
    cepheus_arc.h cepheus_gmu_firmware.h cepheus_gmu_upload.h cepheus_smmu_plan.h \
    cepheus_smmu_install.h cepheus_gpu_power.h cepheus_gpu_power_txn.h cepheus_gpu_submit.h cepheus_gmu_start.h; do
    install -m 0644 "$here/$header" bsd/dev/
done
