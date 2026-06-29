# drDRO Dual-Bank + Settings + Bootloader CLI — Todo

Phased tracker. Design/decisions: `dualbank_design.md`. Builds on `bootloader_todo.md`
(B0–B5 IAP groundwork, HW-verified). Sequence: **dual-bank first** (DC3).

## Phase D1 — Dual-bank (copy-on-activate) + boot selection
### D1.1 — Flash-map foundation (relocate app to Exec)
- [ ] `shared/Bootloader.h`: new map — BL(s0), SETTINGS(s1), EXEC(s5 @0x08020000),
      BANK0(s6 @0x08040000), BANK1(s7 @0x08060000); `APP_EXEC_BASE`, bank bases/sizes,
      `boot_mode` enum. Keep `BOOT_FLAG`/`BOOT_MAGIC`.
- [ ] App linker `STM32F411CEUX_FLASH_APP.ld`: `ORIGIN=0x08020000, LENGTH=128K`.
- [ ] App `main.c`: `SCB->VTOR = APP_EXEC_BASE`.
- [ ] Bootloader `main.c`/`flash.c`: jump + validity target `APP_EXEC_BASE`.
- [ ] Build both green; bench-verify bootloader→app@Exec still boots + protocol live.

### D1.2 — Settings sector — code done 2026-06-29
- [x] `shared/Settings.h`: `settings_t` (magic, version, boot_mode, active_bank,
      loaded_bank, **+ app payload: scales ratios/sync, servo cfg**) + CRC32 + validate +
      defaults + `settings_load()` (header-only, pure).
- [x] Settings read/write + CRC32; defaults on invalid; single-sector. Bootloader writes
      via `flash_write_settings()`; app via `SettingsStore.c` (IRQ-masked erase+program).
- [x] Flash driver generalized: `flash_program_begin/write/end` (region+sector),
      `flash_erase_sector`, `flash_copy_region`, `flash_write_settings`. Refuses sector 0.
- [ ] (D2) ping-pong across two sectors for power-fail safety; bank CRC32 stored+checked.

### D1.3 — Boot selection + copy-on-activate — code done 2026-06-29
- [x] Bootloader boot logic (`bl_boot_app`): RAM flag / boot_mode / validity → copy active
      bank→Exec if stale (`loaded_bank != active_bank` or Exec invalid) → verify → jump.
- [x] Image validity = vector sanity; stored banks hold Exec-linked images so PC is
      checked against the Exec range for both Exec and bank vectors.
- [x] YMODEM receive retargeted to a chosen **bank** (`ymodem_receive(base, sector, ...)`).
- [x] **Bench-verified 2026-06-29:** factory image (BL + app@Exec) boots app directly; seeded
      bank + `bank N` + reset → copy-on-activate runs the selected bank; full `dro_update.py`
      cycle (update→CLI→`info`→YMODEM flash→`bank N`→`boot`→copy→app live) succeeded.

## Phase D2 — Settings payload + app persistence
- [x] Extend `settings_t` with app config (done in D1.2): scales ratios/sync, servo cfg, mode.
- [x] App loads settings at boot into `rampsHandler` (`SettingsApply`); `save`/`load` commands.
- [ ] **Relocate motion ISR to RAM (DC2) — needs bench validation, do NOT ship blind.**
      Full requirement (single-bank flash stalls *all* flash fetches during erase/program):
      (1) `SynchroRefreshTimerIsr` + callees (`deltaPositionAndError`, `updateIndexing/Jog`) →
      `.RamFunc` (the ld already places `.RamFunc` in RAM; callees are pure math, no libm/HAL);
      (2) replace `HAL_GPIO_WritePin` in the ISR with direct `BSRR` writes (RAM-safe);
      (3) rewrite `TIM1_BRK_TIM9_IRQHandler` to clear the TIM9 flag via registers (drop the
      flash `HAL_TIM_IRQHandler` calls) and live in `.RamFunc`;
      (4) **relocate the vector table to RAM and point VTOR at it** (else interrupt entry stalls
      on the flash vector fetch). Then lift the motion-stopped guard on `save`. Validate on a
      scope: steps must keep coming during a 16 KB settings erase. **Until then the guard stays.**
      - [x] Prereq done 2026-06-29: ISR GPIO writes now use fast inline `gpioSet`/`gpioReset`
            (single `BSRR` store, no HAL call — verified inlined; BSRR idiom from `multi_servo`).
            ISR callees confirmed pure math (no libm/HAL). Remaining: register-level IRQ flag
            clear + RAM vector table.
- [ ] Bank CRC32: store image region CRC32 in `settings.bank_crc[n]` on flash; verify on boot
      (beyond the vector-sanity check). CLI `crc <n>` already reports the region CRC.
- [ ] (Hardening) settings ping-pong across two sectors for power-fail safety.

## Phase D3 — CLIs — code done 2026-06-29
- [x] **DC5: bootloader CLI is self-contained** (duplicates the ~80-line line-protocol core)
      rather than sharing app code — keeps the bootloader isolated/robust; identical wire
      format so one host client drives both. (Shared-core extraction dropped.)
- [x] Native tests green after app changes (26 cases, was 21).
- [x] Bootloader CLI (`bl_cli.c`): `version help info bank boot.mode flash erase crc copy boot reset`
      + greeting `bootloader=ready`. Polled line loop.
- [x] App CLI: `bank` get/set (active bank, persisted), `save`, `load`, `reset` (+ existing `update`).
      Flash-write commands gated on motion-stopped.
- [ ] (later) `rollback` convenience alias on the app (today: `bank <other>` + `reset`).

## Phase D4 — LED + updater + CI — done 2026-06-29
- [x] USR_LED (PB12): **repeating** heartbeat — 2 blinks = bootloader, 1 = app, ~1/s
      (BlinkCode.h). Bootloader: SysTick FSM in the CLI poll loop; app: FreeRTOS led task.
      **Active-low** — on = drive LOW (`gpioReset`), off = HIGH (`gpioSet`).
- [x] Updater `tools/dro_update.py`: `update`→CLI→`info`(pick inactive)→`flash <n>` YMODEM→
      `bank <n>`→`boot`; post-boot version verify retried (first post-jump byte can glitch).
- [x] CI + release: build both, `tools/make_factory.py` merges BL + app@Exec → `factory.hex`
      (bootloader emits `.hex` via `support/make_hex.py`); published as an artifact/release asset.
- [x] Bench-verified the whole D1–D3 cycle end-to-end (see D1.3).
