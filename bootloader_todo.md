# drDRO Bootloader (firmware update over RS485) — Todo

Phased tracker for the custom IAP bootloader. **Design/decisions: `protocol_design.md`
Part B** (D4: custom IAP + YMODEM, 115200 max; never erase sector 0). Protocol has
shipped (`protocol_todo.md` Phase 5 done); this is the next milestone.

## Flash layout (F411CE, 512 KB — sectors 4×16K / 1×64K / 3×128K)
| Region | Sector(s) | Address | Size | Notes |
|---|---|---|---|---|
| Bootloader | 0 | `0x08000000` | 16 KB | never erased by an update; recoverable |
| Application | 1 → 7 | `0x08004000` | ~496 KB | VTOR relocated here |

## Decisions (CONFIRMED 2026-06-29)
- **DB1 — Update trigger flag:** **no-init RAM word** at a fixed address; `update` writes a magic
  value then resets. Survives the warm reset; no RTC/PWR/BKP clocks in the bootloader.
- **DB2 — App-valid check:** **vector-table sanity** first (initial SP in RAM range + reset vector
  in app flash). Add a trailing CRC32 in a later phase once the image carries one.
- **DB3 — Build structure:** **standalone project under `bootloader/`** (own `platformio.ini`,
  `src/`, ldscript). *(Revised 2026-06-29 from "second env": the env approach needed a
  non-standard `src/bootloader/` + `build_src_filter` and was confusing. A separate project
  keeps the source trees unambiguous — root = app, `bootloader/` = bootloader. Shared contract
  stays single-source via the top-level `shared/Bootloader.h` (both projects `-I ../shared`). As a clean `-Os` project the bootloader is
  ~4.3 KB, so it fits the 16 KB sector easily — no 32 KB expansion needed.)*
- **DB4 — Bootloader clock:** **reuse the app's `SystemClock_Config`** (100 MHz PLL) so the USART1
  baud divisor matches the app exactly (115200).

## Phase B0 — App relocation to 0x08004000 — code done 2026-06-29
- [x] App linker script `STM32F411CEUX_FLASH_APP.ld`: FLASH ORIGIN `0x08004000`, LENGTH `496K`
      (`platformio.ini` now points the app env at it; original `.ld` kept for reference)
- [x] `SCB->VTOR = FLASH_BASE | 0x4000` set first in `main()` (before `HAL_Init`)
- [x] Sanity (objdump): `.isr_vector`@`0x08004000`, MSP=`0x20020000`, reset=`0x0800e331`, VTOR store in `main`
- [ ] Flash via ST-Link + confirm protocol still works — **deferred to after B2** (a bare relocated
      app won't boot without the bootloader at `0x08000000`; board left on the working 0x0… image)

> ⚠️ CI/release now builds the app at `0x08004000` (non-bootable standalone). Don't ship a release
> until B2+B4 land (bootloader + combined factory image). Hold pushes to `main` meanwhile.

## Phase B1 — `update` command + trigger flag (DB1) — code done 2026-06-29
- [x] `include/Bootloader.h`: shared contract (APP_BASE, `BOOT_FLAG`@0x2001FFF0, `BOOT_MAGIC`)
- [x] App ldscript reserves top 16 B of RAM (`LENGTH = 128K-16`); verified `_estack=0x2001FFF0`
- [x] `update` command: arms `BOOT_FLAG=BOOT_MAGIC`, acks `update=ready`, then `NVIC_SystemReset()`
      after the ack drains (in `ProtocolService`, `osDelay(5)`)
- [x] Native build: added `NVIC_SystemReset` host stub; 21/21 tests still pass
- [ ] Hardware reboot check — deferred to after B2 (needs bootloader at 0x08000000 to boot)

## Phase B2 — Bootloader skeleton (DB3, DB4) — done + HW-verified 2026-06-29
- [x] `bootloader/` standalone project: `platformio.ini` + `STM32F411CEUX_FLASH_BOOT.ld`
      (sector 0, 16 KB) + `src/main.c`. Framework startup/system. -Os, soft-float.
- [x] Clock (reused 8 MHz→100 MHz PLL) + USART1 init (PA10/PA15 AF7, 115200, polled)
- [x] Jump-to-app: consume flag (DB1) + vector sanity (DB2) → set MSP/VTOR → branch
- [x] Size: 4304 B of 16 KB (≈12 KB free for B3)
- [x] **HW-verified:** bootloader→app boot chain (protocol live), `update`→reboot→bootloader
      banner, reset→app recovery (flag consumed). Board left on the new bootloader+app layout.

### B2 gotcha (fixed) — ELF segment clobbered sector 0
The app loads at `0x08004000`, not 64 KB-aligned, so GNU ld's default `max-page-size`
(0x10000) rounded the first PT_LOAD down to `0x08000000` and baked the ELF header into the
loadable segment → `pio run -t upload` (which flashes the `.elf`) wrote the ELF header over
the bootloader. Fix: `-Wl,-z,max-page-size=0x4000` in the app `build_flags` (0x08004000 is
16 KB-aligned → clean segment). Verify after any flash: `mdw 0x08000000` should be the
bootloader's vector table, not `464c457f` ("\x7fELF"). Linker-only changes need `pio run -t clean`.

## Phase B3 — Flash programming + YMODEM receive — code done 2026-06-29
- [x] Flash driver (`src/flash.c`): erase app sectors 1–7, word program, read-back verify
      (HAL_FLASH). Lazy per-sector erase in ascending order; **never** touches sector 0.
- [x] Minimal YMODEM receiver (`src/ymodem.c`): SOH/STX, CRC-16, ACK/NAK, EOT handshake,
      block-0 filename/size header, trailing null header — self-contained, no libs.
- [x] `update mode` wires it: `ymodem_receive()` streams to flash → reset. Flag is already
      consumed at boot, so a valid new app boots normally on the reset.
- [x] Failure handling: lazy erase wipes the vector-table sector first, so a partial/aborted
      transfer leaves the app invalid → `app_valid()` fails → bootloader re-enters update mode.
      Sector 0 (bootloader) is never erased, so it always stays recoverable.
- [x] Size: ~6.6 KB of 16 KB (was 4304; ~9 KB free). Builds green (`pio run -d bootloader`).
- [x] **HW-verified 2026-06-29** end-to-end with `tools/dro_update.py`: 44640-byte image sent over
      YMODEM, flash readback byte-for-byte matched the `.bin`, app booted and answered `version`.
- [x] **BOOT0 fix (critical):** post-update handoff now **jumps to the app** instead of
      `NVIC_SystemReset()` — this board's BOOT0 floats, so a system reset intermittently boots the
      **ST system-memory ROM** (PC=0x1FFFxxxx, lone `\x00` on serial) instead of flash. A jump never
      re-samples BOOT0. On a failed/aborted transfer the bootloader now holds in update mode
      ("power-cycle to retry") rather than resetting into the ROM.
- Note: also renamed `src/bl_main.c` → `src/main.c` (own folder makes the prefix redundant).
- Note: bootloader TX has no RS485 settle delay yet, so the first response byte can be a `\xff`
  turnaround glitch (seen on the B2 banner). YMODEM keys on control chars (C/ACK/NAK) and is
  tolerant, but the bootloader is the YMODEM *receiver* (mostly RX) so it's likely a non-issue;
  add a settle delay before any bootloader TX if the host's YMODEM gets confused.

## Phase B4 — Host tooling + packaging
- [x] Host updater `tools/dro_update.py` (HW-verified 2026-06-29): sends `update`, waits for the
      bootloader 'C' handshake, then YMODEM-sends the `.bin`. Self-contained CRC-16 sender (no
      lrzsz), 1024-byte STX blocks, glitch-tolerant ACK wait for the RS485 turnaround. pyserial
      only. Drove a full `update` → reset → YMODEM → reflash → app-live cycle on the bench.
- [ ] Combined factory image: bootloader + app into one `.hex` (srecord) for first-time flashing
- [ ] CI: build both env artifacts; keep release publishing app + combined image

## Phase B5 — Hardware verification
- [x] End-to-end update over RS485 from a known-good app to a new app — **done 2026-06-29**
      (`tools/dro_update.py`, 44640-byte image, readback verified, app answered `version` after).
- [ ] Bricked-app recovery: corrupt app → bootloader still enters update mode (DB2 vector check).
- [ ] **Follow-up — robust `update` trigger:** the app still enters the bootloader via
      `NVIC_SystemReset()`, which re-samples the floating BOOT0 and can land in the ST ROM (it
      worked in testing but isn't guaranteed). Make the app **jump** to 0x08000000 instead
      (RAM/`BOOT_FLAG` survive a jump), after tearing down FreeRTOS/IRQs/SysTick. Real HW fix: tie
      BOOT0 to GND. See the BOOT0 note in Phase B3.
- [ ] Optional: boot-time "knock" window to force update even if the app never runs.
