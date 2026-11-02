#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/dev/sweet_rpmh.c
install -m 0644 "$here/cepheus_arc.h" "$here/cepheus_arc_probe.h" bsd/dev/
