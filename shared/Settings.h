/**
 * Copyright © 2026 <Stefano Bertelli>
 * MIT.
 *
 * Persistent settings — shared layout for the app and the IAP bootloader. Lives in
 * flash sector 1 (SETTINGS_BASE, 16 KB). Design: dualbank_design.md (DC4). Both sides
 * read it; both may write it (read-modify-write the whole struct so the other side's
 * fields survive). Validity = magic + CRC32; on invalid, callers fall back to defaults.
 *
 * Header-only: struct + CRC + validate + defaults are pure (no flash); each project
 * supplies its own flash read/write (the bootloader in flash.c, the app in Settings.c).
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stddef.h>
#include "Bootloader.h"   /* SETTINGS_BASE, boot_mode_t */

#define SETTINGS_MAGIC    0x44524F31U   /* "DRO1" */
#define SETTINGS_VERSION  1U
#define SETTINGS_SCALES   4U            /* must match the app's SCALES_COUNT */

/* No field reordering without bumping SETTINGS_VERSION (forward-compat handled by
 * version + CRC: an old/short image fails CRC → defaults). Size must stay /4. */
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t boot_mode;                   /* boot_mode_t: 0=app, 1=bootloader */
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

/* The live settings image in flash (read-only view). */
#define SETTINGS_IMAGE  ((const settings_t *)SETTINGS_BASE)

/* Load the flash image into *out; if invalid, fill *out with defaults. Returns 1 if the
 * flash image was valid, 0 if defaults were used. Pure read (no HAL needed). */
static inline int settings_load(settings_t *out) {
  *out = *SETTINGS_IMAGE;
  if (settings_valid(out)) return 1;
  settings_defaults(out);
  return 0;
}

#endif /* SETTINGS_H */
