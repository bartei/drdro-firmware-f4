/*
 * Native unit tests for the drDRO line protocol (transport-independent logic).
 * HAL/CMSIS are mocked (test/mocks); UART TX is captured for assertions.
 * Run: pio test -e native
 */
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "Protocol.h"
#include "SettingsStore.h"
#include "Settings.h"

/* ---- SettingsStore stubs (real impl is HW-only; no flash here) ----------- */
void    SettingsApply(rampsSharedData_t *s)      { (void)s; }
int     SettingsSave(const rampsSharedData_t *s) { (void)s; return 0; }
int     SettingsLoad(rampsSharedData_t *s)       { (void)s; return 1; }
int     SettingsBankSet(uint8_t bank)            { (void)bank; return 0; }
uint8_t SettingsActiveBank(void)                 { return 0; }
void    EnterBootloader(void)                    { }   /* HW jump; stubbed for host tests */

/* ---- TX capture (called by the mock HAL_UART_Transmit) ------------------- */
static char   cap[1024];
static size_t capLen;
void MockUartCapture(const uint8_t *d, uint16_t n) {
  if (capLen + n < sizeof(cap)) { memcpy(cap + capLen, d, n); capLen += n; cap[capLen] = 0; }
}
static void capReset(void) { capLen = 0; cap[0] = 0; }

/* ---- fixtures ------------------------------------------------------------ */
static rampsSharedData_t shared;
static UART_HandleTypeDef huart;

void setUp(void)    { memset(&shared, 0, sizeof(shared)); ProtocolStart(&huart, &shared); capReset(); }
void tearDown(void) {}

/* run a (mutable) command line through the parser */
static void run(const char *line) {
  char buf[160];
  strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  ProtocolProcessLine(buf);
}
static uint8_t xor8(const char *s) { uint8_t c = 0; while (*s) c ^= (uint8_t)*s++; return c; }

/* ---- tests --------------------------------------------------------------- */
static void test_version(void) {
  run("version");
  TEST_ASSERT_NOT_NULL(strstr(cap, "version="));
  TEST_ASSERT_NOT_NULL(strstr(cap, "crc="));
}

static void test_help_lists_commands(void) {
  run("help");
  TEST_ASSERT_NOT_NULL(strstr(cap, "sta="));
  TEST_ASSERT_NOT_NULL(strstr(cap, "set="));
  TEST_ASSERT_NOT_NULL(strstr(cap, "settings="));
}

static void test_framing_ends_with_blank_line(void) {
  run("version");
  size_t L = strlen(cap);
  TEST_ASSERT_TRUE(L >= 2);
  TEST_ASSERT_EQUAL_STRING("\n\n", cap + L - 2);   /* crc line \n + terminator \n */
}

static void test_get_float_scalar(void) {
  shared.servo.maxSpeed = 720.0f;
  run("get servo.max");
  TEST_ASSERT_NOT_NULL(strstr(cap, "servo.max=720"));
}

static void test_set_float_scalar(void) {
  run("set servo.max 1000");
  TEST_ASSERT_EQUAL_FLOAT(1000.0f, shared.servo.maxSpeed);
}

static void test_set_array_element(void) {
  run("set scales.num 2 7");
  TEST_ASSERT_EQUAL_INT32(7, shared.scales[2].syncRatioNum);
  TEST_ASSERT_EQUAL_INT32(0, shared.scales[0].syncRatioNum);   /* others untouched */
}

static void test_get_array_grouped(void) {
  shared.scales[0].syncRatioNum = 1; shared.scales[1].syncRatioNum = 2;
  shared.scales[2].syncRatioNum = 3; shared.scales[3].syncRatioNum = 4;
  run("get scales.num");
  TEST_ASSERT_NOT_NULL(strstr(cap, "scales.num=1,2,3,4"));
}

static void test_sta(void) {
  shared.scales[0].position = 10; shared.scales[1].position = 20;
  shared.scales[0].speed = 5;
  shared.servo.currentSteps = 1234; shared.servo.currentSpeed = 7.5f;
  run("sta");
  TEST_ASSERT_NOT_NULL(strstr(cap, "scales.pos=10,20,0,0"));
  TEST_ASSERT_NOT_NULL(strstr(cap, "scales.speed=5,0,0,0"));
  TEST_ASSERT_NOT_NULL(strstr(cap, "servo.pos=1234"));
  TEST_ASSERT_NOT_NULL(strstr(cap, "servo.speed=7.5"));
}

static void test_settings_dumps_all(void) {
  run("settings");
  TEST_ASSERT_NOT_NULL(strstr(cap, "scales.pos="));
  TEST_ASSERT_NOT_NULL(strstr(cap, "servo.max="));
  TEST_ASSERT_NOT_NULL(strstr(cap, "diag.cycles="));
}

static void test_error_unknown_command(void) {
  run("frobnicate");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=unknown command"));
}

static void test_error_unknown_variable(void) {
  run("get nope.var");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=unknown variable"));
}

static void test_error_readonly(void) {
  run("set scales.speed 0 5");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=read-only"));
  TEST_ASSERT_EQUAL_INT32(0, shared.scales[0].speed);
}

static void test_error_bad_index(void) {
  run("set scales.num 9 1");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=bad index"));
}

static void test_error_out_of_range_u16(void) {
  run("set servo.mode 99999");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=value out of range"));
}

static void test_checksum_valid_accepted(void) {
  char line[64]; const char *body = "set servo.max 50";
  sprintf(line, "%s*%02X", body, xor8(body));
  run(line);
  TEST_ASSERT_NULL(strstr(cap, "error="));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, shared.servo.maxSpeed);
}

static void test_checksum_invalid_rejected(void) {
  char line[64]; const char *body = "set servo.max 50";
  sprintf(line, "%s*%02X", body, (uint8_t)(xor8(body) ^ 0xFF));   /* deliberately wrong */
  run(line);
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=bad checksum"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, shared.servo.maxSpeed);           /* not applied */
}

static void test_checksum_bad_hex_rejected(void) {
  run("version*ZZ");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=bad checksum"));
}

static void test_empty_repeats_last(void) {
  shared.scales[0].position = 42;
  run("sta");
  capReset();
  run("");                                   /* empty line → repeat */
  TEST_ASSERT_NOT_NULL(strstr(cap, "scales.pos=42"));
}

static void test_empty_with_no_history(void) {
  run("");
  TEST_ASSERT_NOT_NULL(strstr(cap, "error=no previous command"));
}

static void test_feed_bytes_crlf_single_line(void) {
  const char *cmd = "version\r\n";
  for (const char *p = cmd; *p; ++p) ProtocolFeedByte((uint8_t)*p);
  TEST_ASSERT_TRUE(ProtocolLineReady());
  ProtocolService();
  TEST_ASSERT_NOT_NULL(strstr(cap, "version="));
  TEST_ASSERT_FALSE(ProtocolLineReady());    /* \n after \r must not re-trigger */
}

static void test_activity_increments(void) {
  uint32_t before = ProtocolActivity();
  ProtocolFeedByte('s'); ProtocolFeedByte('t'); ProtocolFeedByte('a'); ProtocolFeedByte('\n');
  ProtocolService();
  TEST_ASSERT_EQUAL_UINT32(before + 1, ProtocolActivity());
}

/* ---- dual-bank / settings commands -------------------------------------- */
static void test_reset_acks(void) {
  run("reset");
  TEST_ASSERT_NOT_NULL(strstr(cap, "reset=ok"));
}
static void test_save_ok_when_idle(void) {
  shared.fastData.servoMode = 0;          /* motion stopped */
  run("save");
  TEST_ASSERT_NOT_NULL(strstr(cap, "save=ok"));
}
static void test_save_ok_during_motion(void) {
  shared.fastData.servoMode = 2;          /* jog: motion active — still allowed */
  run("save");                            /* RAM-resident ISR keeps stepping during the write */
  TEST_ASSERT_NOT_NULL(strstr(cap, "save=ok"));
}
static void test_bank_reports_active(void) {
  run("bank");
  TEST_ASSERT_NOT_NULL(strstr(cap, "bank.active="));
}
static void test_bank_select_ok(void) {
  shared.fastData.servoMode = 0;
  run("bank 1");
  TEST_ASSERT_NOT_NULL(strstr(cap, "bank.active=1"));
}
static void test_rollback_acks(void) {
  run("rollback");                          /* stub SettingsActiveBank()==0 -> other=1 */
  TEST_ASSERT_NOT_NULL(strstr(cap, "rollback=1"));
}

/* ---- settings: CRC / validate / defaults / ping-pong pick (pure logic) ---- */
static void test_settings_crc32_vector(void) {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, settings_crc32("123456789", 9));  /* CRC-32/ISO-HDLC */
}
static void test_settings_seal_validate(void) {
  settings_t s; settings_defaults(&s);
  TEST_ASSERT_TRUE(settings_valid(&s));
  ((uint8_t *)&s)[8] ^= 0xFF;                 /* corrupt a payload byte */
  TEST_ASSERT_FALSE(settings_valid(&s));
}
static void test_settings_defaults(void) {
  settings_t s; settings_defaults(&s);
  TEST_ASSERT_EQUAL_INT32(100, s.scale_den[0]);
  TEST_ASSERT_EQUAL_FLOAT(720.0f, s.servo_max);
  TEST_ASSERT_EQUAL_UINT8(0xFF, s.loaded_bank);
  TEST_ASSERT_EQUAL_UINT32(0, s.seq);
}
static void test_settings_pick_newest_seq(void) {
  settings_t a, b, out;
  settings_defaults(&a); a.active_bank = 0; a.seq = 5; settings_seal(&a);
  settings_defaults(&b); b.active_bank = 1; b.seq = 7; settings_seal(&b);
  TEST_ASSERT_EQUAL_INT(1, settings_pick(&a, &b, &out));
  TEST_ASSERT_EQUAL_UINT8(1, out.active_bank);            /* b has the newer seq */
  a.seq = 9; settings_seal(&a);
  TEST_ASSERT_EQUAL_INT(1, settings_pick(&a, &b, &out));
  TEST_ASSERT_EQUAL_UINT8(0, out.active_bank);            /* now a is newer */
}
static void test_settings_pick_validity_fallback(void) {
  settings_t a, b, out;
  settings_defaults(&a); a.active_bank = 0; settings_seal(&a);
  settings_defaults(&b); b.magic = 0;                     /* b invalid */
  TEST_ASSERT_EQUAL_INT(1, settings_pick(&a, &b, &out));
  TEST_ASSERT_EQUAL_UINT8(0, out.active_bank);            /* picks the only valid one */
  a.magic = 0;                                            /* now neither valid */
  TEST_ASSERT_EQUAL_INT(0, settings_pick(&a, &b, &out));
  TEST_ASSERT_TRUE(settings_valid(&out));                 /* defaults are returned, sealed */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_version);
  RUN_TEST(test_help_lists_commands);
  RUN_TEST(test_framing_ends_with_blank_line);
  RUN_TEST(test_get_float_scalar);
  RUN_TEST(test_set_float_scalar);
  RUN_TEST(test_set_array_element);
  RUN_TEST(test_get_array_grouped);
  RUN_TEST(test_sta);
  RUN_TEST(test_settings_dumps_all);
  RUN_TEST(test_error_unknown_command);
  RUN_TEST(test_error_unknown_variable);
  RUN_TEST(test_error_readonly);
  RUN_TEST(test_error_bad_index);
  RUN_TEST(test_error_out_of_range_u16);
  RUN_TEST(test_checksum_valid_accepted);
  RUN_TEST(test_checksum_invalid_rejected);
  RUN_TEST(test_checksum_bad_hex_rejected);
  RUN_TEST(test_empty_repeats_last);
  RUN_TEST(test_empty_with_no_history);
  RUN_TEST(test_feed_bytes_crlf_single_line);
  RUN_TEST(test_activity_increments);
  RUN_TEST(test_reset_acks);
  RUN_TEST(test_save_ok_when_idle);
  RUN_TEST(test_save_ok_during_motion);
  RUN_TEST(test_bank_reports_active);
  RUN_TEST(test_bank_select_ok);
  RUN_TEST(test_rollback_acks);
  RUN_TEST(test_settings_crc32_vector);
  RUN_TEST(test_settings_seal_validate);
  RUN_TEST(test_settings_defaults);
  RUN_TEST(test_settings_pick_newest_seq);
  RUN_TEST(test_settings_pick_validity_fallback);
  return UNITY_END();
}
