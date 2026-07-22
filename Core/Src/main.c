/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "esp12f.h"
#include "uart_bridge.h"
#include "oled.h"
#include "app_config.h"
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 烟雾浓度百分比。
 * Smoke_Task() 会周期性更新该值；主循环把它传给 ESP12F_SetStatus()，
 * OLED 刷新时也直接显示该值。
 */
static uint8_t smoke_percent;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Smoke_Init(void);
static void Smoke_Task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ADC 校准。
 * STM32F1 ADC 在正式采样前建议执行一次校准，减少零点和增益误差。
 */
static void Smoke_Init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* 烟雾传感器采样任务。
 *
 * 该函数采用非阻塞方式运行：
 * - 每 APP_SMOKE_SAMPLE_INTERVAL_MS 启动一次 ADC 转换；
 * - 在 APP_SMOKE_WINDOW_MS 时间窗口内累加采样值；
 * - 窗口结束后计算平均 ADC 值，并换算为 0-100 的相对百分比。
 *
 * PA0 / ADC1_IN0 的 ADC 满量程为 4095。公式中的 +2047 用于四舍五入：
 *   percent = round(average * 100 / 4095)
 */
static void Smoke_Task(void)
{
  static uint32_t sample_tick;
  static uint32_t window_tick;
  static uint32_t sample_sum;
  static uint16_t sample_count;
  uint32_t now = HAL_GetTick();

  if ((now - sample_tick) >= APP_SMOKE_SAMPLE_INTERVAL_MS)
  {
    sample_tick = now;

    /* 单次转换超时时间设置为 2 ms，避免 ADC 异常时长时间卡住主循环。 */
    if ((HAL_ADC_Start(&hadc1) == HAL_OK) &&
        (HAL_ADC_PollForConversion(&hadc1, 2U) == HAL_OK))
    {
      sample_sum += HAL_ADC_GetValue(&hadc1);
      sample_count++;
    }
    HAL_ADC_Stop(&hadc1);
  }

  if ((now - window_tick) >= APP_SMOKE_WINDOW_MS)
  {
    window_tick = now;
    if (sample_count > 0U)
    {
      uint32_t average = sample_sum / sample_count;
      smoke_percent = (uint8_t)((average * 100U + 2047U) / 4095U);
    }
    sample_sum = 0U;
    sample_count = 0U;
  }
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  /* 初始化手写应用模块。
   * UART 桥接先启动接收中断，便于尽早看到 ESP-12F 的启动日志。
   */
  UART_Bridge_Init();
  OLED_Init();
  Smoke_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 主循环保持短小、快速、非阻塞：
     * UART_Bridge_Task() 搬运串口字节；
     * Smoke_Task() 做定时 ADC 采样；
     * ESP12F_Task() 推进 Wi-Fi/TCP/MQTT 状态机。
     */
    UART_Bridge_Task();
    Smoke_Task();

    /* LED 为低电平有效，蜂鸣器为高电平有效，因此这里把 GPIO 电平转换为逻辑状态。 */
    ESP12F_SetStatus(
        HAL_GPIO_ReadPin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN) == GPIO_PIN_RESET,
        HAL_GPIO_ReadPin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN) == GPIO_PIN_SET,
        smoke_percent);
    ESP12F_Task();

    {
      static uint32_t oled_update_tick;

      /* OLED 刷新频率低于主循环频率，避免频繁整屏 SPI 写入影响串口和 MQTT 时序。 */
      if ((HAL_GetTick() - oled_update_tick) >= 250U)
      {
        char line[24];
        oled_update_tick = HAL_GetTick();
        OLED_Clear(OLED_COLOR_BLACK);
        OLED_SetCursor(0U, 0U);
        OLED_WriteString(ESP12F_IsConnected() ? "ESP: CONNECT" : "ESP: WAIT", OLED_COLOR_WHITE);
        OLED_SetCursor(0U, 8U);
        snprintf(line, sizeof(line), "LED: %s", HAL_GPIO_ReadPin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN) == GPIO_PIN_RESET ? "ON" : "OFF");
        OLED_WriteString(line, OLED_COLOR_WHITE);
        OLED_SetCursor(0U, 16U);
        snprintf(line, sizeof(line), "BEEP: %s", HAL_GPIO_ReadPin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN) == GPIO_PIN_SET ? "ON" : "OFF");
        OLED_WriteString(line, OLED_COLOR_WHITE);
        OLED_SetCursor(0U, 24U);
        snprintf(line, sizeof(line), "SMOKE: %3u%%", smoke_percent);
        OLED_WriteString(line, OLED_COLOR_WHITE);
        OLED_UpdateScreen();
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
