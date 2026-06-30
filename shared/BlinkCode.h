/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * USR_LED (PB12) diagnostic blink codes, shared by the app and the bootloader. The LED
 * repeats a pattern of N short blinks followed by a ~0.7 s gap, ~once per second, so the
 * current mode (or error) can be read off visually. App and bootloader render this with
 * their own timing source (FreeRTOS task / polled SysTick), but agree on the code values.
 */
#ifndef BLINKCODE_H
#define BLINKCODE_H

typedef enum {
  BLINK_APP        = 1,   /* application running normally */
  BLINK_BOOTLOADER = 2,   /* bootloader CLI / update mode */
  /* error codes (>= 3) — app reserves these for fault indication (future) */
  BLINK_ERR_FLASH  = 5,   /* flash erase/program/verify failure */
} blink_code_t;

/* Pattern timing (ms): each blink = ON then OFF, then a gap before repeating. */
#define BLINK_ON_MS   120U
#define BLINK_OFF_MS  160U
#define BLINK_GAP_MS  700U

#endif /* BLINKCODE_H */
