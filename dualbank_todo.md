# drDRO Dual-Bank + Settings + Bootloader CLI — Todo

**STATUS: COMPLETE & HW-verified (2026-06-29, shipped ≥ v0.3.0).** All phases D1–D4 done.
Phased tracker. Design/decisions: `dualbank_design.md`. Built on `bootloader_todo.md`
(B0–B5 IAP groundwork). Sequence followed: **dual-bank first** (DC3).

## Phase D1 — Dual-bank (copy-on-activate) + boot selection — done 2026-06-29
### D1.1 — Flash-map foundation (relocate app to Exec)
- [x] `shared/Bootloader.h`: new map — BL(s0), SETTINGS(s1, +s2 shadow), EXEC(s5 @0x08020000),
      BANK0(s6 @0x08040000), BANK1(s7 @0x08060000); `APP_EXEC_BASE`, bank bases/sizes,
      `boot_mode` enum. Kept `BOOT_FLAG`/`BOOT_MAGIC`.
- [x] App linker `STM32F411CEUX_FLASH_APP.ld`: `ORIGIN=0x08020000, LENGTH=128K`.
- [x] App `main.c`: `SCB->VTOR = APP_EXEC_BASE` (later → RAM vector table, see D2).
- [x] Bootloader `main.c`/`flash.c`: jump + validity target `APP_EXEC_BASE`.
- [x] Build both green; bench-verified bootloader→app@Exec boots + protocol live.

### D1.2 — Settings sector — code done 2026-06-29
- [x] `shared/Settings.h`: `settings_t` (magic, version, boot_mode, active_bank,
      loaded_bank, **+ app payload: scales ratios/sync, servo cfg**) + CRC32 + validate +
      defaults + `settings_load()` (header-only, pure).
- [x] Settings read/write + CRC32; defaults on invalid. Bootloader writes via
      `flash_write_settings()`; app via `SettingsStore.c`. (Single-sector + IRQ-masked write
      from D1.2 was **superseded in D2** by two-sector ping-pong + RAM-ISR, no masking.)
- [x] Flash driver generalized: `flash_program_begin/write/end` (region+sector),
      `flash_erase_sector`, `flash_copy_region`, `flash_write_settings`. Refuses sector 0.
- [x] Ping-pong across two sectors for power-fail safety + bank CRC32 stored/checked — done in D2.

### D1.3 — Boot selection + copy-on-activate — code done 2026-06-29
- [x] Bootloader boot logic (`bl_boot_app`): RAM flag / boot_mode / validity → copy active
      bank→Exec if stale (`loaded_bank != active_bank` or Exec invalid) → verify → jump.
- [x] Image validity = vector sanity; stored banks hold Exec-linked images so PC is
      checked against the Exec range for both Exec and bank vectors.
- [x] YMODEM receive retargeted to a chosen **bank** (`ymodem_receive(base, sector, ...)`).
- [x] **Bench-verified 2026-06-29:** factory image (BL + app@Exec) boots app directly; seeded
      bank + `bank N` + reset → copy-on-activate runs the selected bank; full `dro_update.py`
      cycle (update→CLI→`info`→YMODEM flash→`bank N`→`boot`→copy→app live) succeeded.

## Phase D2 — Settings payload + app persistence — done 2026-06-29
- [x] Extend `settings_t` with app config: scales ratios/sync, servo cfg, mode.
- [x] App loads settings at boot into `rampsHandler` (`SettingsApply`); `save`/`load` commands.
- [x] **Motion ISR relocated to RAM (DC2)** — `save` now works during motion (guard removed):
      (1) ISR + callees in `.RamFunc` (helpers inline into it at -Ofast); (2) ISR GPIO via inline
      `gpioSet`/`gpioReset` (single `BSRR` store); (3) `TIM1_BRK_TIM9_IRQHandler` rewritten to
      clear `TIM9->SR` UIF via register + run the ISR (no HAL/flash), `.RamFunc`; (4) **vector
      table copied to RAM, VTOR repointed** (`g_ramVectors`). App `save_struct` no longer masks
      IRQs. **Statically verified:** ISR + handler at `0x2000xxxx`, zero `bl` to flash in either
      → interrupt entry + ISR path are flash-free. HW: app boots with RAM vectors; persistence
      confirmed (set→save→reset→value retained). (Scope check of steps-during-erase is the only
      thing not directly observable here, but it's guaranteed by the no-flash-access proof.)
- [x] Bank CRC32: `flash <n>` records the bank region CRC32 in `settings.bank_crc[n]`; boot/copy
      gate on `bl_bank_trusted` (vector sanity + CRC match when recorded). CLI `crc <n>` reports it.
- [x] Settings **ping-pong** across two sectors (A=s1, B=s2) with a `seq` counter: writes go to
      the inactive slot, `settings_load` picks the newest valid → power-fail safe. Pure selection
      (`settings_pick`) host-tested.
- [x] Fixed a boot-select bug: a factory unit (app seeded into Exec, empty banks) dropped to the
      CLI after its first `save` (valid settings + `loaded_bank=0xFF` forced a copy from the empty
      active bank). `need_copy` now also requires the active bank to be trusted.

## Phase D3 — CLIs — code done 2026-06-29
- [x] **DC5: bootloader CLI is self-contained** (duplicates the ~80-line line-protocol core)
      rather than sharing app code — keeps the bootloader isolated/robust; identical wire
      format so one host client drives both. (Shared-core extraction dropped.)
- [x] Native tests green (32 cases, was 21): protocol + settings CRC/validate/defaults/pick + sta servo.
- [x] Bootloader CLI (`bl_cli.c`): `version help info bank boot.mode flash erase crc copy rollback
      boot reset` + greeting `bootloader=ready`. Polled line loop.
- [x] App CLI: `bank` get/set (persisted), `save`, `load`, `rollback`, `reset`, `update`. (Flash
      writes are NOT gated on motion — the RAM-resident ISR keeps stepping during a write, see D2.)
- [x] `rollback` on both CLIs — switch to the other bank and boot it (app jumps to the bootloader).

## Phase D4 — LED + updater + CI — done 2026-06-29
- [x] USR_LED (PB12): **repeating** heartbeat — 2 blinks = bootloader, 1 = app, ~1/s
      (BlinkCode.h). Bootloader: SysTick FSM in the CLI poll loop; app: FreeRTOS led task.
      **Active-low** — on = drive LOW (`gpioReset`), off = HIGH (`gpioSet`).
- [x] Updater `tools/dro_update.py`: `update`→CLI→`info`(pick inactive)→`flash <n>` YMODEM→
      `bank <n>`→`boot`; reads the full greeting frame + retries turnaround-glitched commands.
      Runs **deterministically in one pass** now that app→bootloader is a jump (no reset).
- [x] CI + release: build both, `tools/make_factory.py` merges BL + app@Exec → `factory.hex`
      (bootloader emits `.hex` via `support/make_hex.py`); release assets renamed
      `drdro-{app,bootloader}.* / drdro-factory.hex` (were colliding on `firmware.*`).
- [x] Bench-verified the whole D1–D3 cycle end-to-end (see D1.3), single-pass update post-jump.

## Post-D4 — BOOT0-safe handoff (done) + hardware follow-up
- [x] App→bootloader is a **jump** (`EnterBootloader()`), not `NVIC_SystemReset` — avoids
      re-sampling the floating BOOT0 pin (which boots the ST ROM). Whole cycle is now reset-free.
- [ ] **Hardware (next PCB run):** tie BOOT0 to GND so power-on/NRST boot flash deterministically.
      See `HARDWARE.md` HW-1. (Firmware side complete; this is a board fix, not a firmware item.)
