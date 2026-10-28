/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * See BootGuid.h. Deliberately HAL-free (no stm32f4xx_hal.h, no includes besides
 * stdint.h) so it links straight into the native unit tests, same as RampsRatioBanks.c.
 */
#include "BootGuid.h"

uint32_t BootGuidMix(uint32_t seedA, uint32_t seedB) {
  /* Fold seedB in with a golden-ratio multiply, then run it through a murmur3-style
   * 32-bit finalizer (xor-shift / multiply / xor-shift). Non-cryptographic by design —
   * this only needs to decorrelate the output from its inputs enough that it doesn't
   * read as "unique ID plus a small number", not to resist an attacker. */
  uint32_t x = seedA ^ (seedB * 0x9E3779B9u);
  x ^= x >> 16;
  x *= 0x85EBCA6Bu;
  x ^= x >> 13;
  x *= 0xC2B2AE35u;
  x ^= x >> 16;
  return x;
}
