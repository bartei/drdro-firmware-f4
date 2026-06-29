# drDRO — Custom Protocol & Firmware-Update Design

> **Status: CONFIRMED (2026-06-28).** Decisions locked (see §Confirmed decisions).
> Detail lives here; phased work lives in `protocol_todo.md`. Bootloader gets its
> own `bootloader_todo.md` later (protocol ships first).

Goal: replace the Modbus RTU layer with a simple, human- and machine-friendly
line protocol over RS485, and later add firmware update over the same bus.

---

## Part A — Line protocol

### A.1 Overview
- Each **command** is a text string with space-separated parameters, terminated by
  `\r`, `\n`, or both.
- Each **response** is zero or more `key=value` lines (`\n`-separated) followed by a
  **terminating empty line** (a lone `\n`). Errors carry an `error=<reason>` line.
- Single device on the bus → **no addressing** (future: see Parking lot).
- **Bandwidth-conscious:** no literal `ok` token, no firmware echo, 115200 fixed.

### A.2 Commands (initial set)
| Command | Args | Action | Response |
|---|---|---|---|
| `sta` | — | Fast read: scale positions + speeds (tight poll loop). | scale `key=value` lines + empty line |
| `set` | `<variable> <value>` | Set a configured variable. | empty line (or `error=…`) |
| `get` | `<variable>` | Read one variable. | `<var>=<value>` + empty line |
| `settings` | — | Dump every configured variable. | all `key=value` lines + empty line |
| `version` | — | Firmware version. | `version=<…>` + empty line |
| `help` | — | List all commands. | one line per command + empty line |
| *(empty)* `\n` | — | Repeat the previous command (1-byte re-poll). | same as repeated command |

`set`/`get` cover all configurable options (sync ratios, scale positions, scale
ratios, servo enable, max speed, acceleration, mode…). Extend = add a variable row
or a command row. *(`update` command is added in the bootloader phase.)*

### A.3 Framing & syntax (CONFIRMED)
- **Terminator = an empty line** (`\n` on its own). Client rule: read lines until a
  blank line; that completes the response. (No literal `ok`.)
- **Errors:** an `error=<reason>` line appears in the body; the response still ends
  with the empty line. Presence of `error=` = failure.
- **Separator:** `key=value` (trivial `dict(line.split('=',1) …)` in Python).
- **Keys = exact variable names, 1:1** (no per-command terse aliases). Bandwidth is
  saved by keeping the *canonical variable names themselves short* (see A.4) — same
  key in `sta`, `get`, and `settings` so the client parses one stable namespace.

Example:
```
sta\n
→ sc0=12345\n sc0_speed=10\n sc1=988\n sc1_speed=-3\n
  sc2=0\n sc2_speed=0\n sc3=42\n sc3_speed=0\n \n
\n                         (repeat)
→ sc0=12361\n sc0_speed=11\n … \n \n
set sc0_sync 1\n
→ \n                      (success: just the empty line)
set sc0_den 0\n
→ error=value out of range\n \n
```

### A.4 Variable registry (core of `set`/`get`/`settings`/`help`)
One table drives everything (named, typed, self-documenting, easy to extend):
```c
typedef enum { VT_I32, VT_U32, VT_F32, VT_BOOL } var_type_t;
typedef struct {
    const char *name;   // short canonical name == response key
    void       *ptr;    // -> field in rampsHandler_t.shared
    var_type_t  type;
    uint8_t     flags;  // bit0: READONLY, bit1: grouped (needs critical section)
} var_entry_t;
```

**Proposed variable names** (short canonical; *finalize in todo Phase 1*). `N` = scale 0–3.
| Name | Type | RW | Maps to (`shared…`) |
|---|---|---|---|
| `scN` | i32 | RW | `scales[N].position` (write = set current position) |
| `scN_speed` | i32 | RO | `scales[N].speed` |
| `scN_num` | i32 | RW | `scales[N].syncRatioNum` ⟵ grouped |
| `scN_den` | i32 | RW | `scales[N].syncRatioDen` ⟵ grouped |
| `scN_sync` | bool | RW | `scales[N].syncEnable` |
| `sv_max` | f32 | RW | `servo.maxSpeed` |
| `sv_acc` | f32 | RW | `servo.acceleration` |
| `sv_jog` | f32 | RW | `servo.jogSpeed` |
| `sv_mode` | u16 | RW | `fastData.servoMode` (0=off,1=sync/index,2=jog) |
| `sv_pos` | u32 | RO | `servo.currentSteps` |
| `sv_tgt` | i32 | RW | `servo.stepsToGo` (write = start indexed move) |
| `cycles` | u32 | RO | `fastData.cycles` |
| `interval` | u32 | RO | `fastData.executionInterval` |

`sta` returns the fast set: `scN` + `scN_speed` for N=0..3 (same keys as above).

### A.5 Concurrency (CONFIRMED approach)
TIM9 ISR touches `shared` at high rate while `set` writes from the command task.
- 32-bit aligned scalars (int/float/bool) are atomic on M4 → single-field `set` safe.
- **Grouped fields** (`scN_num`+`scN_den`) → short critical section (mask TIM9 IRQ)
  so the ISR never sees a torn ratio. Marked via `flags` bit1.

### A.6 `version` source
Build-time inject via PlatformIO extra script (`-D FW_VERSION="v…"`), from
`git describe`, reusing the old semantic-version scheme.

### A.7 Architecture / file impact
- **Remove:** `lib/Modbus/`, `UARTCallback.c`, `ModbusConfig.h`, the Modbus
  init/task in `Ramps.c`.
- **Add:** first-party `src/Protocol.c` + `include/Protocol.h`: one FreeRTOS task
  owning USART1 — byte-IT RX into a line buffer, dispatch on `\r`/`\n`, store last
  line for `\n`-repeat, command table + variable table, TX helper.
- **RX:** per-byte interrupt at 115200 (~87 µs/byte) is plenty. **TX:** auto-direction
  transceiver → no DE pin to toggle.
- Net code shrink vs the Modbus stack.

---

## Part B — Firmware update over UART / RS485 (later; protocol ships first)

**Custom in-app bootloader (IAP), NOT the STM32 ROM bootloader. YMODEM @115200.**

### B.1 Why not the ROM bootloader
- F411 ROM UART bootloader is hardwired to **USART1 = PA9 (TX)/PA10 (RX)**. Our
  USART1 is **PA15 (TX)/PA10 (RX)**: RX matches, but replies would exit **PA9 =
  `TIM1_CH2`** (encoder pin), not the RS485 driver (PA15). *(Confirm vs AN2606.)*
- Also speaks ST's binary AN3155, not ours, and drives no direction pin.

### B.2 Custom IAP bootloader
- Sector 0 (16 KB) @ `0x08000000`; app moves to sector 1 (`0x08004000`) with its own
  linker script + `SCB->VTOR` relocation.
- Reuses our USART1 + auto-direction → half-duplex works identically to the app.
- No BOOT0 jumper / ST tooling — just a YMODEM sender.
- F411 is **not dual-bank** → **never erase sector 0**; a failed transfer leaves the
  bootloader recoverable (only the app is at risk).

### B.3 Flash layout (F411CE, 512 KB; sectors 4×16K / 1×64K / 3×128K)
| Region | Sector(s) | Address | Size | Contents |
|---|---|---|---|---|
| Bootloader | 0 | `0x08000000` | 16 KB | IAP loader (never erased by update) |
| Application | 1 → 7 | `0x08004000` | ~496 KB | main firmware (VTOR relocated here) |

*(Could reserve a sector for settings/CRC/metadata later.)*

### B.4 Transfer protocol
- **YMODEM** — off-the-shelf host tools (`lrzsz`/`sb`, Tera Term, pyserial). Binary
  mode switch. **Max baud 115200** (hardware limit; faster boards later).

### B.5 Update flow
1. App gets `update` → write magic flag (RTC backup reg / no-init RAM) → reset.
2. Bootloader checks flag (and/or app CRC): if set/invalid → receive image, erase app
   sectors, write, **verify CRC**, clear flag; else set VTOR/MSP, jump to app.
3. Optional: short boot-time "knock" window to force update even if the app is bricked.

### B.6 Notes
- **Baud locked at 115200** — current circuit can't go faster; will raise on new
  boards. (Sets the floor on transfer time.)
- Keep the bootloader minimal/self-contained (register-level or tiny HAL subset) for
  robustness.

---

## Part C — RS485 direction control (CONFIRMED)
Old Modbus used `EN_Port = NULL` (never toggled DE). Board uses an **auto-direction
(TX-keyed) transceiver** and it works fine → **firmware does nothing** for direction;
no DE GPIO, no hardware change. Applies to both protocol TX and bootloader TX.

---

## Confirmed decisions
- **D1 — Framing:** terminating **empty line** for success; `error=<reason>` on
  failure. No literal `ok`.
- **D2 — Syntax:** `key=value`; **no per-command terse keys** — keep response keys
  1:1 with variable names (Python-parseable); shorten the canonical names in code.
- **D3 — RS485:** **auto-direction** transceiver; no DE GPIO.
- **D4 — Bootloader:** custom IAP + **YMODEM**, **115200 max**.
- **D5 — `get <variable>`:** yes (symmetry with `set`).
- **D6 — Build order:** **protocol first**, bootloader second.

## Parking lot (future, not now — "FFI")
- **Multi-board addressing:** prefix requests with an address (`<addr><command>\n`).
- **CLI echo:** **local echo only** (client terminal) — no firmware echo, saves
  bandwidth.
- **Higher baud:** locked to 115200 for now; new boards will go faster.
- **Settings persistence to flash:** wanted, but not immediate.
