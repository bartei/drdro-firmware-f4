/*
 * Minimal host-side mock of the STM32 HAL — only what Protocol.c / Ramps.h /
 * Scales.h need to compile for native unit tests. UART TX is redirected to
 * MockUartCapture() (defined in the test) so responses can be asserted.
 */
#ifndef MOCK_STM32F4XX_HAL_H
#define MOCK_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define HAL_MAX_DELAY 0xFFFFFFFFU

typedef struct { uint32_t dummy; } GPIO_TypeDef;
typedef struct { void *Instance; uint32_t dummy; } TIM_HandleTypeDef;
typedef struct { void *Instance; uint32_t dummy; } UART_HandleTypeDef;

/* Test hook: capture transmitted bytes. */
void MockUartCapture(const uint8_t *data, uint16_t size);

static inline HAL_StatusTypeDef
HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t to) {
  (void)h; (void)to;
  MockUartCapture(d, n);
  return HAL_OK;
}
static inline HAL_StatusTypeDef
HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *d, uint16_t n) {
  (void)h; (void)d; (void)n;
  return HAL_OK;
}

/* CMSIS core: no-op on host (the `update` reset path is hardware-only). */
static inline void NVIC_SystemReset(void) { }

#endif /* MOCK_STM32F4XX_HAL_H */
