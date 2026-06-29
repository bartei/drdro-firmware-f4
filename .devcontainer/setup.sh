#!/usr/bin/env bash
#
# Dev container provisioning: install PlatformIO Core into the ~/.platformio volume
# and warm the build cache (platform + stm32cube framework + arm-none-eabi toolchain).
#
set -euo pipefail

# The ~/.platformio volume mounts as root the first time — make it ours.
sudo mkdir -p "$HOME/.platformio"
sudo chown -R "$(id -u):$(id -g)" "$HOME/.platformio"

# Install PlatformIO Core (idempotent). The official installer creates the
# ~/.platformio/penv layout that the PlatformIO IDE extension expects, so the
# CLI and the extension share one core (see useBuiltinPIOCore=false).
if [ ! -x "$HOME/.platformio/penv/bin/pio" ]; then
  echo "[setup] Installing PlatformIO Core..."
  curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
  python3 /tmp/get-platformio.py
  rm -f /tmp/get-platformio.py
fi

export PATH="$HOME/.platformio/penv/bin:$PATH"
echo "[setup] $(pio --version)"

# Warm the cache and verify the toolchain builds the project. One-time, ~minutes;
# non-fatal so the container still comes up if offline.
echo "[setup] Warming build (downloads the arm toolchain on first run)..."
pio run -e drdro_f411ce || echo "[setup] warm build skipped/failed — run 'pio run' manually once online."

echo "[setup] Done. Build: pio run | Test: pio test -e native | Flash: pio run -t upload"
