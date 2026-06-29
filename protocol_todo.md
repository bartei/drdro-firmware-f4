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
- [ ] Protocol task owns USART1: byte-IT RX → line buffer + `HAL_UART_RxCpltCallback`
- [ ] Decouple LED activity counter from Modbus
- [ ] Wire `ProtocolStart` in `RampsStart`; remove Modbus init/task
- [ ] Delete `lib/Modbus`, `UARTCallback.c`, `ModbusConfig.h`; strip `Modbus.h` from `Ramps.h`
- [ ] Green build + size check

## Phase 5 — Verify (hardware handoff)
- [ ] `sta` / `set` / `get` / `settings` / `version` / `help` over RS485
- [ ] Empty-line repeat + error cases
- [ ] A/B vs Modbus behavior
