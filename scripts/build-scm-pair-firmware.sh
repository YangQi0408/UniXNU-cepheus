#!/usr/bin/env bash
# Isolated driver build; no sibling source edits or device access.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:?usage: build-scm-pair-firmware.sh NEW_OUTPUT_DIR [--rpmh-snapshot]}
test ! -e "$DEST"
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)
STAGE_OPTIONS=()
if [[ ${2:-} == --rpmh-snapshot ]]; then
    STAGE_OPTIONS+=(--rpmh-snapshot)
elif [[ -n ${2:-} ]]; then
    printf 'unknown build option: %s\n' "$2" >&2
    exit 2
fi
python3 "$ROOT/scripts/stage-scm-pair-firmware.py" "$ROOT/../unixnu/drivers/DarwinBoot" "$DEST/source" "${STAGE_OPTIONS[@]}"
export WORKSPACE=$DEST
export PACKAGES_PATH=$DEST/source:$ROOT/../unixnu/package:$ROOT/../uefi/MU_BASECORE
export EDK_TOOLS_PATH=$ROOT/../uefi/MU_BASECORE/BaseTools
export CONF_PATH=$DEST/Conf
export CLANG_BIN=${CLANG_BIN:-/usr/bin/}
export PYTHON_COMMAND=python3
export PATH=$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_PATH/Source/C/bin:$PATH
mkdir "$CONF_PATH"
cp "$EDK_TOOLS_PATH/Conf/target.template" "$CONF_PATH/target.txt"
cp "$EDK_TOOLS_PATH/Conf/tools_def.template" "$CONF_PATH/tools_def.txt"
cp "$EDK_TOOLS_PATH/Conf/build_rule.template" "$CONF_PATH/build_rule.txt"
cd "$WORKSPACE"
build -p DarwinBootPkg/DarwinBoot.dsc -m DarwinBootPkg/Driver/DarwinBoot.inf -a AARCH64 -b RELEASE -t CLANGPDB -n 4
sha256sum "$DEST/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi"
