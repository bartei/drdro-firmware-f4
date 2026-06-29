# Dev container — drDRO firmware

VS Code dev container with everything needed to build, test, flash, and debug the
firmware: PlatformIO Core + the `ststm32`/`stm32cube` toolchain (auto-fetched on
first build), a host C compiler for the native unit tests, serial terminals, and
the GitHub CLI.

## Use
Open the folder in VS Code → **Reopen in Container**. The first build warms the
cache (downloads the `arm-none-eabi` toolchain + framework — a few minutes, once;
cached in the `drdro-platformio` volume thereafter).

| Task   | Command |
|--------|---------|
| Build  | `pio run` |
| Test   | `pio test -e native` |
| Flash  | `pio run -t upload` (see USB notes) |
| Serial | `tio /dev/ttyUSB0 -b 115200` — then type `help`, `sta`, … |

## Flashing / debugging (ST-Link over USB)
USB passthrough is **Linux-host only**:
1. Plug the ST-Link into the host **before** opening the container.
2. Uncomment the `/dev/bus/usb` bind mount in `devcontainer.json`.
3. The container already runs `--privileged` for device access.
4. On the **host**, install PlatformIO's udev rules:
   ```sh
   curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/master/platformio/assets/system/99-platformio-udev.rules \
     | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
   sudo udevadm control --reload && sudo udevadm trigger
   ```
On macOS/Windows hosts, build/test in the container and flash from the host.

## How it's wired
- `Dockerfile` — base image + apt tools (compiler, serial, lrzsz, usbutils).
- `setup.sh` (postCreate) — installs PlatformIO Core via the official installer to
  `~/.platformio/penv` (so the CLI and the IDE extension share one core;
  `useBuiltinPIOCore=false`) and runs a warm build.
- `~/.platformio` is a named volume so the toolchain/packages survive rebuilds.
