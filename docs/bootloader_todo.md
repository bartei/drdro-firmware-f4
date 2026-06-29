# drDRO IAP Bootloader — Todo

Terse phased tracker for the IAP groundwork (B0–B5). **Design & decisions:**
`protocol_design.md` Part B (rationale) + `dualbank_design.md` (current architecture, flash map,
DB1–DB4 mechanics, gotchas). The single-app IAP below evolved into the dual-bank system — current
bootloader work is tracked in `dualbank_todo.md`. Kept here as the build-up record. **All done.**

## B0 — App relocated off sector 0 — done
- [x] App linker + `SCB->VTOR` relocated (was `0x08004000`; now Exec `0x08020000` — see dual-bank).

## B1 — `update` command + trigger flag — done
- [x] `shared/Bootloader.h` contract (`BOOT_FLAG`@`0x2001FFF0`, `BOOT_MAGIC`); top 16 B RAM reserved.
- [x] `update` arms the flag, then hands off to the bootloader.

## B2 — Bootloader skeleton — done + HW-verified
- [x] Standalone `bootloader/` project (own ldscript @ sector 0, framework startup, `-Os`, soft-float).
- [x] Clock + polled USART1 + jump-to-app (consume flag + vector sanity → set MSP/VTOR → branch).

## B3 — Flash programming + YMODEM receive — done + HW-verified
- [x] Flash driver: erase / word-program / read-back verify; never erases sector 0.
- [x] Self-contained YMODEM receiver (SOH/STX, CRC-16, EOT handshake, no libs).
- [x] Receive → flash → boot; a partial transfer leaves an invalid image → re-enters update mode.

## B4 — Host tooling + packaging — done
- [x] `tools/dro_update.py` host updater (drives the full cycle).
- [x] `tools/make_factory.py` merges bootloader + app@Exec → `factory.hex`.
- [x] CI/release build both projects + the factory image (`make_hex.py` emits the bootloader `.hex`).

## B5 — Hardware verification — done (firmware)
- [x] End-to-end RS485 update from a known-good app to a new app (bench).
- [x] Bricked/partial app → bootloader stays in control (vector + CRC trust check).
- [x] Robust `update` trigger: app JUMPS to the bootloader (no reset) — BOOT0-safe (`EnterBootloader`).
- [ ] Optional: boot-time "knock" window to force update if the app never runs (not implemented).
- [ ] Hardware: tie BOOT0 to GND on the next PCB run — `HARDWARE.md` HW-1 (board fix, not firmware).
