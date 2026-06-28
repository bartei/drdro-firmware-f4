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
- [ ] Create `lib/FreeRTOS/` (sources + headers from main)
- [ ] `lib/FreeRTOS/library.json`
- [ ] Create `lib/Modbus/` (Modbus.c, UARTCallback.c, headers)
- [ ] `lib/Modbus/library.json`

## Phase 2 — Port app + Cube glue
- [ ] Copy app IP (Ramps, Scales) → src/ + include/
- [ ] Copy Cube glue (main, gpio, tim, usart, it, hal_msp, timebase, freertos) → src/
- [ ] Copy config headers (FreeRTOSConfig, hal_conf, main.h, …) → include/
- [ ] Copy newlib stubs (syscalls, sysmem) → src/
- [ ] Copy linker script → root; `.ioc` + RAM.ld → docs/

## Phase 3 — Green build
- [ ] `pio run` compiles
- [ ] Resolve duplicate symbols / include paths / float ABI
- [ ] Compare flash/RAM size vs old `.map`
- [ ] Commit working build

## Phase 4 — Hardware verification (handoff)
- [ ] Flash via ST-Link
- [ ] USR_LED + Modbus slave (addr 17 @115200)
- [ ] Encoders, jog, indexing, sync motion, ENA timing
- [ ] A/B vs shipping firmware

## Phase 5 — CI + remote (optional)
- [ ] PlatformIO GitHub Actions (build + release)
- [ ] Push to `drdro-firmware` remote
