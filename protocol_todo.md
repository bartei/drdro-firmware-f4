# drDRO Line Protocol — Todo

Phased progress tracker. Detail in `protocol_design.md`. Bootloader is a separate
doc (`bootloader_todo.md`), after this.

## Phase 1 — Scaffold & transport
- [ ] Finalize variable name list (design A.4 table)
- [ ] Add `include/Protocol.h` + `src/Protocol.c`
- [ ] USART1 byte-IT RX → line buffer
- [ ] Line assembly on `\r` / `\n`
- [ ] Protocol FreeRTOS task
- [ ] TX helper (write lines + terminator; auto-direction, no DE)

## Phase 2 — Dispatch & core commands
- [ ] Tokenizer + command table + dispatcher
- [ ] Empty-line repeat (store last line)
- [ ] `version` (build-time `FW_VERSION` via git)
- [ ] `help` (command-table walk)
- [ ] Empty-line success framing + `error=` handling

## Phase 3 — Variable registry
- [ ] `var_entry_t` table mapping `shared` fields
- [ ] `set <var> <value>` (typed parse + range checks)
- [ ] `get <var>`
- [ ] `settings` (dump all)
- [ ] Critical section for grouped sets (num/den)

## Phase 4 — Fast read
- [ ] `sta` → scale positions + speeds (1:1 keys)

## Phase 5 — Remove Modbus & integrate
- [ ] Delete `lib/Modbus`, `UARTCallback.c`, `ModbusConfig.h`
- [ ] Strip Modbus init/task from `Ramps.c`
- [ ] Wire Protocol init in `RampsStart`
- [ ] Green build + size check

## Phase 6 — Verify (hardware handoff)
- [ ] `sta` / `set` / `get` / `settings` / `version` / `help` over RS485
- [ ] Empty-line repeat + error cases
- [ ] A/B vs Modbus behavior
