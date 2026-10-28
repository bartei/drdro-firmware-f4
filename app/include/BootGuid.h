/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * Pure, HAL-free mixing for the per-boot diagnostic GUID (fastData.bootGuid, see Ramps.h).
 * Not cryptographic — just needs to scatter two correlated-looking inputs (e.g. the
 * chip's fixed unique ID and a boot-time counter) into something that doesn't visibly
 * look like "unique ID plus a small number". Native-testable: see test/test_bootguid.
 */
#ifndef THIRD_PARTY_BOOTGUID_H_
#define THIRD_PARTY_BOOTGUID_H_

#include <stdint.h>

/* Mix two 32-bit seeds into one pseudo-random-looking 32-bit value. Deterministic
 * (same inputs always give the same output) — callers chain it across more than two
 * seeds by feeding its output back in as one side of the next call. */
uint32_t BootGuidMix(uint32_t seedA, uint32_t seedB);

#endif
