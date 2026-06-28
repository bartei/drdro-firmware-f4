# drDRO Firmware (PlatformIO)

Firmware for the drDRO rotary-controller board (STM32F411CEU6). Migrated from the
STM32CubeIDE project `../rotary-controller-f4` (branch `main`, v2.0.1) to PlatformIO.

## Migration tracking
- `migration_todo.md` — detailed design/reference (no checkboxes).
- `migration_checklist.md` — one-liner progress tracker.
- Keep both in sync: detail in the todo, tick `[x]` in the checklist as items land.

## Toolchain / build
- PlatformIO Core (`pio`), platform `ststm32@~19.4.0`, `framework = stm32cube`.
- Build:  `pio run`
- Upload: `pio run -t upload`   (ST-Link)
- Clean:  `pio run -t clean`
- Verbose (check flags/float-ABI): `pio run -v`
- Artifacts: `.pio/build/drdro_f411ce/firmware.{elf,bin,hex}`

## Architecture
- MCU STM32F411CEU6, Cortex-M4F, 100 MHz (8 MHz HSE × PLL). 512K flash / 128K RAM.
- RTOS: FreeRTOS via CMSIS-RTOS v2 (vendored in `lib/FreeRTOS`), heap_4, tick 1 kHz.
  HAL timebase is **TIM11** (SysTick belongs to FreeRTOS; `SysTick_Handler` is in `cmsis_os2.c`).
- Comms: Modbus RTU **slave, addr 17, USART1 @115200** (vendored A. Mera lib in `lib/Modbus`).
  The register image is the `rampsHandler_t.shared` struct — field layout is the host contract.
- Motion: `src/Ramps.c` — TIM9 base-IT ISR (`SynchroRefreshTimerIsr`) generates step/dir;
  modes 0=disabled / 1=sync+index / 2=jog; accel/decel ramps. 4 quadrature scales on TIM1–4.
- Peripheral/pin map and full detail: see `migration_todo.md`.

## Layout
- `src/` app + frozen CubeMX glue (hand-maintained C; CubeMX is **not** regenerated).
- `include/` app + config headers (`FreeRTOSConfig.h`, `stm32f4xx_hal_conf.h` must stay here — globally visible to vendored libs).
- `lib/FreeRTOS`, `lib/Modbus` — vendored middleware.
- `STM32F411CEUX_FLASH.ld` — custom linker (memory/heap/stack identical to Cube project).
- `docs/` — frozen `rotary-controller-f4.ioc` reference + RAM linker script.

## Conventions
- HAL comes from the pinned PlatformIO framework (not vendored). Don't add `Drivers/`,
  `system_stm32f4xx.c`, or a startup `.s` — the framework supplies them.
- No AI/Claude attribution in commits or PR descriptions.
