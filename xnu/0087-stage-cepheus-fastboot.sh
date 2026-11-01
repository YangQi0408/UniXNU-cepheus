#!/usr/bin/env bash
# Stage the optional read-only UDP endpoint into a private XNU copy.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
install -m 0644 "$here/cepheus_fastboot.c" "$here/cepheus_fastboot.h" bsd/dev/
