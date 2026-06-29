# drDRO Firmware (PlatformIO)

> **Resuming after a break? Read `RESUME.md` first** — current state, next steps,
> build/test commands, and gotchas.

Firmware for the drDRO rotary-controller board (STM32F411CEU6). Migrated from the
STM32CubeIDE project `../rotary-controller-f4` (branch `main`, v2.0.1) to PlatformIO.

## Migration tracking
- `migration_todo.md` — detailed design/reference (no checkboxes).
- `migration_checklist.md` — one-liner progress tracker.
- Keep both in sync: detail in the todo, tick `[x]` in the checklist as items land.

## Toolchain / build
- PlatformIO Core (`pio`), platform `ststm32@~19.4.0`, `framework = stm32cube`.
- **Two sibling projects:** `app/` (the firmware) and `bootloader/` (IAP). `shared/` holds the
  app↔bootloader contract (`Bootloader.h`). Pass `-d <dir>` to `pio` (or `cd` into the project).
- Build:  `pio run -d app`            (bootloader: `pio run -d bootloader`)
- Upload: `pio run -d app -t upload`  (ST-Link; bootloader: `-d bootloader`)
- Test:   `pio test -d app -e native` (host-side protocol unit tests; HAL/RTOS mocked under app/test/mocks/)
- Clean:  `pio run -d app -t clean`   (**linker-script-only changes need a clean rebuild to relink**)
- Artifacts: `app/.pio/build/drdro_f411ce/firmware.{elf,bin,hex}`,
  `bootloader/.pio/build/bootloader/firmware.{elf,bin,hex}`
- CI: `.github/workflows/ci.yml` (builds both + native tests) and `release.yml` (semver tag + release on `main`).

## Architecture
- MCU STM32F411CEU6, Cortex-M4F, 100 MHz (8 MHz HSE × PLL). 512K flash / 128K RAM.
- RTOS: FreeRTOS via CMSIS-RTOS v2 (vendored in `app/lib/FreeRTOS`), heap_4, tick 1 kHz.
  HAL timebase is **TIM11** (SysTick belongs to FreeRTOS; `SysTick_Handler` is in `cmsis_os2.c`).
- Comms: custom CLI line protocol — **USART1 @115200, RS485 auto-direction** (`app/src/Protocol.c`;
  replaced Modbus). The register image is the `rampsHandler_t.shared` struct — field layout is the host contract.
- Motion: `app/src/Ramps.c` — TIM9 base-IT ISR (`SynchroRefreshTimerIsr`) generates step/dir;
  modes 0=disabled / 1=sync+index / 2=jog; accel/decel ramps. 4 quadrature scales on TIM1–4.
- Peripheral/pin map and full detail: see `migration_todo.md`.

## Layout (two PlatformIO projects + shared contract; docs at root)
- `app/` — the application project:
  - `src/` app + frozen CubeMX glue (hand-maintained C; CubeMX is **not** regenerated).
  - `include/` app + config headers (`FreeRTOSConfig.h`, `stm32f4xx_hal_conf.h` — globally visible to vendored libs).
  - `lib/FreeRTOS` — vendored middleware.  `support/` — build extra scripts.  `test/` — native unit tests + mocks.
  - `STM32F411CEUX_FLASH_APP.ld` (app @ `0x08004000`) + `STM32F411CEUX_FLASH.ld` (original 0-based, reference).
- `bootloader/` — IAP bootloader project (`src/main.c`, own ldscript @ `0x08000000`). See `bootloader_todo.md`.
- `shared/Bootloader.h` — app↔bootloader contract (flash layout, handshake word); both projects add `-I ../shared`.
- `docs/` — frozen `rotary-controller-f4.ioc` reference + RAM linker script.

## Conventions
- HAL comes from the pinned PlatformIO framework (not vendored). Don't add `Drivers/`,
  `system_stm32f4xx.c`, or a startup `.s` — the framework supplies them.
- No AI/Claude attribution in commits or PR descriptions.
