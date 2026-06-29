# drDRO Firmware

Firmware for the drDRO rotary-controller / digital-readout board, based on the
STMicroelectronics **STM32F411CEU6** microcontroller. Built with **PlatformIO**
(`framework = stm32cube`, FreeRTOS + a custom RS485 line protocol).

This is the PlatformIO successor to the STM32CubeIDE project `rotary-controller-f4`.

## Build & flash

Two PlatformIO projects: **`app/`** (the firmware, runs at `0x08004000`) and
**`bootloader/`** (IAP firmware-update bootloader, sector 0 `0x08000000`). `shared/`
holds the app↔bootloader contract header. Pass `-d <dir>` (or `cd` into the project):

```sh
pio run -d app                 # build the application
pio run -d bootloader          # build the bootloader
pio test -d app -e native      # host-side protocol unit tests
pio run -d app -t upload       # flash app via ST-Link
pio run -d bootloader -t upload # flash bootloader
```

A full board = bootloader @ `0x08000000` + app @ `0x08004000` (flash both; the
bootloader jumps to the app). Output: `app/.pio/build/drdro_f411ce/firmware.{elf,bin,hex}`
and `bootloader/.pio/build/bootloader/firmware.{elf,bin,hex}`.

### Dev container
A ready-to-use VS Code dev container (PlatformIO + STM32 toolchain + serial tools)
is in `.devcontainer/` — open the folder and **Reopen in Container**. See
`.devcontainer/README.md` (incl. ST-Link USB notes).

## What it does

- Reads 4 quadrature scales (TIM1–TIM4 in encoder mode).
- Drives a step/dir servo with trapezoidal accel/decel ramps (jog, indexing, and
  scale-synchronised motion) from a TIM9 interrupt.
- Exposes state and accepts commands over a **custom line protocol** on USART1
  @ 115200 8N1 (RS485) — `sta`/`set`/`get`/`settings`/`version`/`help`. See
  `protocol_design.md`.

## Migration status

See `migration_checklist.md` for progress and `migration_todo.md` for the design
reference and peripheral/pin map.

## License

MIT (see source headers). © Stefano Bertelli.
