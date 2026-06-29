# RESUME — drDRO firmware (read this first when picking up)

Last updated: 2026-06-28. Branch `main`, last commit `a0a6d47`.
This is the portable handoff (the local `~/.claude` memory does NOT travel to a
remote machine — this file does). Detail lives in the linked docs.

## TL;DR
PlatformIO firmware for the drDRO rotary controller (STM32F411CEU6). Migrated off
STM32CubeIDE to PlatformIO + `stm32cube` framework. **Modbus has been fully replaced
by a custom CLI-friendly line protocol** (`src/Protocol.c`), which is live. Build is
green. Next up: **verify the protocol on the test bench**, then build the firmware-
update bootloader.

## Build / flash / test
```sh
pio run                 # build  (env: drdro_f411ce)  → flash ~44 KB / RAM ~23 KB
pio run -t upload       # flash via ST-Link
pio test -e native      # host-side protocol unit tests (21 cases, no hardware)
pio run -t compiledb    # regenerate compile_commands.json for clangd (gitignored)
```
CI runs build + native tests on every push/PR (`.github/workflows/ci.yml`); pushes
to `main` also semantic-version, tag, and publish a release with firmware artifacts
(`release.yml`). Remote: `git@github.com:bartei/drdro-firmware-f4.git`.
Terminal test (the whole point of the new protocol) — RS485 adapter at **115200 8N1**,
type commands (no checksum needed in CLI mode; responses end with a blank line):
```
version
help
settings
sta            # scales.pos=...  scales.speed=...  crc=..  (blank line)
               # press Enter alone to repeat the previous command
get servo.max
set servo.max 1000
set scales.sync 0 1     # array element: <name> <idx> <value>
```
Protocol details/grammar: `protocol_design.md`. Variable names: §A.4 there.

## Status — what's done
- **PlatformIO port** (was STM32CubeIDE): green, faithful to `rotary-controller-f4`@`main` (v2.0.1).
- **Protocol Phases 1–4 done** (see `protocol_todo.md`): core+framing+XOR-8 checksum,
  array-aware variable registry (`set`/`get`/`settings`), `sta`, and the Modbus→protocol
  switchover. Modbus is gone.
- **CubeMX boilerplate stripped** (~830 lines): USER CODE markers, ST `@attention`
  banners, dashed section dividers, and the redundant `defaultTask`/`freertos.c`.

## Status — what's next
1. **Phase 5 — hardware verification** (`protocol_todo.md`): exercise every command over
   RS485, the empty-line repeat, and error/checksum paths; A/B against old Modbus behavior.
   - Watch: a **self-echo guard** drops RX during TX. If responses look doubled or commands
     get eaten on the bench, that's the first knob (`sTxActive` in `src/Protocol.c`).
2. **Firmware-update bootloader** — custom IAP + YMODEM @115200 (design in `protocol_design.md`
   Part B). Not started; give it its own `bootloader_todo.md` when starting.
3. Optional: PlatformIO CI (GitHub Actions) + push to a remote; settings persistence to flash.

## Repo map
- `src/Protocol.c`, `include/Protocol.h` — the line protocol (RX ISR + service task + registry).
- `src/Ramps.c`, `include/Ramps.h` — motion core (TIM9 ISR, ramps, scales). `rampsHandler_t.shared` is the protocol's register image.
- `src/Scales.c` — encoder timer init. `src/main.c` — clock + peripheral init + entry.
- `src/{gpio,tim,usart,stm32f4xx_it,stm32f4xx_hal_msp,stm32f4xx_hal_timebase_tim}.c` — peripheral glue (hand-maintained; **CubeMX is never regenerated**).
- `lib/FreeRTOS/` — vendored kernel + CMSIS-RTOS v2 (`libArchive:false`, see gotchas).
- `support/stm32_hardfloat.py`, `support/fw_version.py` — build extra scripts (see gotchas).
- `STM32F411CEUX_FLASH.ld` — custom linker. `docs/` — frozen `.ioc` reference + RAM ld.
- Old project for reference: `../rotary-controller-f4` (branch `main`).

## Gotchas (don't undo these — each was a real fix)
- **Hard-float**: `support/stm32_hardfloat.py` applies `-mfloat-abi=hard -mfpu=fpv4-sp-d16`
  to compile AND link; the board/framework default is soft-float, which breaks FreeRTOS `port.c`.
- **`libArchive:false`** on `lib/FreeRTOS` (and was on Modbus): otherwise weak-symbol overrides
  (RTOS handlers / UART callbacks) get dropped from the archive.
- **`-u _printf_float`** (in `platformio.ini`): needed for `%g` on the float vars under nano.specs.
- **Don't add** `Drivers/`, `system_stm32f4xx.c`, or a startup `.s` — the framework provides them.
- **`FreeRTOSConfig.h` / `stm32f4xx_hal_conf.h` live in `include/`** (globally visible to vendored libs).
- **RS485 is auto-direction** (no DE GPIO). **Baud is fixed at 115200** (circuit limit).
- Platform pinned `ststm32@~19.4.0`.

## Conventions (also in `~/.claude/CLAUDE.md`)
- Per feature: a detailed reference doc + a terse phased `*_todo.md` checkbox tracker. Tick as you go.
- Commits/PRs: **no AI/Claude attribution** trailers or footers.

## Doc index
- `protocol_design.md` — protocol + bootloader design, confirmed decisions D1–D9.
- `protocol_todo.md` — phased progress (Phases 1–4 ticked; Phase 5 pending).
- `migration_todo.md` + `migration_checklist.md` — the Cube→PlatformIO migration record.
- `CLAUDE.md` — project overview + build/architecture for Claude Code.
