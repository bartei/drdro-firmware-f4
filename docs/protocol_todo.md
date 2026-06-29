# drDRO Line Protocol — Todo

Phased progress tracker. Detail in `protocol_design.md`. Bootloader is a separate
doc (`bootloader_todo.md`), after this.

## Phase 1 — Protocol core (compiles alongside Modbus; callback-free)
- [x] `include/Protocol.h` + `src/Protocol.c` skeleton
- [x] Line buffer + tokenizer
- [x] Command table + dispatcher
- [x] Response builder (empty-line framing, `error=`)
- [x] TX helper (blocking `HAL_UART_Transmit`; auto-direction)
- [x] Checksum: validate optional `*HH` on requests; append `crc=HH` to responses
- [x] `version` (build-time `FW_VERSION` via git extra script)
- [x] `help` (command-table walk)
- [x] Empty-line repeat (store last line)

## Phase 2 — Variable registry (array-aware)
- [x] Finalize dotted variable names (design A.4) — approved
- [x] `var_entry_t` table (offset/type/count/stride/flags)
- [x] `set <name> [idx] <value>` (typed parse + range checks)
- [x] `get <name>` (whole variable, grouped line)
- [x] `settings` (dump all)

## Phase 3 — Fast read
- [x] `sta` → `scales.pos` + `scales.speed` (grouped lines)

## Phase 4 — Switchover to Protocol (atomic; remove Modbus)
- [x] Protocol task owns USART1: byte-IT RX → line buffer + `HAL_UART_RxCpltCallback`
- [x] Decouple LED activity counter from Modbus (`ProtocolActivity()`)
- [x] Wire `ProtocolStart` in `RampsStart`; remove Modbus init/task; rename `modbusUart`→`commUart`
- [x] Delete `lib/Modbus`, `UARTCallback.c`, `ModbusConfig.h`; strip `Modbus.h` from `Ramps.h`
- [x] Green build + size check (flash 44.5 KB; symbols verified)

## Phase 5 — Verify (hardware handoff) — DONE 2026-06-29
- [x] `sta` / `set` / `get` / `settings` / `version` / `help` over RS485
- [x] Empty-line repeat + error cases (unknown command/variable, bad checksum, valid `*HH` accepted)
- [x] A/B vs Modbus behavior — N/A (clean replacement; register image is the same `shared`
      struct, Modbus build kept at git `b4f1b77` as fallback). Verified protocol equivalence instead.

### Phase 5 notes / findings
- **Baud was wrong on first bring-up:** framework defaulted `HSE_VALUE` to 25 MHz (board is
  8 MHz) → USART1 baud 3.125× off → garbage. Fixed with `-D HSE_VALUE=8000000` (commit `bd90460`).
- **RS485 turnaround:** added a 2 ms TX settle delay in `respBegin()` (auto-direction transceiver
  swallowed the opening bytes). Verified clean, CRC-checked; `sta` round-trip ≈ 135 Hz.
- Bench harness lives in the session scratchpad (`probe.py`/`bench.py`, pyserial @115200).

## Next — Firmware update over RS485 (bootloader)
Tracked in **`bootloader_todo.md`** (design: `protocol_design.md` Part B). Protocol has shipped.
