# drDRO Firmware

Firmware for the drDRO rotary-controller / digital-readout board, based on the
STMicroelectronics **STM32F411CEU6** (Cortex-M4F, 100 MHz, 512 K flash / 128 K RAM).
Built with **PlatformIO** (`framework = stm32cube`), FreeRTOS, a custom RS485 line
protocol, and a dual-bank IAP bootloader for field firmware updates.

PlatformIO successor to the STM32CubeIDE project `rotary-controller-f4` (v2.0.1).

## What it does

- Reads **4 quadrature scales** (TIM1–TIM4 in encoder mode).
- Drives a **step/dir servo** with trapezoidal accel/decel ramps — jog, indexing, and
  scale-synchronised motion — from a TIM9 interrupt (relocated to RAM so it keeps
  stepping even during a flash write).
- Speaks a **custom line protocol** on USART1 @ 115200 8N1 (RS485, auto-direction).
- Stores rarely-changed settings (scale ratios, servo config, boot mode, active bank)
  in flash, power-fail-safe.
- **Field-updatable** over RS485 via a two-bank A/B bootloader (keep two versions, switch
  / roll back).

## Projects & layout

Two PlatformIO projects plus a shared contract:

- **`app/`** — the application firmware (linked at the Exec region `0x08020000`).
- **`bootloader/`** — the IAP bootloader (sector 0, `0x08000000`).
- **`shared/`** — `Bootloader.h` (flash map + handshake), `Settings.h`, `BlinkCode.h`;
  both projects build with `-I ../shared`.
- **`tools/`** — host-side helper scripts (see *Tooling* below).
- **`docs/`** — design docs, trackers, and the hardware-issues list (see *Documentation*).

### Flash map (512 KB)

| Region | Sector(s) | Address | Size | Role |
|---|---|---|---|---|
| Bootloader | 0 | `0x08000000` | 16 K | never erased by an update |
| Settings A / B | 1 / 2 | `0x08004000` / `0x08008000` | 16 K each | ping-pong, power-fail safe |
| *(reserved)* | 3–4 | `0x0800C000` | 80 K | future |
| **Exec** | 5 | `0x08020000` | 128 K | the app runs here |
| **Bank 0 / 1** | 6 / 7 | `0x08040000` / `0x08060000` | 128 K each | stored app versions |

Banking is **copy-on-activate**: the app is linked once for Exec; the bootloader copies
the active bank into Exec and jumps to it. Switching banks / rolling back just changes
the active-bank byte in settings.

## Build, test & flash

```sh
pio run  -d app                  # build the application  -> firmware.{elf,bin,hex}
pio run  -d bootloader           # build the bootloader
pio test -d app -e native        # host-side protocol/settings unit tests (no hardware)
pio run  -d app        -t upload # flash the app   via ST-Link (to Exec, 0x08020000)
pio run  -d bootloader -t upload # flash the bootloader via ST-Link (sector 0)
```

**First-time flashing** uses the combined factory image (bootloader + app-in-Exec):

```sh
python tools/make_factory.py factory.hex \
    bootloader/.pio/build/bootloader/firmware.hex \
    app/.pio/build/drdro_f411ce/firmware.hex
# then, e.g.:  st-flash --format ihex write factory.hex
```

CI builds both projects, runs the native tests, and publishes `drdro-app.*`,
`drdro-bootloader.*`, and `drdro-factory.hex` on tagged releases.

> **Note (`pio` on PATH):** on hosts without PlatformIO on PATH, wrap commands as
> `nix-shell -p platformio --run '…'`. Linker-script-only changes need `pio run -t clean`.

## Firmware updates

In production, firmware is updated **from the drDRO host application** (a separate Python
program that already talks to this board) — it drives the same bootloader protocol
described below. The scripts in `tools/` are **low-level developer utilities**, not the
production path, but are kept and documented for bring-up, debugging, and CI.

### Update flow (what the host does)

1. App command `update` → the app **jumps** to the bootloader (no reset — see *Hardware*).
2. Bootloader CLI greets `bootloader=ready`.
3. `flash <bank>` → host **YMODEM-sends** the new `.bin` into the inactive bank.
4. `bank <bank>` → select it as active (persisted to settings).
5. `boot` → bootloader copies the active bank into Exec and jumps to it.

Rollback is `bank <other>` + reboot, or the `rollback` command (on either CLI).

## Tooling (`tools/`)

Low-level, dependency-light helpers (Python 3; `dro_update.py` needs `pyserial`):

### `dro_update.py` — dual-bank updater over RS485/UART
Drives the whole update cycle from a host PC for development/bring-up. Self-contained
YMODEM sender (CRC-16, 1024-byte blocks — no `lrzsz`); baud fixed at 115200.

```sh
# auto-pick the inactive bank, flash, select, boot, verify:
python tools/dro_update.py /dev/ttyACM0 app/.pio/build/drdro_f411ce/firmware.bin
python tools/dro_update.py /dev/ttyACM0 firmware.bin --bank 1        # force a bank
python tools/dro_update.py /dev/ttyACM0 firmware.bin --in-bootloader # already in the CLI
python tools/dro_update.py /dev/ttyACM0 firmware.bin --no-boot       # flash+select, don't boot
```

### `make_factory.py` — combined factory image
Merges Intel-HEX files (no dependencies) into one image — bootloader + app-in-Exec — for
first-time flashing. Errors out on address overlap.

```sh
python tools/make_factory.py factory.hex bootloader.hex app.hex
```

## CLI command reference

Same wire format on both CLIs: `key=value` lines, a `crc=HH` line, then a blank line;
requests may carry an optional `*HH` XOR-8 checksum.

**Application** (USART1 @ 115200): `sta` (scale + servo position/speed), `get`/`set`
`<var>`, `settings`, `save`/`load` (persist/reload to flash), `bank [<0|1>]`, `rollback`,
`version`, `help`, `update` (enter bootloader), `reset`.
Variables include `scales.{pos,speed,num,den,sync}` (4-element arrays), `servo.{max,acc,
jog,mode,pos,speed,tgt}`, `diag.{cycles,interval}`.

**Bootloader** (greets `bootloader=ready`): `version`, `info` (banks: mode/active/loaded/
validity), `bank [<0|1>]`, `boot.mode <app|bl>`, `flash <0|1>` (YMODEM receive into a
bank), `erase <0|1>`, `crc <0|1>`, `copy`, `rollback`, `boot`, `reset`, `help`.

### USR_LED (PB12) — diagnostic heartbeat
Repeating blink pattern, ~1 Hz, **active-low**: **1 blink = app running**, **2 blinks =
bootloader**. Longer counts are reserved for app error codes.

## Hardware

See **`docs/HARDWARE.md`** for board-level issues the firmware works around and that
should be fixed on the next PCB revision — notably **HW-1: BOOT0 floats**, so a hardware
*reset* can intermittently boot the ST system ROM. The firmware avoids this by **jumping**
(never resetting) across every app↔bootloader handoff; the permanent fix is to tie BOOT0
to GND.

## Dev container

A ready-to-use VS Code dev container (PlatformIO + STM32 toolchain + serial tools) is in
`.devcontainer/` — open the folder and **Reopen in Container**. See
`.devcontainer/README.md` (incl. ST-Link USB notes).

## Documentation (`docs/`)

- `HARDWARE.md` — known hardware bugs to fix on the next PCB run.
- `dualbank_design.md` / `dualbank_todo.md` — dual-bank update system (current design + tracker).
- `protocol_design.md` / `protocol_todo.md` — line protocol + IAP rationale.
- `bootloader_todo.md` — IAP bootloader groundwork (B0–B5).
- `migration_todo.md` — peripheral/pin map + architecture reference (Cube→PlatformIO).
- `migration_checklist.md` — migration progress (motion HW-verification still open).

`CLAUDE.md` (repo root) is the orientation doc for the Claude Code agent; `RESUME.md` is
the pick-up-where-you-left-off handoff.

## License

GPL-3.0-or-later (see LICENSE). © Stefano Bertelli.
