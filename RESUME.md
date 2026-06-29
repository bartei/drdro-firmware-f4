# RESUME — drDRO firmware (read this first when picking up)

Last updated: 2026-06-29. Branch **`feat/iap-bootloader`** (NOT yet on `main`), last commit `3027be0`.
This is the portable handoff (the local `~/.claude` memory does NOT travel to a
remote machine — this file does). Detail lives in the linked docs.

**Resuming:** `git checkout feat/iap-bootloader`. Repo is now two projects — `app/` + `bootloader/`
(+ `shared/`); build with `pio run -d app` / `pio run -d bootloader`. Protocol shipped + verified;
bootloader B0–B2 done & HW-verified. **Next = B3** (flash driver + YMODEM receiver): see
`bootloader_todo.md`. Branch not pushed to `main` (would release an incomplete IAP setup).

## TL;DR
PlatformIO firmware for the drDRO rotary controller (STM32F411CEU6). Migrated off
STM32CubeIDE to PlatformIO + `stm32cube` framework. **Modbus has been fully replaced
by a custom CLI-friendly line protocol** (`app/src/Protocol.c`), which is live. Build is
green. Next up: **verify the protocol on the test bench**, then build the firmware-
update bootloader.

## Build / flash / test
Two sibling PlatformIO projects: `app/` (firmware) and `bootloader/` (IAP). `shared/` holds the
contract header. Pass `-d <dir>` (or `cd` in). On this host `pio` is not on PATH — see the local
`~/.claude` memory; wrap commands in `nix-shell -p platformio --run '...'`.
```sh
pio run -d app                 # build app (env: drdro_f411ce) → flash ~44 KB / RAM ~23 KB
pio run -d app -t upload       # flash app via ST-Link (lives at 0x08004000)
pio run -d bootloader          # build bootloader (~4.3 KB, sector 0 @ 0x08000000)
pio run -d bootloader -t upload
pio test -d app -e native      # host-side protocol unit tests (21 cases, no hardware)
```
**Full board = bootloader @ 0x08000000 + app @ 0x08004000.** Flash both; the bootloader jumps to
the app. **Verify after any flash:** `mdw 0x08000000` (openocd) must show the bootloader vectors,
not `464c457f` ("\x7fELF") — see the B2 gotcha in `bootloader_todo.md`. Linker-only edits need `-t clean`.
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
   - **In progress (2026-06-29):** protocol confirmed live on the bench — `version`,
     `get`, `sta`, `settings` all return correct data at 115200 (after the HSE_VALUE fix
     above). Probe harness: `scratchpad/probe.py` (pyserial), adapter on `/dev/ttyACM2`.
   - **Open bug — RS485 turnaround:** the first ~6–7 bytes of each response are corrupted
     (the leading *key* token; value+crc arrive clean), consistent with half-duplex RX→TX
     turnaround on the auto-direction transceiver (board and/or the PC USB-RS485 adapter).
     Fixed: 2 ms pre-TX settle delay in `respBegin()` (`app/src/Protocol.c`). Verified clean.
   - Watch: a **self-echo guard** drops RX during TX. If responses look doubled or commands
     get eaten on the bench, that's the first knob (`sTxActive` in `app/src/Protocol.c`).
2. **Firmware-update bootloader** — custom IAP + YMODEM @115200 (design `protocol_design.md` Part B;
   plan/progress `bootloader_todo.md`). **B0–B2 done + HW-verified** (app relocation, `update` command,
   bootloader boot/jump). **Next: B3** — flash erase/write driver + YMODEM receiver in the bootloader,
   then a host updater. B4 = combined factory image + CI. (Branch: `feat/iap-bootloader`.)
3. Optional: settings persistence to flash. (CI + remote: done.)

## Repo map (two projects + shared/; docs at root)
**`app/`** — the application PlatformIO project:
- `src/Protocol.c`, `include/Protocol.h` — the line protocol (RX ISR + service task + registry; `update` cmd).
- `src/Ramps.c`, `include/Ramps.h` — motion core (TIM9 ISR, ramps, scales). `rampsHandler_t.shared` is the protocol's register image.
- `src/Scales.c` — encoder timer init. `src/main.c` — VTOR relocation + clock + peripheral init + entry.
- `src/{gpio,tim,usart,stm32f4xx_it,stm32f4xx_hal_msp,stm32f4xx_hal_timebase_tim}.c` — peripheral glue (hand-maintained; **CubeMX is never regenerated**).
- `lib/FreeRTOS/` — vendored kernel + CMSIS-RTOS v2 (`libArchive:false`, see gotchas).
- `support/stm32_hardfloat.py`, `support/fw_version.py`, `support/make_hex.py` — build extra scripts (see gotchas).
- `STM32F411CEUX_FLASH_APP.ld` (app @ 0x08004000) + `STM32F411CEUX_FLASH.ld` (original 0-based, reference).
- `test/` — native unit tests + `test/mocks/`.

**`bootloader/`** — IAP bootloader project: `src/bl_main.c`, `STM32F411CEUX_FLASH_BOOT.ld` (sector 0 @ 0x08000000).
**`shared/Bootloader.h`** — app↔bootloader contract (flash layout, handshake word); both projects `-I ../shared`.
`docs/` — frozen `.ioc` reference + RAM ld.
- Old project for reference: `../rotary-controller-f4` (branch `main`).

## Gotchas (don't undo these — each was a real fix). Paths are in `app/` unless noted.
- **`-D HSE_VALUE=8000000`** (in `app/platformio.ini` build_flags; **also `bootloader/platformio.ini`**):
  the board crystal is 8 MHz, but the stm32cube framework's `system_stm32f4xx.c`/HAL headers default
  `HSE_VALUE` to **25 MHz**. Our `app/include/stm32f4xx_hal_conf.h` has 8 MHz but it's `#if !defined`-
  guarded and loses when the framework compiles its own system file. Result without the flag:
  `SystemCoreClock` = 312.5 MHz (chip still runs at 100 MHz via literal PLL regs, but
  HAL computes `USART1_BRR` from the wrong clock → real baud ≈ 36.8 kbaud → all serial
  garbage). Verified fixed: `SystemCoreClock=0x05F5E100` (100 MHz), `BRR=0x364` (115200).
- **App-only flash offset**: `app/platformio.ini` has `-Wl,-z,max-page-size=0x4000` — the app loads at
  0x08004000 (not 64 KB-aligned), so ld's default would bake the ELF header into the first LOAD segment
  and flashing the `.elf` would clobber the bootloader (B2 gotcha in `bootloader_todo.md`).
- **Hard-float**: `app/support/stm32_hardfloat.py` applies `-mfloat-abi=hard -mfpu=fpv4-sp-d16` to
  compile AND link; the board/framework default is soft-float, which breaks FreeRTOS `port.c`.
  (The bootloader has no FreeRTOS → soft-float, no hard-float script.)
- **`libArchive:false`** on `app/lib/FreeRTOS`: otherwise weak-symbol overrides
  (RTOS handlers / UART callbacks) get dropped from the archive.
- **`-u _printf_float`** (in `app/platformio.ini`): needed for `%g` on the float vars under nano.specs.
- **Don't add** `Drivers/`, `system_stm32f4xx.c`, or a startup `.s` — the framework provides them.
- **`FreeRTOSConfig.h` / `stm32f4xx_hal_conf.h` live in `app/include/`** (globally visible to vendored libs).
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
