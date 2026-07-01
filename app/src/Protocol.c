/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * drDRO line protocol (replaces Modbus). Command in (text, \r/\n/both terminated)
 * → key=value lines out, terminated by an empty line; errors carry `error=<reason>`.
 * See protocol_design.md.
 *
 * ProtocolStart() owns USART1: byte-IT RX feeds ProtocolFeedByte() (ISR), which
 * wakes a FreeRTOS service task on a complete line; the task parses, dispatches,
 * and writes the response with blocking TX (auto-direction RS485, no DE pin).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include "cmsis_os2.h"
#include "Protocol.h"
#include "Bootloader.h"
#include "SettingsStore.h"

/* ---- bound UART + shared data ------------------------------------------- */
static UART_HandleTypeDef *sUart = NULL;
static rampsSharedData_t  *sShared = NULL;

/* ---- RX task + activity ------------------------------------------------- */
static uint8_t           sRxByte;
static volatile uint8_t  sTxActive = 0;   /* drop self-echo while transmitting   */
static volatile uint32_t sRxLines  = 0;   /* processed-command counter (LED blink) */
static osThreadId_t      sTask = NULL;
static const osThreadAttr_t kProtoTaskAttr = {
  .name = "protocol", .stack_size = 512 * 4, .priority = (osPriority_t) osPriorityNormal,
};
static void protocolTask(void *arg);

/* ---- RX line assembly (CRLF-safe; preserves the deliberate empty-line repeat) */
static char    sLine[PROTOCOL_LINE_MAX + 1];   /* in-progress line                */
static uint16_t sLen   = 0;
static char    sPendEOL = 0;                   /* last terminator, for \r\n pairs */
static char    sReady[PROTOCOL_LINE_MAX + 1];  /* last completed line             */
static volatile uint8_t sReadyFlag = 0;
static char    sLast[PROTOCOL_LINE_MAX + 1];   /* previous command (for repeat)   */

/* ---- checksum (XOR-8, hex) — isolated so it can become CRC-8/16 later ---- */
static uint8_t protoCksum(const char *buf, int len) {
  uint8_t c = 0;
  for (int i = 0; i < len; i++) c ^= (uint8_t)buf[i];
  return c;
}
static char hexDigit(uint8_t v) { return "0123456789ABCDEF"[v & 0xF]; }
static void toHex2(uint8_t b, char *out) { out[0] = hexDigit(b >> 4); out[1] = hexDigit(b); out[2] = '\0'; }
static int  fromHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static int     isHex2(const char *s) { return s[0] && s[1] && !s[2] && fromHex(s[0]) >= 0 && fromHex(s[1]) >= 0; }
static uint8_t parseHex2(const char *s) { return (uint8_t)((fromHex(s[0]) << 4) | fromHex(s[1])); }

/* ---- TX helpers (blocking; auto-direction transceiver, no DE pin) -------- */
static uint8_t sTxCrc = 0;                          /* running XOR over the response body */
static void txRaw(const char *s) {                  /* send, not counted toward crc       */
  if (sUart) HAL_UART_Transmit(sUart, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}
static void txStr(const char *s) {                  /* send + accumulate crc              */
  for (const char *p = s; *p; ++p) sTxCrc ^= (uint8_t)*p;
  txRaw(s);
}
/* RS485 is half-duplex with an auto-direction transceiver (no DE pin): after we finish
 * receiving the command we must let the line settle before driving TX, or the opening
 * bytes of the response are swallowed by the RX->TX turnaround. Runs in protocolTask
 * context, so osDelay() (yields) is safe. Tune PROTOCOL_TX_SETTLE_MS on the bench. */
#define PROTOCOL_TX_SETTLE_MS 2   /* verified clean on the bench (25/25, CRC-checked) */
static void respBegin(void) { osDelay(PROTOCOL_TX_SETTLE_MS); sTxCrc = 0; }
static void respKV(const char *key, const char *val) { txStr(key); txStr("="); txStr(val); txStr("\n"); }
static void respError(const char *reason)            { txStr("error="); txStr(reason); txStr("\n"); }
static void respEnd(void) {                          /* crc=HH then the terminating empty line */
  char h[3]; toHex2(sTxCrc, h);
  txRaw("crc="); txRaw(h); txRaw("\n");
  txRaw("\n");
}

/* ---- command handlers ---------------------------------------------------- */
typedef void (*cmd_fn)(int argc, char **argv);
typedef struct { const char *name; cmd_fn fn; const char *help; } cmd_t;
static const cmd_t kCommands[];   /* fwd decl */

static void cmdVersion(int argc, char **argv) {
  (void)argc; (void)argv;
  respKV("version", FW_VERSION);
}

static void cmdHelp(int argc, char **argv) {
  (void)argc; (void)argv;
  for (const cmd_t *c = kCommands; c->name; c++) respKV(c->name, c->help);
}

/* ---- variable registry (array-aware) ------------------------------------ */
typedef enum { VT_I32, VT_U32, VT_F32, VT_U16 } var_type_t;
#define V_RO 1
typedef struct {
  const char *name;
  size_t      offset;   /* byte offset of element [0] within rampsSharedData_t */
  var_type_t  type;
  uint8_t     count;    /* 1 = scalar, N = array */
  uint16_t    stride;   /* bytes between array elements */
  uint8_t     flags;    /* V_RO */
} var_entry_t;

#define OFF(f) offsetof(rampsSharedData_t, f)
static const var_entry_t kVars[] = {
  { "scales.pos",    OFF(scales[0].position),         VT_I32, 4, sizeof(input_t), 0    },
  { "scales.speed",  OFF(scales[0].speed),            VT_I32, 4, sizeof(input_t), V_RO },
  { "scales.num",    OFF(scales[0].syncRatioNum),     VT_I32, 4, sizeof(input_t), 0    },
  { "scales.den",    OFF(scales[0].syncRatioDen),     VT_I32, 4, sizeof(input_t), 0    },
  { "scales.sync",   OFF(scales[0].syncEnable),       VT_U16, 4, sizeof(input_t), 0    },
  { "servo.max",     OFF(servo.maxSpeed),             VT_F32, 1, 0, 0    },
  { "servo.acc",     OFF(servo.acceleration),         VT_F32, 1, 0, 0    },
  { "servo.jog",     OFF(servo.jogSpeed),             VT_F32, 1, 0, 0    },
  { "servo.idx",     OFF(servo.indexSpeed),           VT_F32, 1, 0, 0    },
  { "servo.mode",    OFF(fastData.servoMode),         VT_U16, 1, 0, 0    },
  { "servo.pos",     OFF(servo.currentSteps),         VT_U32, 1, 0, V_RO },
  { "servo.speed",   OFF(servo.currentSpeed),         VT_F32, 1, 0, V_RO },
  { "servo.tgt",     OFF(servo.stepsToGo),            VT_I32, 1, 0, 0    },
  { "diag.cycles",   OFF(fastData.cycles),            VT_U32, 1, 0, V_RO },
  { "diag.interval", OFF(fastData.executionInterval), VT_U32, 1, 0, V_RO },
  { "diag.test",     OFF(testValue),                  VT_U32, 1, 0, 0    },
  { NULL, 0, 0, 0, 0, 0 },
};

static const var_entry_t *findVar(const char *name) {
  for (const var_entry_t *v = kVars; v->name; v++)
    if (strcmp(name, v->name) == 0) return v;
  return NULL;
}
static void *fieldPtr(const var_entry_t *v, int idx) {
  return (uint8_t *)sShared + v->offset + (size_t)idx * v->stride;
}
static void formatField(const var_entry_t *v, int idx, char *out, size_t n) {
  void *p = fieldPtr(v, idx);
  switch (v->type) {
    case VT_I32: snprintf(out, n, "%ld", (long)*(int32_t *)p); break;
    case VT_U32: snprintf(out, n, "%lu", (unsigned long)*(uint32_t *)p); break;
    case VT_U16: snprintf(out, n, "%u", (unsigned)*(uint16_t *)p); break;
    case VT_F32: snprintf(out, n, "%g", (double)*(float *)p); break;
  }
}
/* parse+store; returns 0 ok, -1 on parse/range error. 32-bit/16-bit aligned
 * stores are atomic on M4 vs the TIM9 ISR, so no lock is needed (design A.5). */
static int writeField(const var_entry_t *v, int idx, const char *s) {
  void *p = fieldPtr(v, idx);
  char *end = NULL;
  switch (v->type) {
    case VT_I32: { long  x = strtol(s, &end, 0);  if (*end) return -1; *(int32_t  *)p = (int32_t)x;  break; }
    case VT_U32: { unsigned long x = strtoul(s, &end, 0); if (*end) return -1; *(uint32_t *)p = (uint32_t)x; break; }
    case VT_U16: { unsigned long x = strtoul(s, &end, 0); if (*end || x > 0xFFFF) return -1; *(uint16_t *)p = (uint16_t)x; break; }
    case VT_F32: { float x = strtof(s, &end);     if (*end) return -1; *(float    *)p = x;           break; }
  }
  return 0;
}
static void emitVar(const var_entry_t *v) {
  char val[24];
  txStr(v->name); txStr("=");
  for (int i = 0; i < v->count; i++) {
    if (i) txStr(",");
    formatField(v, i, val, sizeof(val));
    txStr(val);
  }
  txStr("\n");
}

/* ---- registry-backed commands ------------------------------------------- */
static void cmdGet(int argc, char **argv) {
  if (argc < 2) { respError("usage: get <name>"); return; }
  const var_entry_t *v = findVar(argv[1]);
  if (!v) { respError("unknown variable"); return; }
  emitVar(v);
}
static void cmdSettings(int argc, char **argv) {
  (void)argc; (void)argv;
  for (const var_entry_t *v = kVars; v->name; v++) emitVar(v);
}
static void cmdSet(int argc, char **argv) {
  if (argc < 2) { respError("usage: set <name> [idx] <value>"); return; }
  const var_entry_t *v = findVar(argv[1]);
  if (!v) { respError("unknown variable"); return; }
  if (v->flags & V_RO) { respError("read-only"); return; }

  int idx = 0;
  const char *valStr;
  if (v->count > 1) {                        /* array: need <idx> <value> */
    if (argc < 4) { respError("usage: set <name> <idx> <value>"); return; }
    char *end = NULL;
    long i = strtol(argv[2], &end, 10);
    if (*end || i < 0 || i >= v->count) { respError("bad index"); return; }
    idx = (int)i; valStr = argv[3];
  } else {                                    /* scalar: <value> */
    if (argc < 3) { respError("usage: set <name> <value>"); return; }
    valStr = argv[2];
  }
  if (writeField(v, idx, valStr) != 0) { respError("value out of range"); return; }
  /* success: no body line; respEnd() emits crc + empty line */
}
static void cmdSta(int argc, char **argv) {
  (void)argc; (void)argv;
  static const char *kStaVars[] = {
    "scales.pos", "scales.speed", "servo.pos", "servo.speed", "servo.tgt", "servo.mode"
  };
  for (unsigned i = 0; i < sizeof(kStaVars) / sizeof(kStaVars[0]); i++) {
    const var_entry_t *v = findVar(kStaVars[i]);
    if (v) emitVar(v);
  }
}

/* Enter the IAP bootloader: arm the handshake word, ack, then reset (done in
 * ProtocolService once the ack has fully left the wire). See include/Bootloader.h. */
static volatile uint8_t sResetPending = 0;
static void cmdUpdate(int argc, char **argv) {
  (void)argc; (void)argv;
  BOOT_FLAG = BOOT_MAGIC;
  sResetPending = 1;
  respKV("update", "ready");
}

/* Plain system reset (no bootloader request) — lets the client drive the boot cycle.
 * NOTE: a reset re-samples the board's floating BOOT0 (can land in the ST ROM); the
 * robust app->bootloader path (jump, not reset) is the D2/B5 follow-up. */
static void cmdReset(int argc, char **argv) {
  (void)argc; (void)argv;
  sResetPending = 1;
  respKV("reset", "ok");
}

static void cmdSave(int argc, char **argv) {
  (void)argc; (void)argv;
  if (SettingsSave(sShared) != 0)    { respError("flash write"); return; }
  respKV("save", "ok");
}
static void cmdLoad(int argc, char **argv) {
  (void)argc; (void)argv;
  respKV("load", SettingsLoad(sShared) ? "ok" : "default");
}
static void cmdBank(int argc, char **argv) {
  char buf[4];
  if (argc < 2) {                    /* report the persisted active bank */
    snprintf(buf, sizeof(buf), "%u", (unsigned)SettingsActiveBank());
    respKV("bank.active", buf);
    return;
  }
  if ((argv[1][0] != '0' && argv[1][0] != '1') || argv[1][1]) { respError("usage: bank <0|1>"); return; }
  if (SettingsBankSet((uint8_t)(argv[1][0] - '0')) != 0) { respError("flash write"); return; }
  respKV("bank.active", argv[1]);
}

/* Switch to the other bank and hand off to the bootloader (which copies it into Exec
 * and runs it). Convenience over `bank <other>` + `reset`. */
static void cmdRollback(int argc, char **argv) {
  (void)argc; (void)argv;
  uint8_t other = SettingsActiveBank() ? 0u : 1u;
  if (SettingsBankSet(other) != 0) { respError("flash write"); return; }
  char b[2] = { (char)('0' + other), '\0' };
  respKV("rollback", b);
  sResetPending = 1;                   /* ProtocolService jumps to the bootloader */
}

static const cmd_t kCommands[] = {
  { "sta",      cmdSta,      "fast read: scale pos/speed + servo pos/speed/tgt/mode" },
  { "set",      cmdSet,      "set <name> [idx] <value>" },
  { "get",      cmdGet,      "get <name>" },
  { "settings", cmdSettings, "dump all variables" },
  { "save",     cmdSave,     "persist settings to flash (motion stopped)" },
  { "load",     cmdLoad,     "reload settings from flash" },
  { "bank",     cmdBank,     "bank [<0|1>] — active bank: report, or select for next boot" },
  { "rollback", cmdRollback, "switch to the other bank and reboot into it" },
  { "version",  cmdVersion,  "firmware version" },
  { "help",     cmdHelp,     "list commands" },
  { "update",   cmdUpdate,   "reboot into the firmware-update bootloader" },
  { "reset",    cmdReset,    "system reset" },
  { NULL, NULL, NULL },
};

/* ---- line processing ----------------------------------------------------- */
void ProtocolProcessLine(char *line) {
  char work[PROTOCOL_LINE_MAX + 1];
  respBegin();

  if (line[0] == '\0') {                      /* empty line → repeat last command */
    if (sLast[0] == '\0') { respError("no previous command"); respEnd(); return; }
    strncpy(work, sLast, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';
  } else {
    strncpy(work, line, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';

    /* Optional `*HH` checksum: validate if present, then strip it. */
    char *star = strrchr(work, '*');
    if (star) {
      if (!isHex2(star + 1)) { respError("bad checksum"); respEnd(); return; }
      uint8_t want = parseHex2(star + 1);
      *star = '\0';
      if (protoCksum(work, (int)strlen(work)) != want) { respError("bad checksum"); respEnd(); return; }
    }
    strncpy(sLast, work, sizeof(sLast) - 1); sLast[sizeof(sLast) - 1] = '\0';  /* stripped body */
  }

  char *argv[8];
  int   argc = 0;
  for (char *tok = strtok(work, " "); tok && argc < 8; tok = strtok(NULL, " ")) argv[argc++] = tok;

  if (argc == 0) { respError("empty command"); respEnd(); return; }

  for (const cmd_t *c = kCommands; c->name; c++) {
    if (strcmp(argv[0], c->name) == 0) { c->fn(argc, argv); respEnd(); return; }
  }
  respError("unknown command");
  respEnd();
}

/* ---- RX byte feed (ISR-safe: no blocking, emits nothing) ----------------- */
void ProtocolFeedByte(uint8_t b) {
  if (b == '\r' || b == '\n') {
    if (sLen > 0) {                           /* finalize a content line          */
      sLine[sLen] = '\0';
      memcpy(sReady, sLine, sLen + 1);
      sReadyFlag = 1;
      sLen = 0;
      sPendEOL = (char)b;
    } else if (sPendEOL && (char)b != sPendEOL) {
      sPendEOL = 0;                           /* swallow the 2nd half of \r\n      */
    } else {                                  /* deliberate empty line → repeat    */
      sReady[0] = '\0';
      sReadyFlag = 1;
      sPendEOL = 0;
    }
    return;
  }
  if (sLen < PROTOCOL_LINE_MAX) sLine[sLen++] = (char)b;  /* else: truncate to max */
  sPendEOL = 0;
}

uint8_t ProtocolLineReady(void) { return sReadyFlag; }

void ProtocolService(void) {
  if (!sReadyFlag) return;
  sReadyFlag = 0;
  sTxActive = 1;                       /* ignore self-echo during the response   */
  ProtocolProcessLine(sReady);
  sTxActive = 0;
  sRxLines++;                          /* command processed → blink the LED       */

  if (sResetPending) {                 /* `update`/`reset`: ack is sent (blocking TX waits */
    osDelay(5);                        /* for TC); let the line settle, then hand off.     */
    EnterBootloader();                 /* JUMP to the bootloader (BOOT0-safe; no return).  */
  }                                    /* BOOT_FLAG (set by `update`) selects CLI vs boot. */
}

uint32_t ProtocolActivity(void) { return sRxLines; }

/* RX interrupt: one byte at a time → line buffer; wake the task on a full line. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart != sUart) return;
  uint8_t b = sRxByte;
  HAL_UART_Receive_IT(sUart, &sRxByte, 1);    /* re-arm immediately               */
  if (sTxActive) return;                       /* drop our own transmitted echo    */
  ProtocolFeedByte(b);
  if (sReadyFlag) osThreadFlagsSet(sTask, 0x01U);
}

static void protocolTask(void *arg) {
  (void)arg;
  HAL_UART_Receive_IT(sUart, &sRxByte, 1);
  for (;;) {
    osThreadFlagsWait(0x01U, osFlagsWaitAny, osWaitForever);
    ProtocolService();
  }
}

void ProtocolStart(UART_HandleTypeDef *huart, rampsSharedData_t *shared) {
  sUart = huart;
  sShared = shared;
  sLen = 0; sPendEOL = 0; sReadyFlag = 0; sTxActive = 0; sRxLines = 0;
  sLine[0] = '\0'; sReady[0] = '\0'; sLast[0] = '\0';
  sTask = osThreadNew(protocolTask, NULL, &kProtoTaskAttr);
}
