# Dev container — drDRO firmware

VS Code dev container with everything needed to build, test, flash, and debug the
firmware: PlatformIO Core + the `ststm32`/`stm32cube` toolchain (auto-fetched on
first build), a host C compiler for the native unit tests, serial terminals, the
GitHub CLI, `st-flash` (raw ST-Link flashing), and the **Claude Code CLI** wired to
your host login.

## Use
Open the folder in VS Code → **Reopen in Container**. The first build warms the
cache (downloads the `arm-none-eabi` toolchain + framework — a few minutes, once;
cached in the `drdro-platformio` volume thereafter).

| Task   | Command |
|--------|---------|
| Build  | `pio run` |
| Test   | `pio test -e native` |
| Flash  | `pio run -t upload`  •  or `st-flash write firmware.bin 0x08000000` |
| Serial | `tio /dev/ttyUSB0 -b 115200` — then type `help`, `sta`, … |
| Claude | `claude` — already signed in with your host account |

## Claude Code
`claude` is installed in the image (native standalone build, in `~/.local/bin`) and
the container **bind-mounts your host `~/.claude` and `~/.claude.json`**. That means
it shares the host's login (`~/.claude/.credentials.json`), settings, global
`CLAUDE.md`, and memory — no separate `/login` inside the container.

- Sharing works because the container's `vscode` user is uid 1000, matching the host
  user, so the mounted config files are owned correctly.
- The container's claude has `DISABLE_AUTOUPDATER=1` so it won't self-update and
  rewrite the shared host config; update Claude on the host as usual.
- Because the config is *shared* (not copied), sessions/history from the container
  land in your host `~/.claude`.

## Flashing / debugging (ST-Link over USB)
USB device passthrough is **Linux-host only**. The container runs `--privileged`
with host networking and bind-mounts all of `/dev`, so any USB-connected ST-Link
(and USB-serial adapter) is visible — including devices hot-plugged after the
container started. No per-device flags, no uncommenting, no pre-plug requirement.

Two ways to flash:
- `pio run -t upload` — PlatformIO's uploader (OpenOCD/ST-Link).
- `st-flash write app/.pio/build/drdro_f411ce/firmware.bin 0x08020000` — raw
  ST-Link. (`st-info --probe` lists connected probes; `0x08000000` for the
  bootloader, `0x08020000` for the app — see the linker scripts.)

On the **host**, install PlatformIO's udev rules once so the ST-Link is accessible
without root:
```sh
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/master/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload && sudo udevadm trigger
```
On macOS/Windows hosts, build/test in the container and flash from the host (the
`/dev` bind mount and `--network=host` are Linux-only; drop them there).

## How it's wired
- `Dockerfile` — base image + apt tools (compiler, serial, lrzsz, usbutils,
  **stlink-tools**) and the **Claude Code** native install for the `vscode` user.
- `devcontainer.json` — `--privileged` + `--network=host`, `/dev` bind mount (full
  device access), host `~/.claude`/`~/.claude.json` bind mounts (shared Claude
  login), and the PlatformIO extension pinned to the volume's CLI core.
- `setup.sh` (postCreate) — installs PlatformIO Core via the official installer to
  `~/.platformio/penv` (so the CLI and the IDE extension share one core;
  `useBuiltinPIOCore=false`), warms the build, and reports claude / st-flash status.
- `~/.platformio` is a named volume so the toolchain/packages survive rebuilds.

## PlatformIO extension note
`platformio-ide.customPATH` points the extension at the CLI core installed in the
`~/.platformio` volume, and `useBuiltinPIOCore=false` stops it from downloading and
installing its *own* core on open — which otherwise races the `setup.sh` install and
triggers the "installing PlatformIO Core…" prompt on every container open.
