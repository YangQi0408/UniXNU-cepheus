#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# DXB executes all stage scripts BEFORE applying patches. sweet_rpmh.c is
# created by the upstream patch series, so only the base XNU tree exists here.
test -f bsd/conf/files
test -d bsd/dev
install -m 0644 "$here/cepheus_arc.h" "$here/cepheus_arc_probe.h" "$here/cepheus_bcm.h" \
    "$here/cepheus_arc_active.h" "$here/cepheus_arc_active_private.h" \
    "$here/cepheus_bcm_active.h" "$here/cepheus_bcm_active_private.h" \
    "$here/cepheus_rpmh_active_private.h" "$here/cepheus_rpmh_scope.h" \
    "$here/cepheus_rpmh_scope_private.h" \
    "$here/cepheus_gpu_config_native.h" "$here/cepheus_gpu_config_native_private.h" \
    "$here/cepheus_gpu_config.h" "$here/cepheus_gpu_bw.h" "$here/cepheus_gpu_pdc.h" \
    "$here/cepheus_hfi_boot.h" "$here/cepheus_hfi_messages.h" "$here/cepheus_hfi_session.h" \
    "$here/cepheus_hfi.h" "$here/cepheus_gpu_bin.h" bsd/dev/
