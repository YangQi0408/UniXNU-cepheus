#!/usr/bin/env bash
# Standalone local driver build only; never package, boot or change source trees.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:?usage: build-smem-map-firmware.sh NEW_OUTPUT_DIR UEFI_TREE}
UEFI=${2:?explicit UEFI tree required}
UEFI=$(cd "$UEFI" && pwd)
test ! -e "$DEST"
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)
python3 "$ROOT/scripts/stage-smem-map-firmware.py" --uefi "$UEFI" --output "$DEST/source"
export WORKSPACE=$DEST
export PACKAGES_PATH=$DEST/source:$UEFI/MU_BASECORE
export EDK_TOOLS_PATH=$UEFI/MU_BASECORE/BaseTools
export CONF_PATH=$DEST/Conf
export CLANG_BIN=${CLANG_BIN:-/usr/bin/}
export PYTHON_COMMAND=python3
export PATH=$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_PATH/Source/C/bin:$PATH
mkdir "$CONF_PATH"
cp "$EDK_TOOLS_PATH/Conf/target.template" "$CONF_PATH/target.txt"
cp "$EDK_TOOLS_PATH/Conf/tools_def.template" "$CONF_PATH/tools_def.txt"
cp "$EDK_TOOLS_PATH/Conf/build_rule.template" "$CONF_PATH/build_rule.txt"
cd "$WORKSPACE"
build -p SmemMapProbePkg/SmemMapProbe.dsc -m SmemMapProbePkg/Driver/SmemMapProbe.inf -a AARCH64 -b RELEASE -t CLANGPDB -n 4
sha256sum "$DEST/build/SmemMapProbe/RELEASE_CLANGPDB/AARCH64/CepheusSmemMapProbe.efi"
