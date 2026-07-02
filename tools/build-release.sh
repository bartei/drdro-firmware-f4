#!/usr/bin/env bash
# python-semantic-release build_command: invoked by `semantic-release version` with NEW_VERSION
# in the environment. Builds app + bootloader with the version baked in (FW_VERSION, picked up
# by app/support/fw_version.py), then stages the release assets into dist/ for
# `semantic-release publish`.
#
# Asset names are the contract with the drdro-software-f4 firmware updater
# (dro/comms/updater.py matches "drdro-app.bin") — keep them stable and unversioned;
# the release tag carries the version.
set -euo pipefail

: "${NEW_VERSION:?build-release.sh must be run by semantic-release (NEW_VERSION unset)}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export FW_VERSION="v${NEW_VERSION}"

pio run -d "$HERE/app"
pio run -d "$HERE/bootloader"
python3 "$HERE/tools/make_factory.py" "$HERE/factory.hex" \
  "$HERE/bootloader/.pio/build/bootloader/firmware.hex" \
  "$HERE/app/.pio/build/drdro_f411ce/firmware.hex"

mkdir -p "$HERE/dist"
for ext in bin hex elf; do
  cp "$HERE/app/.pio/build/drdro_f411ce/firmware.$ext"      "$HERE/dist/drdro-app.$ext"
  cp "$HERE/bootloader/.pio/build/bootloader/firmware.$ext" "$HERE/dist/drdro-bootloader.$ext"
done
cp "$HERE/factory.hex" "$HERE/dist/drdro-factory.hex"
(cd "$HERE/dist" && sha256sum drdro-* > SHA256SUMS.txt)
ls -lh "$HERE/dist"
