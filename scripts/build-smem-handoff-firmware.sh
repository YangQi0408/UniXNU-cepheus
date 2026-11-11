#!/usr/bin/env bash
# Local isolated DarwinBoot link, not FV/boot packaging or device execution.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:?usage: build-smem-handoff-firmware.sh NEW_OUTPUT_DIR DARWINBOOT_TREE UEFI_TREE UNIXNU_PACKAGE}
REPOSITORY=${2:?explicit DarwinBoot tree required}
UEFI=${3:?explicit UEFI tree required}
PACKAGE=${4:?explicit UniXNU package required}
UEFI=$(cd "$UEFI" && pwd)
PACKAGE=$(cd "$PACKAGE" && pwd)
test ! -e "$DEST"
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)
python3 "$ROOT/scripts/stage-smem-handoff-firmware.py" --repository "$REPOSITORY" --uefi "$UEFI" --output "$DEST/source"
export WORKSPACE=$DEST
export PACKAGES_PATH=$DEST/source:$PACKAGE:$UEFI/MU_BASECORE
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
build -p DarwinBootPkg/DarwinBoot.dsc -m DarwinBootPkg/Driver/DarwinBoot.inf -a AARCH64 -b RELEASE -t CLANGPDB -n 4
sha256sum "$DEST/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi"
