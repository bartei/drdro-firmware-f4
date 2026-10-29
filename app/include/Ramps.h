/**
 * Copyright © 2022 <Stefano Bertelli>
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the “Software”), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef THIRD_PARTY_RAMPS_H_
#define THIRD_PARTY_RAMPS_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"
#include "Scales.h"
#include "BlinkCode.h"

/* USR_LED diagnostic code rendered by userLedTask (BlinkCode.h). Default BLINK_APP. */
extern volatile uint8_t gBlinkCode;

/* Place a function in RAM (.RamFunc — loaded in flash, copied to RAM by startup) so it
 * can execute while flash is being erased/programmed (single-bank flash stalls all flash
 * reads). Used for the motion ISR path so a settings/bank save never drops steps. */
#define RAM_FUNC __attribute__((section(".RamFunc")))

/* Fast GPIO writes — a single store to BSRR, far cheaper than HAL_GPIO_WritePin and,
 * being inline (no flash call), safe on the motion ISR hot path / in RAM-resident code.
 * Set = write the pin bit; reset = write it shifted into the high half. (BSRR idiom from
 * the old multi_servo branch.) */
static inline void gpioSet(GPIO_TypeDef *port, uint16_t pin)   { port->BSRR = pin; }
static inline void gpioReset(GPIO_TypeDef *port, uint16_t pin) { port->BSRR = (uint32_t)pin << 16; }

#define STEP_PIN GPIO_PIN_0
#define STEP_GPIO_PORT GPIOA

#define DIR_PIN GPIO_PIN_14
#define DIR_GPIO_PORT GPIOB

#define ENA_PIN GPIO_PIN_15
#define ENA_GPIO_PORT GPIOB
#define ENA_DELAY_MS 500

#define USR_LED_Pin GPIO_PIN_12
#define USR_LED_GPIO_Port GPIOB

#define SPARE_1_PIN GPIO_PIN_1
#define SPARE_1_GPIO_PORT GPIOA

#define SPARE_2_PIN GPIO_PIN_3
#define SPARE_2_GPIO_PORT GPIOA

#define SPARE_3_PIN GPIO_PIN_4
#define SPARE_3_GPIO_PORT GPIOA

typedef struct {
  int32_t delta;
  uint32_t oldPosition;
  uint32_t position;
  int32_t scaledDelta;
  int32_t error;
} deltaPosError_t;

typedef struct {
  TIM_HandleTypeDef *timerHandle;
  int32_t position;
  int32_t speed;          /* encoder steps per second (updateSpeedTask, RO) */
  int32_t syncRatioNum, syncRatioDen;
  uint16_t syncEnable;
  uint16_t filterValue;   /* encoder input-capture filter, 0..SCALES_FILTER_MAX */
  uint32_t revSteps;      /* rotary: counts per revolution; 0 = not rotary */
} input_t;

typedef struct {
  float maxSpeed;
  float currentSpeed;
  float jogSpeed;
  float acceleration;
  float indexSpeed;     /* feedrate cap for indexing/offset moves; 0 = use maxSpeed */
  int32_t stepsToGo;
  uint32_t destinationSteps;
  uint32_t currentSteps;
  uint32_t desiredSteps;
} servo_t;

typedef struct {
  uint32_t servoCurrent;
  uint32_t servoDesired;
  uint32_t stepsToGo;
  float servoSpeed;
  int32_t scaleCurrent[SCALES_COUNT];
  int32_t scaleSpeed[SCALES_COUNT];
  uint32_t cycles;
  uint32_t executionInterval;
  uint16_t servoMode; // Servo modes: 0=disabled, 1=sync/index, 2=jog
  uint16_t activeRatioBank;  /* ratioBanks[] index currently live in scales[].syncRatio* */
  uint16_t armedRatioBank;   /* 0 = not armed; else = (armed bank index + 1) — the +1
                              * disambiguates "armed into bank 0" from "not armed". */
  uint32_t armPhaseTolerance; /* `arm` engage tolerance, rotary watched-scale counts;
                               * needs bench tuning for real spindle speed/encoder. */
  int32_t  armInitialError[SCALES_COUNT]; /* internal: snapshot, per scale, of
                              * RatioBankMatchError() at the moment of arming — only
                              * meaningful for a non-rotary watched scale. Waiting for
                              * that error to change sign (or hit exactly 0) is the
                              * intercept condition for it. NOT protocol-exposed —
                              * cross-task bookkeeping between servoEnableTask and the
                              * `arm` command only. See RampsRatioBanks.h. */
  uint32_t armTargetPhase[SCALES_COUNT]; /* internal: `arm`'s `phase` argument, per
                              * scale, folded into [0,revSteps) — only meaningful for a
                              * rotary watched scale. NOT protocol-exposed, same
                              * treatment as armInitialError. */
} fastData_t;

/* Ratio bank: a stored {ratioNum, ratioDen, syncEnable} per scale, selected as a unit —
 * loaded into the live scales[] fields by RatioBankActivate(), one bank at a time. NOT
 * the flash firmware-image "bank" (SettingsBankSet et al) — that's why this is always
 * "rbank" over the protocol, never bare "bank". */
#define RBANK_COUNT 2

typedef struct {
  int32_t  ratioNum, ratioDen;
  uint16_t syncEnable;
} ratioBankAxis_t;

typedef struct {
  ratioBankAxis_t axis[SCALES_COUNT];
} ratioBank_t;

typedef struct {
  uint32_t executionInterval;
  uint32_t executionIntervalPrevious;
  uint32_t executionIntervalCurrent;
  uint32_t executionCycles;
  servo_t servo;
  input_t scales[SCALES_COUNT];
  fastData_t fastData;
  ratioBank_t ratioBanks[RBANK_COUNT];
} rampsSharedData_t;

typedef struct {
  // Comm shared data (protocol register image)
  rampsSharedData_t shared;

  // STM32 Related
  TIM_HandleTypeDef *synchroRefreshTimer;
  UART_HandleTypeDef *commUart;

  deltaPosError_t scalesDeltaPos[SCALES_COUNT];
  deltaPosError_t scalesSyncDeltaPos[SCALES_COUNT];
  deltaPosError_t scalesSpeed[SCALES_COUNT];
  deltaPosError_t rampsDeltaPos;
  uint32_t servoPreviousDirection;
} rampsHandler_t;

void RampsStart(rampsHandler_t *rampsData);

RAM_FUNC void SynchroRefreshTimerIsr(rampsHandler_t *data);

_Noreturn void updateSpeedTask(void *argument);

_Noreturn void userLedTask(__attribute__((unused)) void *argument);

_Noreturn void servoEnableTask(void *argument);

#endif