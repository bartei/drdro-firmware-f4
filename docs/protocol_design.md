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
- **Array variables are returned as one grouped line:** `group.field=v0,v1,v2,v3`
  (full dotted name kept; values comma-joined). Scalars: `group.field=v`.
- Single device on the bus → **no addressing** (future: see Parking lot).
- **Integrity:** optional `*HH` checksum suffix on requests; every response ends with
  a `crc=HH` line (before the empty line). See A.8.
- **Bandwidth-conscious:** no literal `ok` token, no firmware echo, 115200 fixed.

### A.2 Commands (initial set)
| Command | Args | Action | Response |
|---|---|---|---|
| `sta` | — | Fast read: scale pos/speed + servo pos/speed/tgt/mode (tight poll loop). | `scales.pos=…` + `scales.speed=…` + `servo.pos=…` + `servo.speed=…` + `servo.tgt=…` + `servo.mode=…` + empty line |
| `set` | `<name> [idx] <value>` | Set a variable (idx required for arrays). | empty line (or `error=…`) |
| `get` | `<name>` | Read a whole variable. | `<name>=<v[,v…]>` + empty line |
| `settings` | — | Dump every configured variable. | all `key=value` lines + empty line |
| `version` | — | Firmware version. | `version=<…>` + empty line |
| `help` | — | List all commands. | one line per command + empty line |
| *(empty)* `\n` | — | Repeat the previous command (1-byte re-poll). | same as repeated command |

`set`/`get` cover all configurable options. Extend = add a variable row or a command
row. *(`update` command added in the bootloader phase.)*

### A.3 Framing & syntax (CONFIRMED)
- **Terminator = an empty line** (`\n` on its own). Client rule: read lines until a
  blank line; that completes the response. (No literal `ok`.)
- **Errors:** an `error=<reason>` line in the body; the response still ends with the
  empty line. Presence of `error=` = failure.
- **Separator:** `key=value`. **Arrays:** comma-joined values on one line.
- **Keys = exact dotted variable names, 1:1** across `sta`/`get`/`settings`.

Example:
```
sta\n
→ scales.pos=12345,988,0,42\n scales.speed=10,-3,0,0\n \n
\n                              (repeat)
→ scales.pos=12361,990,0,42\n scales.speed=11,-2,0,0\n \n
get servo.max\n
→ servo.max=720.0\n \n
set scales.sync 0 1\n           (scale 0 sync = on)
→ \n
set scales.den 0 0\n
→ error=value out of range\n \n
```

### A.4 Variable registry (core of `set`/`get`/`settings`/`help`)
One **array-aware** table drives everything:
```c
typedef enum { VT_I32, VT_U32, VT_F32, VT_BOOL } var_type_t;
typedef struct {
    const char *name;    // dotted, e.g. "scales.pos"
    void       *base;    // address of element [0]'s field
    var_type_t  type;
    uint8_t     count;   // 1 = scalar, N = array
    uint16_t    stride;  // bytes between elements (e.g. sizeof(input_t))
    uint8_t     flags;   // bit0: READONLY
} var_entry_t;
```
- `get`/`settings`: walk `count`, read each element at `base + i*stride`, join with `,`.
- `set <name> [idx] <value>`: `idx` required when `count > 1`; write `base + idx*stride`.
- `help`/`settings` = table walks.

**Proposed names** (dotted, short; *finalize before Phase 2*). `N` = scale 0–3.
| Name | Type | Count | RW | Maps to (`shared…`) |
|---|---|---|---|---|
| `scales.pos` | i32 | 4 | RW | `scales[N].position` (write = set current pos) |
| `scales.speed` | i32 | 4 | RO | `scales[N].speed` |
| `scales.num` | i32 | 4 | RW | `scales[N].syncRatioNum` |
| `scales.den` | i32 | 4 | RW | `scales[N].syncRatioDen` |
| `scales.sync` | bool | 4 | RW | `scales[N].syncEnable` |
| `scales.filt` | u16 | 4 | RW | `scales[N].filterValue` (encoder input filter, 0–15 = TIM ICxF; write reprograms the timer live; persisted as `scale_filter[N]`, default 5) |
| `servo.max` | f32 | 1 | RW | `servo.maxSpeed` |
| `servo.acc` | f32 | 1 | RW | `servo.acceleration` |
| `servo.jog` | f32 | 1 | RW | `servo.jogSpeed` |
| `servo.idx` | f32 | 1 | RW | `servo.indexSpeed` (indexing/offset feedrate cap; 0 = use `servo.max`) |
| `servo.mode` | u16 | 1 | RW | `fastData.servoMode` (0=off,1=sync/index,2=jog) |
| `servo.pos` | u32 | 1 | RO | `servo.currentSteps` |
| `servo.tgt` | i32 | 1 | RW | `servo.stepsToGo` (write = start indexed move) |
| `diag.cycles` | u32 | 1 | RO | `fastData.cycles` |
| `diag.interval` | u32 | 1 | RO | `fastData.executionInterval` |

`sta` = `scales.pos` + `scales.speed` + `servo.pos` + `servo.speed` + `servo.tgt` +
`servo.mode` (same keys/source as `get`). The servo fields let the host poll loop detect
move completion (`servo.tgt == 0`) and track enable/mode in a single round-trip — added
2026-06-29 for the drDRO-software (RCP) port.

### A.5 Concurrency (CONFIRMED approach)
TIM9 ISR touches `shared` at high rate while `set` writes from the command task.
- 32-bit aligned scalars (int/float/bool) are atomic on M4 → single-field `set` safe;
  no lock needed.
- Cross-field coherence (e.g. `scales.num`+`scales.den` together) isn't atomic across
  two `set` commands; ratios are set at config time so a 1-cycle transient is
  acceptable. If ever needed, add a combined `set scales.ratio <idx> <num> <den>`.

### A.6 `version` source
Build-time inject via PlatformIO extra script (`-D FW_VERSION="v…"`), from
`git describe`, reusing the old semantic-version scheme.

### A.7 Architecture / file impact
- **Add:** first-party `src/Protocol.c` + `include/Protocol.h`: one FreeRTOS task
  owning USART1 — byte-IT RX into a line buffer, dispatch on `\r`/`\n`, store last
  line for `\n`-repeat, command table + array-aware variable table, TX helper.
- **RX:** per-byte interrupt at 115200 (~87 µs/byte) is plenty. **TX:** blocking
  `HAL_UART_Transmit` (short responses); auto-direction transceiver → no DE pin.
- **Remove (at switchover):** `lib/Modbus/`, `UARTCallback.c`, `ModbusConfig.h`, the
  Modbus include in `Ramps.h`, the Modbus init/task in `Ramps.c`; decouple the LED
  activity counter from `RampsModbusData.u16InCnt`.

**Modbus ↔ Protocol selection (CONFIRMED):** they never coexist at runtime. Plan =
**clean replacement** (git keeps the working Modbus build at commit `b4f1b77` as a
flashable fallback). Dev strategy to stay green: build Protocol *logic* alongside
Modbus (no UART callbacks), then do the UART hand-off as one atomic switchover.
*Optional* if a switchable build is ever wanted: `-D COMM_PROTOCOL` / `-D COMM_MODBUS`
build flag + per-env `lib_ignore = Modbus` (two PlatformIO envs via `extends`).

### A.8 Message checksum (CONFIRMED)
NMEA-style, kept CLI-friendly:
- **Algorithm:** XOR-8 of the message body bytes, 2 hex digits, uppercase
  (e.g. `1A`). *(Alternative: CRC-8 / CRC-16-CCITT for stronger detection.)*
- **Request:** optional `*HH` suffix before the terminator; checksum covers every
  byte before `*`. **Present → validated** (mismatch → `error=bad checksum`);
  **absent → accepted** (CLI mode, type by hand). `*` is reserved as the delimiter.
- **Response:** always a final **`crc=HH`** line (XOR-8 over all response bytes
  before it), then the terminating empty line. Clients read until the empty line,
  pop `crc`, verify the rest.
- **Repeat (`\n`):** no checksum (re-runs the already-validated last command).

Examples:
```
set servo.max 720*70\n         (checksummed request; XOR-8 of "set servo.max 720" = 0x70)
→ crc=00\n \n                  (empty success body; crc over "" then the line)
sta\n                          (CLI request, no checksum — accepted)
→ scales.pos=12345,988,0,42\n scales.speed=10,-3,0,0\n crc=7A\n \n
set servo.max 720*FF\n
→ error=bad checksum\n crc=..\n \n
```

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
- Keep the bootloader minimal/self-contained for robustness.

---

## Part C — RS485 direction control (CONFIRMED)
Old Modbus used `EN_Port = NULL` (never toggled DE). Board uses an **auto-direction
(TX-keyed) transceiver** and it works fine → **firmware does nothing** for direction;
no DE GPIO, no hardware change. Applies to both protocol TX and bootloader TX.

---

## Confirmed decisions
- **D1 — Framing:** terminating **empty line** for success; `error=<reason>` on
  failure. No literal `ok`.
- **D2 — Syntax:** `key=value`; dotted variable names; keys 1:1 with variables.
- **D3 — RS485:** **auto-direction** transceiver; no DE GPIO.
- **D4 — Bootloader:** custom IAP + **YMODEM**, **115200 max**.
- **D5 — `get <variable>`:** yes (symmetry with `set`).
- **D6 — Build order:** **protocol first**, bootloader second.
- **D7 — Response format:** array variables returned as one grouped line
  `name=v0,v1,…`; `get` returns the whole variable, `set <name> [idx] <value>`
  targets one element; registry is array-aware.
- **D8 — Modbus vs Protocol:** never coexist at runtime; **clean replacement**
  (git keeps the Modbus build as fallback); optional `-D COMM_*` toggle if a
  switchable build is ever wanted.
- **D9 — Checksum:** XOR-8 hex; `*HH` suffix **optional** on requests (CLI-friendly),
  `crc=HH` line always on responses.
- **Names:** proposed dotted scheme (A.4) **approved**. Note: `scales.sync` and
  `servo.mode` are stored `uint16_t` (sync = 0/1).

## Parking lot (future, not now — "FFI")
- **Multi-board addressing:** prefix requests with an address (`<addr><command>\n`).
- **CLI echo:** **local echo only** (client terminal) — no firmware echo.
- **Higher baud:** locked to 115200 for now; new boards will go faster.
- **Settings persistence to flash:** wanted, but not immediate.
- **Combined ratio setter:** `set scales.ratio <idx> <num> <den>` if atomic num/den
  updates are ever needed.
