# drDRO Dual-Bank Update + Settings + Bootloader CLI — Design

Extends the IAP bootloader (`bootloader_todo.md`, `protocol_design.md` Part B) into a
field-update system with two retained app versions, persistent settings, and a CLI in
the bootloader. Confirmed decisions are in **§Decisions**; phased progress in
`dualbank_todo.md`.

## Goals (from the 2026-06-29 design conversation)
1. **Bootloader CLI** — same line protocol as the app (`key=value`, dotted names, optional
   `*HH`, blank-line terminator, `crc=HH`), so the flash process can be driven proactively.
2. **Persistent settings** — a flash region for rarely-changed config (scales ratios, boot
   mode, active bank) that both app and bootloader read; app can write it.
3. **Two app banks** — keep two versions; pick which one runs; rollback from either CLI.
4. **USR_LED codes** — 2 blinks = bootloader mode, 1 = app mode (room for more app codes).

## Decisions (CONFIRMED 2026-06-29)
- **DC1 — Banking = copy-on-activate, single build.** The app is linked **once** at the
  fixed **Exec** region (`0x08020000`). The two banks are *storage*. On boot the bootloader
  copies the active bank into Exec (only if Exec is stale) and jumps. Chosen over
  execute-in-place (which would need two per-bank builds). Cost: a ~2–3 s flash copy on a
  bank switch; benefit: one binary, no per-bank link addresses.
- **DC2 — Step ISR in RAM.** `SynchroRefreshTimerIsr` (TIM9) and everything it touches move
  to `.RamFunc`, so a settings/bank flash write (which stalls the single-bank flash bus)
  never freezes step generation. Settings `save` is then allowed during motion.
- **DC3 — Sequencing = dual-bank first**, then settings payload, then CLIs, then LED + tooling.
  Banking needs persisted `active_bank`/`loaded_bank`, so D1 stands up a *minimal* settings
  sector (boot-control fields only); the app-settings payload lands in D2.
- **DC4 — Boot target:** persistent `boot_mode {app, bootloader}` + `active_bank {0,1}` in
  settings; a one-shot "enter bootloader" still uses the no-init RAM flag (`BOOT_FLAG`) so
  triggering an update costs no flash wear.
- **DC5 — Bootloader CLI is self-contained** (confirmed 2026-06-29). The bootloader duplicates
  the ~80-line line-protocol core (`bl_cli.c`) rather than sharing the app's `Protocol.c`, to
  keep the bootloader isolated/robust and avoid destabilizing the shipped app protocol. The
  **wire format is byte-identical** (key=value, terminating blank line, `crc=HH`, optional
  `*HH`), so a single host client drives both. (Supersedes the earlier "extract shared core".)

## Flash map (F411CE, 512 KB — sectors 4×16K / 1×64K / 3×128K)
| Region | Sector(s) | Address | Size | Notes |
|---|---|---|---|---|
| Bootloader | 0 | `0x08000000` | 16 K | never erased |
| Settings | 1 | `0x08004000` | 16 K | magic+CRC; ping-pong hardening = D2/later |
| *(reserved)* | 2,3,4 | `0x08008000` | 96 K | future / recovery image |
| **Exec** | 5 | `0x08020000` | 128 K | app runs here (single build, VTOR here) |
| **Bank 0** | 6 | `0x08040000` | 128 K | stored version |
| **Bank 1** | 7 | `0x08060000` | 128 K | stored version |

App is ~44 KB, so a 128 KB region is generous. Erase granularity is one sector, so each
region is sector-aligned and independently erasable.

## Boot logic (bootloader `main`)
```
req = BOOT_FLAG; BOOT_FLAG = 0          // consume one-shot flag
s = settings_load()                      // magic+CRC checked
if req == BOOT_MAGIC || s.boot_mode == BOOTLOADER || !s.valid:
    enter CLI / update mode (LED: 2 blinks)
else:
    if s.loaded_bank != s.active_bank || !exec_valid():
        copy bank[s.active_bank] -> Exec ; verify ; s.loaded_bank = s.active_bank ; save
    if exec_valid(): VTOR=Exec; jump            // (LED handled by app: 1 blink)
    else: enter CLI / update mode
```
`exec_valid()` / bank-valid = vector-table sanity (DB2) now, trailing CRC32 later.

## Settings (shared/Settings.h — read by both, written by both)
```c
typedef struct {
  uint32_t magic;          // SETTINGS_MAGIC
  uint16_t version;        // schema version
  uint16_t boot_mode;      // 0=app, 1=bootloader
  uint8_t  active_bank;    // 0|1 — which stored bank should run
  uint8_t  loaded_bank;    // 0|1|0xFF — which bank is currently copied into Exec
  uint16_t _pad;
  uint32_t bank_crc[2];    // CRC32 of each bank image (0 = unknown)  [D2+]
  /* --- app payload (D2): scales ratios, servo cfg, ... --- */
  uint32_t crc;            // CRC32 of all preceding bytes
} settings_t;
```
Both sides read-modify-write the whole struct so the other's fields are preserved.

## Bootloader CLI command set (D3 sketch)
`version` · `help` · `bank.info` (banks, sizes, validity, loaded/active) · `boot.mode <app|bl>`
· `boot.bank <0|1>` · `erase <bank>` · `flash <bank>` (→ YMODEM receive into that bank) ·
`crc <bank>` · `copy` (active→exec) · `boot` (jump to exec) · `reset`. Shares the app's
line-protocol core (extracted to `shared/`), each side registering its own command table + I/O.

## App CLI additions (D3)
`boot.bank` get/set + `rollback` (flip active_bank) + `save` (persist settings) + existing
`update` (set RAM flag, reset into bootloader).

## Build changes
- App linker `ORIGIN = 0x08020000, LENGTH = 128K`; `SCB->VTOR` = exec base.
- One app `.bin`; updater sends it into a chosen bank, sets `active_bank`.
- CI: bootloader + app(exec) + combined factory `.hex` (bootloader + exec + seed a bank).

## LED (PB12)
Bootloader blinks **2×** at entry; app blinks **1×** at entry. Reserve longer blink counts
for future app error codes (not implemented yet).
