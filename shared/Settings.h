/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * Persistent settings — shared layout for the app and the IAP bootloader. Design:
 * dualbank_design.md (DC4). Both sides read it; both may write it (read-modify-write the
 * whole struct so the other side's fields survive). Validity = magic + CRC32.
 *
 * Power-fail safe via ping-pong across TWO sectors (A = sector 1, B = sector 2): each
 * write goes to the *inactive* slot with an incremented `seq`, so a brown-out mid-erase
 * leaves the previous slot intact. settings_load() picks the valid slot with the newest
 * seq (falls back to defaults if neither is valid).
 *
 * Header-only: struct + CRC + validate + defaults + slot selection are pure (the pick is
 * pointer-based and host-testable); each project supplies its own flash erase/program.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stddef.h>
#include "Bootloader.h"   /* SETTINGS_BASE, SETTINGS_SECTOR, BANK_COUNT, boot_mode_t */

#define SETTINGS_MAGIC    0x44524F31U   /* "DRO1" */
#define SETTINGS_VERSION  2U            /* bumped: added seq + bank_crc (ping-pong) */
#define SETTINGS_SCALES   4U            /* must match the app's SCALES_COUNT */

/* Two settings slots (ping-pong). A = sector 1, B = sector 2. */
#define SETTINGS_A_BASE    SETTINGS_BASE
#define SETTINGS_A_SECTOR  SETTINGS_SECTOR
#define SETTINGS_B_BASE    0x08008000U
#define SETTINGS_B_SECTOR  2U
#define SETTINGS_SLOT_BASE(n)    ((n) ? SETTINGS_B_BASE   : SETTINGS_A_BASE)
#define SETTINGS_SLOT_SECTOR(n)  ((n) ? SETTINGS_B_SECTOR : SETTINGS_A_SECTOR)
#define SETTINGS_SLOT_IMG(n)     ((const settings_t *)(uintptr_t)SETTINGS_SLOT_BASE(n))

/* No field reordering without bumping SETTINGS_VERSION. Size must stay a multiple of 4. */
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t boot_mode;                   /* boot_mode_t: 0=app, 1=bootloader */
  uint32_t seq;                         /* monotonic write counter (ping-pong selection) */
  uint8_t  active_bank;                 /* 0|1 — which stored bank should run */
  uint8_t  loaded_bank;                 /* 0|1|0xFF — bank currently copied into Exec */
  uint16_t reserved0;

  /* --- application payload (mirrors the protocol registry) --- */
  int32_t  scale_num[SETTINGS_SCALES];
  int32_t  scale_den[SETTINGS_SCALES];
  uint16_t scale_sync[SETTINGS_SCALES];
  float    servo_max;
  float    servo_acc;
  float    servo_jog;
  uint16_t servo_mode;
  uint16_t reserved1;

  uint32_t bank_crc[2];                 /* CRC32 of each bank's 128 KB region (0 = unknown) */
  uint32_t crc;                         /* CRC32 over all preceding bytes */
} settings_t;

/* CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) — table-less, tiny. */
static inline uint32_t settings_crc32(const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFU;
  while (len--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
static inline uint32_t settings_compute_crc(const settings_t *s) {
  return settings_crc32(s, offsetof(settings_t, crc));
}
static inline int settings_valid(const settings_t *s) {
  return s->magic == SETTINGS_MAGIC && s->crc == settings_compute_crc(s);
}
static inline void settings_seal(settings_t *s) {   /* stamp magic/version + crc before write */
  s->magic = SETTINGS_MAGIC;
  s->version = SETTINGS_VERSION;
  s->crc = settings_compute_crc(s);
}
static inline void settings_defaults(settings_t *s) {
  for (uint32_t i = 0; i < sizeof(*s); i++) ((uint8_t *)s)[i] = 0;
  s->boot_mode   = BOOT_MODE_APP;
  s->seq         = 0;
  s->active_bank = 0;
  s->loaded_bank = 0xFF;                /* unknown → force a copy on first boot */
  for (uint32_t i = 0; i < SETTINGS_SCALES; i++) {
    s->scale_num[i] = 1;
    s->scale_den[i] = 100;
    s->scale_sync[i] = 0;
  }
  s->servo_max = 720.0f;
  s->servo_acc = 120.0f;
  s->servo_jog = 0.0f;
  s->servo_mode = 0;
  settings_seal(s);
}

/* Pure: choose the valid slot with the newest seq (signed diff handles wraparound).
 * Fills *out and returns 1; if neither is valid, fills defaults and returns 0.
 * Pointer-based so it is host-testable (the flash-reading wrappers below call it). */
static inline int settings_pick(const settings_t *a, const settings_t *b, settings_t *out) {
  int va = settings_valid(a), vb = settings_valid(b);
  if (va && vb) { *out = ((int32_t)(b->seq - a->seq) >= 0) ? *b : *a; return 1; }
  if (va) { *out = *a; return 1; }
  if (vb) { *out = *b; return 1; }
  settings_defaults(out);
  return 0;
}

/* Load the newest valid slot from flash into *out (defaults if neither valid). */
static inline int settings_load(settings_t *out) {
  return settings_pick(SETTINGS_SLOT_IMG(0), SETTINGS_SLOT_IMG(1), out);
}

/* Which physical slot holds the newest valid image (0=A, 1=B), or -1 if none. */
static inline int settings_newest_slot(void) {
  int va = settings_valid(SETTINGS_SLOT_IMG(0)), vb = settings_valid(SETTINGS_SLOT_IMG(1));
  if (va && vb) return ((int32_t)(SETTINGS_SLOT_IMG(1)->seq - SETTINGS_SLOT_IMG(0)->seq) >= 0) ? 1 : 0;
  if (va) return 0;
  if (vb) return 1;
  return -1;
}

/* Prepare *s for writing: bump seq past the newest slot and seal; return the slot index
 * to program (the inactive one). Writers erase+program SETTINGS_SLOT_{SECTOR,BASE}(slot). */
static inline int settings_prepare(settings_t *s) {
  int newest = settings_newest_slot();
  uint32_t base_seq = (newest < 0) ? 0U : SETTINGS_SLOT_IMG(newest)->seq;
  s->seq = base_seq + 1U;
  settings_seal(s);
  return (newest < 0) ? 0 : (1 - newest);
}

#endif /* SETTINGS_H */
