/**
 * Copyright © 2026 <Stefano Bertelli>
 * MIT.
 *
 * drDRO line protocol — transport-independent core (Phase 1).
 * Command in (text, \r/\n/both terminated) → key=value lines out, terminated by an
 * empty line. Errors carry an `error=<reason>` line. See protocol_design.md.
 *
 * The USART1 RX interrupt + FreeRTOS task that drive this are wired at the Modbus
 * switchover (Phase 4); here TX is blocking and RX is fed byte-by-byte via
 * ProtocolFeedByte() so the logic compiles and runs alongside the Modbus stack.
 */
#include <string.h>
#include "Protocol.h"

/* ---- bound UART ---------------------------------------------------------- */
static UART_HandleTypeDef *sUart = NULL;

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
static void respBegin(void) { sTxCrc = 0; }
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

/* Registry-backed commands land in Phase 2/3. */
static void cmdSta(int argc, char **argv)      { (void)argc; (void)argv; respError("not implemented"); }
static void cmdSet(int argc, char **argv)      { (void)argc; (void)argv; respError("not implemented"); }
static void cmdGet(int argc, char **argv)      { (void)argc; (void)argv; respError("not implemented"); }
static void cmdSettings(int argc, char **argv) { (void)argc; (void)argv; respError("not implemented"); }

static const cmd_t kCommands[] = {
  { "sta",      cmdSta,      "fast read: scale positions + speeds" },
  { "set",      cmdSet,      "set <name> [idx] <value>" },
  { "get",      cmdGet,      "get <name>" },
  { "settings", cmdSettings, "dump all variables" },
  { "version",  cmdVersion,  "firmware version" },
  { "help",     cmdHelp,     "list commands" },
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
  ProtocolProcessLine(sReady);
}

void ProtocolStart(UART_HandleTypeDef *huart) {
  sUart = huart;
  sLen = 0; sPendEOL = 0; sReadyFlag = 0;
  sLine[0] = '\0'; sReady[0] = '\0'; sLast[0] = '\0';
}
