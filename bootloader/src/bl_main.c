/**
 * Copyright © 2026 <Stefano Bertelli>
 * MIT.
 *
 * drDRO IAP bootloader (sector 0 @ 0x08000000). Design: protocol_design.md Part B;
 * plan: bootloader_todo.md.
 *
 * Boot decision (run before any peripheral init, so a normal boot hands the app clean
 * hardware): consume the handshake word (include/Bootloader.h). If an update was
 * requested OR the app image looks invalid, enter update mode (YMODEM receive — Phase
 * B3); otherwise relocate VTOR/MSP and jump to the application at 0x08004000.
 *
 * Self-contained: HAL for clock + USART only, polled I/O, SysTick timebase (no RTOS).
 */
#include <string.h>
#include "stm32f4xx_hal.h"
#include "Bootloader.h"

static UART_HandleTypeDef huart1;

void Error_Handler(void) { __disable_irq(); while (1) { } }

/* Same 8 MHz HSE -> 100 MHz PLL as the app, so USART1 baud (115200) matches exactly. */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 4;
  osc.PLL.PLLN = 100;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) Error_Handler();
}

/* HAL timebase tick — no FreeRTOS in the bootloader, so SysTick is ours. */
void SysTick_Handler(void) { HAL_IncTick(); }

/* USART1 pins: PA10=RX, PA15=TX, AF7 (mirrors the app's MspInit). */
void HAL_UART_MspInit(UART_HandleTypeDef *h) {
  if (h->Instance != USART1) return;
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin = GPIO_PIN_10 | GPIO_PIN_15;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &g);
}

static void usart1_init(void) {
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void bl_puts(const char *s) {
  HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

/* Vector-table sanity: initial SP in RAM, reset vector in app flash (DB2). */
static int app_valid(void) {
  uint32_t sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);
  if (sp < 0x20000000U || sp > 0x20020000U) return 0;
  if (pc <  APP_BASE_ADDR || pc >= 0x08080000U) return 0;
  return 1;
}

static void jump_to_app(void) {
  uint32_t sp = *(volatile uint32_t *)APP_BASE_ADDR;
  uint32_t pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);
  __disable_irq();
  SysTick->CTRL = 0U;                 /* stop the tick we never started, just in case */
  SCB->VTOR = APP_BASE_ADDR;
  __set_MSP(sp);
  __DSB();
  __ISB();
  ((void (*)(void))pc)();
  while (1) { }                       /* not reached */
}

int main(void) {
  uint32_t req = BOOT_FLAG;
  BOOT_FLAG = 0U;                     /* consume: a failed/aborted update won't loop */

  if (req != BOOT_MAGIC && app_valid()) {
    jump_to_app();                    /* normal boot — no return */
  }

  /* --- update mode (clock/USART only needed here) --- */
  HAL_Init();
  SystemClock_Config();
  usart1_init();
  bl_puts("drDRO bootloader: update mode (send firmware via YMODEM)\r\n");

  /* TODO Phase B3: ymodem_receive() -> erase app sectors -> write -> verify -> reset */
  for (;;) { }
}
