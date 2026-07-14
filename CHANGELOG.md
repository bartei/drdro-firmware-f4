# CHANGELOG

<!-- version list -->

## v0.6.1 (2026-07-14)

### Bug Fixes

- Update devcontainer
  ([`0a17186`](https://github.com/bartei/drdro-firmware-f4/commit/0a1718604e25d50a7894c828353c9d86d8844b70))

### Continuous Integration

- Add mock static content deploy pipeline
  ([`f9fd76a`](https://github.com/bartei/drdro-firmware-f4/commit/f9fd76a0bfdd5d2eb00caaffd9e431e18c02f03a))

- Remove static content deploy pipeline
  ([`0e2be87`](https://github.com/bartei/drdro-firmware-f4/commit/0e2be87229c4002c7a5bb1e6ce55f19170a1eb48))


## v0.6.0 (2026-07-02)

### Continuous Integration

- Semantic-release — dev betas, main stables, changelog + release notes
  ([`4fa25cc`](https://github.com/bartei/drdro-firmware-f4/commit/4fa25cc6a00c5bb4872e3c7acf7befd9e4ab0a88))

### Features

- **scales**: Per-scale encoder input filter (scales.filt, TIM ICxF 0-15)
  ([`5548d8c`](https://github.com/bartei/drdro-firmware-f4/commit/5548d8c39176a2e619f6d5db6e651fdbe9a8c0d9))


## v0.5.2 (2026-07-01)

### Testing

- Remove diag.test scratch variable
  ([`6a73d29`](https://github.com/bartei/drdro-firmware-f4/commit/6a73d29936d4ae8cb9a584e43cfe866218d3e7cf))


## v0.5.1 (2026-07-01)

### Testing

- Add diag.test scratch variable to validate append-only settings
  ([`8627767`](https://github.com/bartei/drdro-firmware-f4/commit/8627767562453e904e0892f9f5727977d2b5d2f5))


## v0.5.0 (2026-06-30)

### Features

- Forward-compatible settings + persisted indexing feedrate (servo.idx)
  ([`5dac543`](https://github.com/bartei/drdro-firmware-f4/commit/5dac543220a9fd65ff5d6502802cc1912e315cb8))


## v0.4.4 (2026-06-30)

### Documentation

- Relicense to GPL-3.0-or-later
  ([`d9b2ab3`](https://github.com/bartei/drdro-firmware-f4/commit/d9b2ab33616ad5565b2c10007ceb3eb0e021c779))


## v0.4.3 (2026-06-30)

### Features

- **protocol**: Add servo.tgt + servo.mode to sta
  ([`0a1e6b3`](https://github.com/bartei/drdro-firmware-f4/commit/0a1e6b33baba4f514b7c035e7c131c3ba4d8fbcd))


## v0.4.2 (2026-06-29)

### Documentation

- Rewrite README (dual-bank, tooling, CLI); move docs into docs/; drop stale CubeMX files
  ([`b05c350`](https://github.com/bartei/drdro-firmware-f4/commit/b05c3501910c89cea67faf62506275216ee675d7))


## v0.4.1 (2026-06-29)

### Documentation

- Tidy bootloader_todo into terse one-liners; move IAP design (DB1-DB4, gotchas) into
  dualbank_design; reflect current state
  ([`798801f`](https://github.com/bartei/drdro-firmware-f4/commit/798801fcbb90060b14407ae5cb25ef8a58cbe967))


## v0.4.0 (2026-06-29)

### Features

- Rollback command on both CLIs; mark dual-bank tracker complete
  ([`d5f3bd3`](https://github.com/bartei/drdro-firmware-f4/commit/d5f3bd366e857a1f67140acf05f5af82625910d9))


## v0.3.0 (2026-06-29)

### Features

- App jumps to bootloader (BOOT0-safe); HARDWARE.md; release asset names
  ([`927fda1`](https://github.com/bartei/drdro-firmware-f4/commit/927fda126eb02951af543e04d70d030311c66a06))


## v0.2.3 (2026-06-29)

### Features

- **bootloader**: D2 hardening — RAM motion ISR, ping-pong settings, bank CRC32
  ([`770ff23`](https://github.com/bartei/drdro-firmware-f4/commit/770ff232c7f01d44aa162d57286fd88200b86a21))


## v0.2.2 (2026-06-29)

### Chores

- Split into app/ + bootloader/ sibling projects
  ([`3027be0`](https://github.com/bartei/drdro-firmware-f4/commit/3027be081f4c9a21466f1934a976799cf0f6dcb7))

### Documentation

- Mark protocol Phase 5 verified; sync migration + bootloader trackers
  ([`256073f`](https://github.com/bartei/drdro-firmware-f4/commit/256073fb7f783b057d37d9a8724756d7575b62be))

- **resume**: Update handoff header — branch, layout, next step (B3)
  ([`d425492`](https://github.com/bartei/drdro-firmware-f4/commit/d425492967aa1e3d7df21c1aecf758d39d741b8e))

### Features

- **bootloader**: Dual-bank A/B updates, CLIs, persistent settings, factory image
  ([`d7ff7b5`](https://github.com/bartei/drdro-firmware-f4/commit/d7ff7b5d6fd751e549a978b52c5fc5882b7e376a))

- **bootloader**: IAP groundwork — app relocation, update command, bootloader (B0-B2)
  ([`075d1a3`](https://github.com/bartei/drdro-firmware-f4/commit/075d1a35fe3425f99e73ad94c99b7d850e1767ac))


## v0.2.1 (2026-06-29)

### Bug Fixes

- **protocol**: Correct USART1 baud and RS485 TX turnaround
  ([`bd90460`](https://github.com/bartei/drdro-firmware-f4/commit/bd9046049241e12811b4d5da34e12035b9f542f3))


## v0.2.0 (2026-06-29)

### Features

- VS Code dev container for PlatformIO/STM32 + README refresh
  ([`8e007b8`](https://github.com/bartei/drdro-firmware-f4/commit/8e007b80d5a101bad747523de06639533057f559))


## v0.1.2 (2026-06-29)

### Continuous Integration

- Restrict GITHUB_TOKEN permissions in CI workflow
  ([`df62806`](https://github.com/bartei/drdro-firmware-f4/commit/df62806b95bfc9d91da6c3b8537d1c31d81211b3))


## v0.1.1 (2026-06-29)

### Continuous Integration

- Emit firmware.hex as a build/release artifact
  ([`0c2ea38`](https://github.com/bartei/drdro-firmware-f4/commit/0c2ea3870701dba2da50bf5a1133a484381b9254))


## v0.1.0 (2026-06-29)

- Initial Release
