#include "main.h"
#include "cmsis_os.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "Bootloader.h"   /* APP_EXEC_BASE — keep VTOR in sync with the linker/bootloader */
#include "SettingsStore.h"

rampsHandler_t RampsData;

/* Vector table relocated to RAM so interrupt entry (esp. the motion ISR) doesn't fetch
 * from flash — lets the step ISR keep running while flash is erased/programmed (settings
 * /bank save). 512-byte aligned (VTOR requires alignment >= table size, ~101 vectors). */
static uint32_t g_ramVectors[128] __attribute__((aligned(0x200)));

void SystemClock_Config(void);

/* Hand off to the IAP bootloader by JUMPING to 0x08000000 — never NVIC_SystemReset().
 * A system reset re-samples the floating BOOT0 pin and can boot the ST system ROM
 * (see HARDWARE.md HW-1); a jump never samples BOOT0 and preserves the no-init RAM
 * BOOT_FLAG. Tears down the running app (mask IRQs, stop SysTick, clear NVIC, switch to
 * MSP) then branches to the bootloader's reset vector. */
void EnterBootloader(void) {
  __disable_irq();
  SysTick->CTRL = 0U; SysTick->LOAD = 0U; SysTick->VAL = 0U;
  for (int i = 0; i < 8; i++) { NVIC->ICER[i] = 0xFFFFFFFFU; NVIC->ICPR[i] = 0xFFFFFFFFU; }
  uint32_t sp = *(volatile uint32_t *)BL_BASE_ADDR;
  uint32_t pc = *(volatile uint32_t *)(BL_BASE_ADDR + 4U);
  __set_CONTROL(0);                 /* leave the FreeRTOS task PSP — run on MSP */
  __ISB();
  SCB->VTOR = BL_BASE_ADDR;
  __set_MSP(sp);
  __DSB(); __ISB();
  __enable_irq();                   /* bootloader needs SysTick/flash IRQs; none pending */
  ((void (*)(void))pc)();
  while (1) { }                     /* not reached */
}

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* IAP: this image runs from the Exec region (0x08020000). Copy the vector table into
   * RAM and point VTOR at it (before HAL_Init, so the HAL/NVIC use it): keeping vectors
   * in RAM means interrupt entry never fetches from flash, so the motion ISR keeps firing
   * during a flash erase/program (settings/bank save). */
  for (uint32_t i = 0; i < 128U; i++)
    g_ramVectors[i] = ((const volatile uint32_t *)APP_EXEC_BASE)[i];
  SCB->VTOR = (uint32_t)g_ramVectors;
  __DSB();
  __ISB();

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  MX_TIM9_Init();
  // htim1 is used in encoder mode
  // htim2 is used in encoder mode
  // htim3 is used in encoder mode
  // htim4 is used in encoder mode
  // htim9 is used to generate the synchro motion

  RampsData.shared.scales[0].timerHandle = &htim1;
  RampsData.shared.scales[1].timerHandle = &htim2;
  RampsData.shared.scales[2].timerHandle = &htim3;
  RampsData.shared.scales[3].timerHandle = &htim4;
  RampsData.synchroRefreshTimer = &htim9;
  RampsData.commUart = &huart1;
  RampsStart(&RampsData);

  /* Override compiled defaults with persisted settings (scales ratios, servo cfg)
   * if a valid image is present in flash. Before the scheduler starts. */
  SettingsApply(&RampsData.shared);

  /* The encoder timers were started by RampsStart with the compiled-in filter;
   * reprogram them with the persisted per-scale value. */
  for (int i = 0; i < SCALES_COUNT; i++)
    setScaleFilter(RampsData.shared.scales[i].timerHandle, RampsData.shared.scales[i].filterValue);

  /* Init and start the scheduler (tasks were created in RampsStart) */
  osKernelInitialize();
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  while (1)
  {

  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM11 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{

  if (htim->Instance == TIM11) {
    HAL_IncTick();
  }

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
}
#endif /* USE_FULL_ASSERT */
