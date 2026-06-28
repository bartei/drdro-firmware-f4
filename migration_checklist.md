# drDRO Migration — Progress Checklist

One-liner progress tracker. Detail for each item lives in `migration_todo.md`.

## Phase 0 — Repo + PlatformIO + docs
- [x] `git init` (branch `main`)
- [x] `pio project init --board genericSTM32F411CE`
- [x] Write `platformio.ini` (stm32cube, pinned, -Ofast, custom ldscript)
- [x] Write `migration_todo.md` + `migration_checklist.md`
- [x] Write `CLAUDE.md`
- [x] Write `README.md`
- [x] Expand `.gitignore`
- [x] Initial commit

## Phase 1 — Vendor middleware
- [x] Create `lib/FreeRTOS/` (sources + headers from main)
- [x] `lib/FreeRTOS/library.json` (libArchive=false)
- [x] Create `lib/Modbus/` (Modbus.c, UARTCallback.c, headers)
- [x] `lib/Modbus/library.json` (libArchive=false)

## Phase 2 — Port app + Cube glue
- [x] Copy app IP (Ramps, Scales) → src/ + include/
- [x] Copy Cube glue (main, gpio, tim, usart, it, hal_msp, timebase, freertos) → src/
- [x] Copy config headers (FreeRTOSConfig, hal_conf, main.h, …) → include/
- [x] Copy newlib stubs (syscalls, sysmem) → src/
- [x] Copy linker script → root; `.ioc` + RAM.ld → docs/

## Phase 3 — Green build
- [x] `pio run` compiles
- [x] Resolve float ABI (hard-float extra script); no dup-symbol issues
- [x] Verify ELF: hard-float ABI + weak overrides (UART cb, RTOS handlers) linked
- [x] Compare flash/RAM size vs original (31 KB vs 33 KB flash — OK)
- [x] Commit working build

## Phase 4 — Hardware verification (handoff)
- [ ] Flash via ST-Link
- [ ] USR_LED + Modbus slave (addr 17 @115200)
- [ ] Encoders, jog, indexing, sync motion, ENA timing
- [ ] A/B vs shipping firmware

## Phase 5 — CI + remote (optional)
- [ ] PlatformIO GitHub Actions (build + release)
- [ ] Push to `drdro-firmware` remote
