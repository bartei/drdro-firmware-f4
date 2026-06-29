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
  stays single-source via `../include/Bootloader.h`. As a clean `-Os` project the bootloader is
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
      (sector 0, 16 KB) + `src/bl_main.c`. Framework startup/system. -Os, soft-float.
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

## Phase B3 — Flash programming + YMODEM receive
- [ ] Flash driver: erase app sectors 1–7, program word/byte, read-back verify (HAL_FLASH)
- [ ] Minimal YMODEM receiver (SOH/STX, CRC-16, ACK/NAK, filename/size header) — self-contained, no libs
- [ ] Receive image → erase → write → verify → clear flag → reset into the new app
- [ ] Failure handling: abort leaves app erased but bootloader intact (recoverable retry)
- Note: bootloader TX has no RS485 settle delay yet, so the first response byte can be a `\xff`
  turnaround glitch (seen on the B2 banner). YMODEM keys on control chars (C/ACK/NAK) and is
  tolerant, but the bootloader is the YMODEM *receiver* (mostly RX) so it's likely a non-issue;
  add a settle delay before any bootloader TX if the host's YMODEM gets confused.

## Phase B4 — Host tooling + packaging
- [ ] Host updater: send `update`, wait for reset, YMODEM-send the `.bin` (lrzsz `sb` or pyserial)
- [ ] Combined factory image: bootloader + app into one `.hex` (srecord) for first-time flashing
- [ ] CI: build both env artifacts; keep release publishing app + combined image

## Phase B5 — Hardware verification
- [ ] End-to-end update over RS485 from a known-good app to a new app
- [ ] Bricked-app recovery: corrupt app → bootloader still enters update mode
- [ ] Optional: boot-time "knock" window to force update even if the app never runs
