# drDRO Firmware Migration — Design / Reference (todo)

Detailed reference for the STM32CubeIDE → PlatformIO migration of the drDRO
rotary-controller firmware. **No checkboxes here** — progress is tracked in
`migration_checklist.md`. This file holds the *why* and the *detail* behind each
checklist line.

## Source of truth
- Old project: `../rotary-controller-f4`, branch **`main`** (shipping v2.0.1,
  single-servo). The `multi_servo` branch is WIP and explicitly **out of scope**.
- Read old files from the ref, e.g. `git -C ../rotary-controller-f4 show main:Core/Src/Ramps.c`.

## Locked decisions
- **Freeze** the CubeMX-generated code; hand-maintain in C. `.ioc` kept only as frozen reference in `docs/`.
- **Faithful 1:1 port** first — build + runtime behavior identical; verify against the old `.elf`/`.map`. Quirks preserved, not fixed (see Quirk log).
- **HAL source = PlatformIO bundled** `framework-stm32cubef4`, platform pinned (`ststm32@~19.4.0`). Vendor-exact-V1.27.1 is the documented fallback if a behavioral diff shows up.

## Target hardware / clock
- MCU: STM32F411CEU6, Cortex-M4F, 512K flash / 128K RAM, UFQFPN48. Board: `genericSTM32F411CE`.
- Clock: 8 MHz HSE → PLL (M=4, N=100, P=2) → 100 MHz SYSCLK; APB1=50 MHz, APB2=100 MHz, timer clocks 100 MHz. Flash latency 3, VOS scale 1.
- `HSE_VALUE=8000000` and module enables come from our own `stm32f4xx_hal_conf.h`; `SystemClock_Config()` in `main.c` is explicit — board defaults don't affect the clock.

## Peripheral map (preserve exactly)
- TIM1/TIM2/TIM3/TIM4 — encoder mode TI12 (4 quadrature scales). Re-init'd by `Scales.c::initScaleTimer`.
- TIM9 — base interrupt, prescaler 100-1, period 20-1 → drives `SynchroRefreshTimerIsr` (step generation). Started with `HAL_TIM_Base_Start_IT`.
- TIM11 — HAL timebase (frees SysTick for FreeRTOS); `HAL_IncTick` from `HAL_TIM_PeriodElapsedCallback`.
- USART1 @115200 8N1 — Modbus RTU slave, interrupt mode (`HAL_UART_Receive_IT`).
- GPIO: STEP=PA0, DIR=PB14, ENA=PB15 (ENA_DELAY_MS=500), USR_LED=PB12, SPARE_1=PA1, SPARE_2=PA3, SPARE_3=PA4.

## Encoder pins (scales)
- TIM1: PA8/PA9 · TIM2: PA5/PB3 · TIM3: PA6/PA7 · TIM4: PB6/PB7 (all pull-up, very-high speed).

## Application architecture (the IP)
- `Ramps.c/.h` — motion core. `rampsHandler_t` wraps a nested `rampsSharedData_t shared`
  (the Modbus register image). Tasks: `userLedTask`, `updateSpeedTask`, `servoEnableTask`
  (all osPriorityLow, 512 B stacks). Motion modes via `fastData.servoMode`: 0=disabled,
  1=sync/index, 2=jog. Accel/decel ramping with fixed-point error accumulation
  (denominator 100000000). DWT cycle counter used for ISR timing.
- `Scales.c/.h` — `initScaleTimer()` configures a TIM in encoder mode; `SCALES_COUNT=4`.
- Modbus exposes the whole `shared` struct: `u16regs=&shared`, `u16regsize=sizeof(shared)/2`.
  The struct field layout *is* the Modbus map — document before any future struct change.

## FreeRTOS specifics
- CMSIS-RTOS v2 wrapper over FreeRTOS. heap_4, port ARM_CM4F, `configTOTAL_HEAP_SIZE=15360`,
  tick 1000 Hz, FPU on, `configUSE_NEWLIB_REENTRANT=1`, static+dynamic alloc.
- Handler wiring: `SVC_Handler`/`PendSV_Handler` remapped in `FreeRTOSConfig.h`;
  **`SysTick_Handler` is defined in `cmsis_os2.c`** (because timebase is TIM11) — must vendor it.

## Target layout
```
platformio.ini  .gitignore  CLAUDE.md  README.md  migration_todo.md  migration_checklist.md
STM32F411CEUX_FLASH.ld
docs/  rotary-controller-f4.ioc  STM32F411CEUX_RAM.ld
include/  main.h gpio.h tim.h usart.h stm32f4xx_it.h Ramps.h Scales.h FreeRTOSConfig.h stm32f4xx_hal_conf.h
src/      main.c gpio.c tim.c usart.c stm32f4xx_it.c stm32f4xx_hal_msp.c
          stm32f4xx_hal_timebase_tim.c freertos.c Ramps.c Scales.c syscalls.c sysmem.c
lib/FreeRTOS/  library.json  src/{tasks,queue,list,timers,event_groups,stream_buffer,croutine,port,heap_4,cmsis_os2}.c  include/*
lib/Modbus/    library.json  src/{Modbus,UARTCallback}.c  include/{Modbus,ModbusConfig}.h
```

## File disposition (old → new)
- App IP → `Ramps.c`,`Scales.c`→`src/`; `Ramps.h`,`Scales.h`→`include/`.
- Cube glue → `src/`: `main.c gpio.c tim.c usart.c stm32f4xx_it.c stm32f4xx_hal_msp.c stm32f4xx_hal_timebase_tim.c freertos.c`; headers → `include/`: `main.h gpio.h tim.h usart.h stm32f4xx_it.h FreeRTOSConfig.h stm32f4xx_hal_conf.h`.
- Newlib stubs → `src/`: `syscalls.c sysmem.c` (keep; drop if framework duplicates — see Risks).
- Vendor FreeRTOS → `lib/FreeRTOS/` (7 core .c + port.c + heap_4.c + cmsis_os2.c + all `Source/include/*.h` + CMSIS_RTOS_V2 headers + portmacro.h).
- Vendor Modbus → `lib/Modbus/` (`Modbus.c UARTCallback.c` + `Modbus.h ModbusConfig.h`).
- From framework, **do not copy**: `Drivers/*`, `system_stm32f4xx.c`, `startup_*.s`.
- Linker `STM32F411CEUX_FLASH.ld` → repo root (memory 0x08000000/512K, 0x20000000/128K, heap 0x200, stack 0x400).
- Drop: CMake/Makefile/.cproject/.project/.mxproject/.settings/.cmake/cmake-build-*/prebuilt binaries/Testing/st_nucleo_f4.cfg.
- Reference → `docs/`: `rotary-controller-f4.ioc`, `STM32F411CEUX_RAM.ld`.

## Build config (platformio.ini)
- `platform=ststm32@~19.4.0`, `board=genericSTM32F411CE`, `framework=stm32cube`.
- `board_build.ldscript=STM32F411CEUX_FLASH.ld`.
- `build_unflags=-Os`; `build_flags=-Ofast -I include -D USE_HAL_DRIVER -D STM32F411xE -D ARM_MATH_CM4 -D ARM_MATH_MATRIX_CHECK -D ARM_MATH_ROUNDING`.
- `lib_ldf_mode=chain+`; `upload_protocol=stlink`; `debug_tool=stlink`.
- Float ABI: rely on board default hard-float (`-mfloat-abi=hard -mfpu=fpv4-sp-d16`); verify via `pio run -v`; do **not** hand-add float flags (ABI-split risk).

## Risks & gotchas
- **Duplicate symbols** (`SystemInit`, `Reset_Handler`, vector table, `_sbrk`): framework provides startup + `system_*.c` — don't copy them. If `syscalls.c`/`sysmem.c` collide, exclude via `build_src_filter = +<*> -<syscalls.c> -<sysmem.c>`.
- **HAL version drift** (bundled vs V1.27.1): low risk — only stable APIs used. `stm32f4xx_hal_conf.h` controls module enables + `HSE_VALUE`. Fallback: vendor `Drivers/` into `lib/STM32_HAL/` and disable framework HAL.
- **`FreeRTOSConfig.h` visibility**: must be in `include/` (global) so the vendored FreeRTOS lib finds it.
- **LDF**: if `Modbus.h`/FreeRTOS headers not found, bump to `lib_ldf_mode=deep+`.
- **Size delta**: small flash/RAM delta vs old `.map` is expected (HAL patch version + GCC 16 vs old toolchain).

## Quirk log (preserved as-is; candidates for later cleanup pass)
- `Ramps.h` declares `static timServoEnableOn/OffCallback` prototypes that are never defined (harmless warning).
- `ModbusConfig.h` sets `ENABLE_USART_DMA 1` but runtime uses interrupt mode (`xTypeHW=USART_HW`) — DMA path compiled, unused.
- Modbus register map = raw `shared` struct layout (implicit contract with the host UI).

## Verification (Phase 4, on hardware)
- USR_LED idle + Modbus-activity flicker; Modbus RTU slave addr **17** @115200 — read holding regs vs `shared` offsets.
- Encoder counts on all 4 scales; jog (mode 2), indexing (mode 1), scale-sync motion; step/dir + ENA timing.
- A/B against current shipping firmware.

## CI / remote (Phase 5, optional)
- Replace `release.yaml` cmake/make with `pio run`; keep PaulHatch semantic-version tagging + release of `firmware.{bin,hex,elf}` from `.pio/build/drdro_f411ce/`.
- New GitHub remote `drdro-firmware`.
