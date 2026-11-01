#!/usr/bin/env bash
# Stage refusal-first reset/control code into a private XNU source copy.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
a=bsd/dev
install -m 0644 "$here/cepheus_reboot.c" "$here/cepheus_reboot.h" "$a/"
