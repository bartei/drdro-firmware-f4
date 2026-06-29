# drDRO — Known Hardware Issues (address on the next PCB revision)

Issues in the current board that the firmware works around but that should be **fixed in
hardware** on the next fab run. Each entry: symptom, root cause, current firmware
mitigation, and the recommended hardware change.

---

## HW-1 — BOOT0 floats → MCU intermittently boots the ST system ROM ⚠️ (fix on next fab)

**Severity:** high (intermittent boot failures / failed field updates).

**Symptom**
- After a system reset, the board sometimes does not run our firmware: serial returns a
  lone `\x00` and the part is unresponsive to our CLI.
- Under a debugger, halting shows the PC in **`0x1FFFxxxx`** (the STM32 system-memory
  bootloader) with MSP in low SRAM — i.e. the chip booted ST's built-in ROM, not flash.
- Roughly 50/50 per reset; `st-flash reset` exhibits it too.

**Root cause**
- The STM32F411 boot mode is latched from the **BOOT0 pin** at the rising edge of reset
  (`BOOT0=0` → main flash; `BOOT0=1` → system memory). On this board BOOT0 is **not tied
  low** (floating / inadequate pull-down), so it reads high unpredictably and the part
  boots the ROM. Any reset that re-samples BOOT0 is affected: power-on, NRST,
  `NVIC_SystemReset`/AIRCR `SYSRESETREQ`.

**Firmware mitigation (in place — keeps the product working, but it's a workaround)**
- The bootloader hands off to the app by a **direct jump** (sets VTOR/MSP and branches),
  never a reset — a jump does not re-sample BOOT0.
- The app enters the bootloader by **jumping to `0x08000000`** (`EnterBootloader()` in
  `app/src/main.c`), not `NVIC_SystemReset()` — RAM (`BOOT_FLAG`) survives the jump, and
  BOOT0 is not sampled. So the whole app↔bootloader update cycle avoids resets.
- Residual exposure: a true power-on / NRST and the bootloader CLI `reset` command still
  sample BOOT0. On the bench, retry the reset until the app answers `version`.

**Recommended hardware fix (next PCB revision)**
- Tie **BOOT0 firmly to GND** through a pull-down (e.g. 10 kΩ), or hard-wire it low if no
  ROM-bootloader access is needed. Verify it reads a solid logic-low at reset across temp
  / supply ramp. After this, resets boot flash deterministically and the firmware
  workarounds become belt-and-suspenders (they can stay).
- If keeping the option to enter the ST ROM, add a deliberate jumper/strap to GND by
  default with a test point to pull high — never leave BOOT0 floating.

**References:** `~/.claude` memory `boot0-floating-system-rom`; `dualbank_design.md`;
`bootloader_todo.md` (B3/B5 notes).

---

*(Add further hardware findings below as they're discovered.)*
