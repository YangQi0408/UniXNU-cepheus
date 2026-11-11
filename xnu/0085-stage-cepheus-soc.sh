#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
install -m 0644 "$here/cepheus_soc.c" "$here/cepheus_soc.h" bsd/dev/
install -m 0644 "$here/cepheus_cpu_policy.h" bsd/dev/
install -m 0644 "$here/cepheus_gpu_power.h" bsd/dev/
install -m 0644 "$here/cepheus_gpu_idle.h" "$here/cepheus_gpu_idle_native_private.h" osfmk/arm64/
install -m 0644 "$here/cepheus_rpmh_snapshot.h" "$here/cepheus_rpmh_tail.h" "$here/cepheus_rpmh_tail_handoff.h" bsd/dev/
install -m 0644 "$here/cepheus_smem_map_handoff.h" bsd/dev/
