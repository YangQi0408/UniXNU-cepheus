#!/usr/bin/env bash
# darwin-xnu-build executes scripts in the XNU source root before .patch.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[[ -f bsd/conf/files ]] || { echo 'not an XNU source root' >&2; exit 1; }
install -m 0644 "$here/cepheus_touch.c" bsd/dev/cepheus_touch.c
install -m 0644 "$here/cepheus_fts_events.h" bsd/dev/cepheus_fts_events.h
install -m 0644 "$here/cepheus_fts_sysinfo.h" bsd/dev/cepheus_fts_sysinfo.h
install -m 0644 "$here/cepheus_clock_status.h" bsd/dev/cepheus_clock_status.h
install -m 0644 "$here/cepheus_gpi_layout.h" bsd/dev/cepheus_gpi_layout.h
install -m 0644 "$here/cepheus_i2c_bitbang.h" bsd/dev/cepheus_i2c_bitbang.h
