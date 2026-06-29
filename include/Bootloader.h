/**
 * Copyright © 2026 <Stefano Bertelli>
 * MIT.
 *
 * Shared contract between the drDRO application and its IAP bootloader.
 * Design: protocol_design.md Part B; plan: bootloader_todo.md.
 *
 * Flash layout (F411CE, 512 KB):
 *   sector 0       0x08000000  16 KB   bootloader (never erased by an update)
 *   sectors 1..7   0x08004000  496 KB  application (VTOR relocated here)
 *
 * Update handshake: the app's `update` command writes BOOT_FLAG = BOOT_MAGIC into a
 * reserved no-init RAM word (top 16 bytes of RAM, carved out by both linker scripts),
 * then resets. The bootloader reads it: magic => enter update (YMODEM); otherwise jump
 * to the app. The bootloader clears it once consumed so a later reset boots normally.
 */
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

#define BL_SECTOR_SIZE   0x4000U       /* sector 0 = 16 KB (bootloader) */
#define APP_BASE_ADDR    0x08004000U   /* sector 1: application + relocated vector table */

/* No-init handshake word. Must match the LENGTH reserve in both linker scripts
 * (RAM = 128K - 16 => this address is just past _estack, untouched by stack/heap). */
#define BOOT_FLAG_ADDR   0x2001FFF0U
#define BOOT_MAGIC       0xB00710ADU   /* "boot load" */
#define BOOT_FLAG        (*(volatile uint32_t *)BOOT_FLAG_ADDR)

#endif /* BOOTLOADER_H */
