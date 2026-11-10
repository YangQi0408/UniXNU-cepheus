#!/usr/bin/env bash
# Build staged real core, never edit sibling trees or access the device.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:?usage: build-pool-lifetime-core.sh NEW_OUTPUT_DIR}
test ! -e "$DEST"
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)
OPTIONS=()
if [[ ${2:-} == --rpmh-tail ]]; then
  OPTIONS+=(--rpmh-tail)
elif [[ -n ${2:-} ]]; then
  printf 'unknown build option: %s\n' "$2" >&2
  exit 2
fi
python3 "$ROOT/scripts/stage-pool-lifetime-core.py" "$ROOT/../uefi/MU_BASECORE" "$DEST/source" "${OPTIONS[@]}"
cp "$ROOT/firmware/PoolLifetimeCore.dsc" "$DEST/PoolLifetimeCore.dsc"
export WORKSPACE=$DEST
export PACKAGES_PATH=$DEST/source:$ROOT/../uefi/MU_BASECORE
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
build -p PoolLifetimeCore.dsc -m MdeModulePkg/Core/Dxe/DxeMain.inf -a AARCH64 -b RELEASE -t CLANGPDB -n 4 > "$DEST/build.log" 2>&1 || {
  tail -n 80 "$DEST/build.log"
  exit 1
}
tail -n 5 "$DEST/build.log"
sha256sum "$DEST/build/PoolLifetimeCore/RELEASE_CLANGPDB/AARCH64/DxeCore.efi"
