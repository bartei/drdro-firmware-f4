/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * App-side persistent settings — see SettingsStore.h. Reads/writes the shared settings
 * image in flash sector 1 (../shared/Settings.h). The write erases + programs the whole
 * struct with interrupts masked: the flash bus stall freezes the core for the duration,
 * so callers gate this on motion-stopped (the step ISR can't run anyway while masked).
 */
#include "stm32f4xx_hal.h"
#include "SettingsStore.h"
#include "Settings.h"     /* shared layout */
#include "Bootloader.h"
#include "Scales.h"       /* SCALES_COUNT */

static void shared_to_settings(const rampsSharedData_t *sh, settings_t *s) {
  for (int i = 0; i < SCALES_COUNT; i++) {
    s->scale_num[i]  = sh->scales[i].syncRatioNum;
    s->scale_den[i]  = sh->scales[i].syncRatioDen;
    s->scale_sync[i] = sh->scales[i].syncEnable;
  }
  s->servo_max   = sh->servo.maxSpeed;
  s->servo_acc   = sh->servo.acceleration;
  s->servo_jog   = sh->servo.jogSpeed;
  s->servo_index = sh->servo.indexSpeed;
  s->servo_mode  = sh->fastData.servoMode;
  s->test_value  = sh->testValue;
}

static void settings_to_shared(const settings_t *s, rampsSharedData_t *sh) {
  for (int i = 0; i < SCALES_COUNT; i++) {
    sh->scales[i].syncRatioNum = s->scale_num[i];
    sh->scales[i].syncRatioDen = s->scale_den[i];
    sh->scales[i].syncEnable   = s->scale_sync[i];
  }
  sh->servo.maxSpeed     = s->servo_max;
  sh->servo.acceleration = s->servo_acc;
  sh->servo.jogSpeed     = s->servo_jog;
  sh->servo.indexSpeed   = s->servo_index;
  sh->fastData.servoMode = s->servo_mode;
  sh->testValue          = s->test_value;
}

/* Erase one settings slot's sector and program the struct there (word-verified). */
static int write_slot(int slot, const settings_t *s) {
  uint32_t base = SETTINGS_SLOT_BASE(slot);
  HAL_FLASH_Unlock();
  FLASH_EraseInitTypeDef e = {0};
  e.TypeErase    = FLASH_TYPEERASE_SECTORS;
  e.Sector       = SETTINGS_SLOT_SECTOR(slot);
  e.NbSectors    = 1U;
  e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t err = 0;
  int rc = (HAL_FLASHEx_Erase(&e, &err) == HAL_OK) ? 0 : -1;
  const uint32_t *w = (const uint32_t *)s;
  for (uint32_t i = 0; rc == 0 && i < sizeof(*s) / 4U; i++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, base + i * 4U, w[i]) != HAL_OK) { rc = -1; break; }
    if (*(volatile uint32_t *)(base + i * 4U) != w[i]) { rc = -1; break; }
  }
  HAL_FLASH_Lock();
  return rc;
}

/* Ping-pong write to the inactive slot (prepare bumps seq + seals). Interrupts stay
 * ENABLED: the flash bus stall freezes only flash-resident code, while the motion ISR
 * (relocated to RAM, with a RAM vector table) keeps generating steps throughout. */
static int save_struct(settings_t *s) {
  int slot = settings_prepare(s);
  return write_slot(slot, s);
}

void SettingsApply(rampsSharedData_t *shared) {
  settings_t s;
  if (settings_load(&s)) settings_to_shared(&s, shared);
}

int SettingsLoad(rampsSharedData_t *shared) {
  settings_t s;
  if (!settings_load(&s)) return 0;
  settings_to_shared(&s, shared);
  return 1;
}

int SettingsSave(const rampsSharedData_t *shared) {
  settings_t s;
  settings_load(&s);                 /* preserve bootloader fields (active/loaded/mode/crc) */
  shared_to_settings(shared, &s);
  return save_struct(&s);            /* prepare() bumps seq + seals */
}

int SettingsBankSet(uint8_t bank) {
  if (bank >= BANK_COUNT) return -1;
  settings_t s;
  settings_load(&s);
  s.active_bank = bank;
  return save_struct(&s);
}

uint8_t SettingsActiveBank(void) {
  settings_t s;
  return settings_load(&s) ? s.active_bank : 0U;
}
