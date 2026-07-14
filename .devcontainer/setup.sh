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

# Report the extra tooling so a broken share/install is obvious at create time.
echo "[setup] claude:   $(command -v claude >/dev/null && claude --version 2>/dev/null || echo 'NOT FOUND')"
if [ -r "$HOME/.claude/.credentials.json" ]; then
  echo "[setup] claude:   host login shared (~/.claude/.credentials.json present)"
else
  echo "[setup] claude:   ~/.claude/.credentials.json not found — run 'claude' and /login once, or check the host bind mount."
fi
echo "[setup] st-flash: $(st-flash --version 2>&1 | head -n1 || echo 'NOT FOUND')"

echo "[setup] Done. Build: pio run | Test: pio test -e native | Flash: pio run -t upload (or st-flash write firmware.bin 0x08000000)"
