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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "esp12f.h"
#include "uart_bridge.h"

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
#if 0 /* Legacy implementation moved to App/Src. */
#define UART_BRIDGE_BUFFER_SIZE 256U
#define ESP_COMMAND_BUFFER_SIZE 64U
#define ESP_WIFI_SSID "Redmi Turbo 3"
#define ESP_WIFI_PASSWORD "11122222"
#define ESP_SERVER_IP "192.168.159.46"
#define ESP_SERVER_PORT "8080"

typedef enum
{
  ESP_STATE_BOOT_DELAY, ESP_STATE_SEND_AT, ESP_STATE_WAIT_AT,
  ESP_STATE_SEND_MODE, ESP_STATE_WAIT_MODE, ESP_STATE_SEND_WIFI,
  ESP_STATE_WAIT_WIFI, ESP_STATE_SEND_MUX, ESP_STATE_WAIT_MUX,
  ESP_STATE_SEND_TCP, ESP_STATE_WAIT_TCP, ESP_STATE_CONNECTED,
  ESP_STATE_RETRY_DELAY
} ESP_AutoState;

typedef struct
{
  uint8_t data[UART_BRIDGE_BUFFER_SIZE];// 数据缓冲区
  volatile uint16_t head;// 头指针
  volatile uint16_t tail;// 尾指针
  volatile uint32_t overflow_count;// 溢出计数
} UART_BridgeBuffer;

static uint8_t uart1_rx_byte;// UART1 接收字节
static uint8_t uart2_rx_byte;// UART2 接收字节
static UART_BridgeBuffer uart1_to_uart2;// UART1 到 UART2 的桥接缓冲区
static UART_BridgeBuffer uart2_to_uart1;// UART2 到 UART1 的桥接缓冲区

static char esp_command_buffer[ESP_COMMAND_BUFFER_SIZE];
static uint8_t esp_command_index;
static volatile uint8_t esp_response_ok;
static volatile uint8_t esp_response_error;
static volatile uint8_t esp_response_connected;
static volatile uint8_t esp_response_closed;
static ESP_AutoState esp_auto_state = ESP_STATE_BOOT_DELAY;
static uint32_t esp_state_deadline;
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if 0 /* Legacy implementation moved to App/Src. */
static void UART_BridgePush(UART_BridgeBuffer *buffer, uint8_t data)// 将数据推入桥接缓冲区
{
  uint16_t next = (uint16_t)((buffer->head + 1U) % UART_BRIDGE_BUFFER_SIZE);

  if (next == buffer->tail)
  {
    buffer->overflow_count++;
    return;
  }

  buffer->data[buffer->head] = data;
  buffer->head = next;
}

static void UART_BridgeForward(UART_BridgeBuffer *buffer,
                               UART_HandleTypeDef *destination)// 将桥接缓冲区中的数据转发到目标 UART
{
  uint8_t data;

  if (buffer->tail == buffer->head)
  {
    return;
  }

  data = buffer->data[buffer->tail];
  if (HAL_UART_Transmit(destination, &data, 1U, 10U) == HAL_OK)
  {
    buffer->tail = (uint16_t)((buffer->tail + 1U) % UART_BRIDGE_BUFFER_SIZE);
  }
}

static void ESP_ParseByte(uint8_t data)
{
  if (esp_command_index >= (ESP_COMMAND_BUFFER_SIZE - 1U))
  {
    memmove(esp_command_buffer, &esp_command_buffer[1],
            ESP_COMMAND_BUFFER_SIZE - 2U);
    esp_command_index = ESP_COMMAND_BUFFER_SIZE - 2U;
  }

  esp_command_buffer[esp_command_index++] = (char)data;
  esp_command_buffer[esp_command_index] = '\0';

  if (strstr(esp_command_buffer, "OK\r\n") != NULL) esp_response_ok = 1U;
  if (strstr(esp_command_buffer, "ERROR") != NULL ||
      strstr(esp_command_buffer, "FAIL") != NULL) esp_response_error = 1U;
  if (strstr(esp_command_buffer, "CONNECT") != NULL) esp_response_connected = 1U;
  if (strstr(esp_command_buffer, "CLOSED") != NULL) esp_response_closed = 1U;

  if (strstr(esp_command_buffer, "+IPD,6:LED ON") != NULL)
  {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    memset(esp_command_buffer, 0, sizeof(esp_command_buffer));
    esp_command_index = 0U;
  }
  else if (strstr(esp_command_buffer, "+IPD,7:LED OFF") != NULL)
  {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    memset(esp_command_buffer, 0, sizeof(esp_command_buffer));
    esp_command_index = 0U;
  }
}

static void ESP_SendCommand(const char *command, uint32_t timeout_ms,
                            ESP_AutoState wait_state)
{
  __disable_irq();
  memset(esp_command_buffer, 0, sizeof(esp_command_buffer));
  esp_command_index = 0U;
  esp_response_ok = 0U;
  esp_response_error = 0U;
  esp_response_connected = 0U;
  esp_response_closed = 0U;
  __enable_irq();
  HAL_UART_Transmit(&huart2, (uint8_t *)command, (uint16_t)strlen(command), 100U);
  esp_state_deadline = HAL_GetTick() + timeout_ms;
  esp_auto_state = wait_state;
}

static uint8_t ESP_DeadlineExpired(void)
{
  return ((int32_t)(HAL_GetTick() - esp_state_deadline) >= 0) ? 1U : 0U;
}

static void ESP_ScheduleRetry(void)
{
  esp_state_deadline = HAL_GetTick() + 5000U;
  esp_auto_state = ESP_STATE_RETRY_DELAY;
}

static void ESP_AutoConnectTask(void)
{
  switch (esp_auto_state)
  {
    case ESP_STATE_BOOT_DELAY:
      esp_state_deadline = HAL_GetTick() + 2000U;
      esp_auto_state = ESP_STATE_RETRY_DELAY;
      break;
    case ESP_STATE_RETRY_DELAY:
      if (ESP_DeadlineExpired()) esp_auto_state = ESP_STATE_SEND_AT;
      break;
    case ESP_STATE_SEND_AT:
      ESP_SendCommand("AT\r\n", 2000U, ESP_STATE_WAIT_AT);
      break;
    case ESP_STATE_WAIT_AT:
      if (esp_response_ok) esp_auto_state = ESP_STATE_SEND_MODE;
      else if (esp_response_error || ESP_DeadlineExpired()) ESP_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MODE:
      ESP_SendCommand("AT+CWMODE=1\r\n", 3000U, ESP_STATE_WAIT_MODE);
      break;
    case ESP_STATE_WAIT_MODE:
      if (esp_response_ok) esp_auto_state = ESP_STATE_SEND_WIFI;
      else if (esp_response_error || ESP_DeadlineExpired()) ESP_ScheduleRetry();
      break;
    case ESP_STATE_SEND_WIFI:
      ESP_SendCommand("AT+CWJAP=\"" ESP_WIFI_SSID "\",\"" ESP_WIFI_PASSWORD "\"\r\n",
                      30000U, ESP_STATE_WAIT_WIFI);
      break;
    case ESP_STATE_WAIT_WIFI:
      if (esp_response_ok) esp_auto_state = ESP_STATE_SEND_MUX;
      else if (esp_response_error || ESP_DeadlineExpired()) ESP_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MUX:
      ESP_SendCommand("AT+CIPMUX=0\r\n", 3000U, ESP_STATE_WAIT_MUX);
      break;
    case ESP_STATE_WAIT_MUX:
      if (esp_response_ok) esp_auto_state = ESP_STATE_SEND_TCP;
      else if (esp_response_error || ESP_DeadlineExpired()) ESP_ScheduleRetry();
      break;
    case ESP_STATE_SEND_TCP:
      ESP_SendCommand("AT+CIPSTART=\"TCP\",\"" ESP_SERVER_IP "\"," ESP_SERVER_PORT "\r\n",
                      10000U, ESP_STATE_WAIT_TCP);
      break;
    case ESP_STATE_WAIT_TCP:
      if (esp_response_connected) esp_auto_state = ESP_STATE_CONNECTED;
      else if (esp_response_error || esp_response_closed || ESP_DeadlineExpired()) ESP_ScheduleRetry();
      break;
    case ESP_STATE_CONNECTED:
      if (esp_response_closed) ESP_ScheduleRetry();
      break;
    default:
      ESP_ScheduleRetry();
      break;
  }
}
#endif

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
  /* USER CODE BEGIN 2 */
  UART_Bridge_Init();
#if 0 /* Legacy implementation moved to App/Src. */
  HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);// 启用 UART1 接收中断
  HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);// 启用 UART2 接收中断

#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if 0 /* Legacy implementation moved to App/Src. */
    UART_BridgeForward(&uart1_to_uart2, &huart2);// 将 UART1 到 UART2 的桥接缓冲区中的数据转发到 UART2
    UART_BridgeForward(&uart2_to_uart1, &huart1);// 将 UART2 到 UART1 的桥接缓冲区中的数据转发到 UART1
    ESP_AutoConnectTask();
#endif
    UART_Bridge_Task();
    ESP12F_Task();
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
}

/* USER CODE BEGIN 4 */
#if 0 /* Legacy implementation moved to App/Src. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    UART_BridgePush(&uart1_to_uart2, uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);
  }
  else if (huart->Instance == USART2)
  {
    ESP_ParseByte(uart2_rx_byte);
    UART_BridgePush(&uart2_to_uart1, uart2_rx_byte);
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);
  }
  else if (huart->Instance == USART2)
  {
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
  }
}

#endif
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
