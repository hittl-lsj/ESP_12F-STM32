#include "esp12f.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "usart.h"

typedef enum
{
  ESP_STATE_BOOT_DELAY,// 引导延迟状态
  ESP_STATE_SEND_AT,// 发送 AT 命令状态
  ESP_STATE_WAIT_AT,// 等待 AT 命令响应状态
  ESP_STATE_SEND_MODE,// 发送模式状态
  ESP_STATE_WAIT_MODE,// 等待模式响应状态
  ESP_STATE_SEND_WIFI,// 发送 WIFI 命令状态
  ESP_STATE_WAIT_WIFI,// 等待 WIFI 命令响应状态
  ESP_STATE_SEND_MUX,// 发送 MUX 命令状态
  ESP_STATE_WAIT_MUX,// 等待 MUX 命令响应状态
  ESP_STATE_SEND_TCP,// 发送 TCP 命令状态
  ESP_STATE_WAIT_TCP,// 等待 TCP 命令响应状态
  ESP_STATE_CONNECTED,// 已连接状态
  ESP_STATE_WAIT_SEND_PROMPT,
  ESP_STATE_WAIT_SEND_OK,
  ESP_STATE_RETRY_DELAY,// 重试延迟状态
} ESP_AutoState;

static char response_buffer[APP_ESP_RESPONSE_BUFFER_SIZE];
static uint8_t response_index;
static volatile uint8_t response_ok;
static volatile uint8_t response_error;
static volatile uint8_t response_connected;
static volatile uint8_t response_closed;
static volatile uint8_t response_prompt;
static volatile uint8_t response_send_ok;
static ESP_AutoState auto_state = ESP_STATE_BOOT_DELAY;
static uint32_t state_deadline;
static uint32_t status_upload_tick;
static uint8_t status_led_on;
static uint8_t status_buzzer_on;
static uint8_t status_noise_percent;
static char status_payload[48];

uint8_t ESP12F_IsConnected(void)
{
  return ((auto_state == ESP_STATE_CONNECTED) ||
          (auto_state == ESP_STATE_WAIT_SEND_PROMPT) ||
          (auto_state == ESP_STATE_WAIT_SEND_OK)) ? 1U : 0U;
}

void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t noise_percent)
{
  status_led_on = led_on ? 1U : 0U;
  status_buzzer_on = buzzer_on ? 1U : 0U;
  status_noise_percent = (noise_percent > 100U) ? 100U : noise_percent;
}

static uint8_t ESP12F_DeadlineExpired(void)
{
  return ((int32_t)(HAL_GetTick() - state_deadline) >= 0) ? 1U : 0U;
}

static void ESP12F_ScheduleRetry(void)
{
  state_deadline = HAL_GetTick() + 5000U;
  auto_state = ESP_STATE_RETRY_DELAY;
}

static void ESP12F_SendCommand(const char *command, uint32_t timeout_ms,
                               ESP_AutoState wait_state)
{
  __disable_irq();
  memset(response_buffer, 0, sizeof(response_buffer));
  response_index = 0U;
  response_ok = 0U;
  response_error = 0U;
  response_connected = 0U;
  response_closed = 0U;
  response_prompt = 0U;
  response_send_ok = 0U;
  __enable_irq();

  HAL_UART_Transmit(&huart2, (uint8_t *)command, (uint16_t)strlen(command), 100U);
  state_deadline = HAL_GetTick() + timeout_ms;
  auto_state = wait_state;
}

void ESP12F_OnRxByte(uint8_t data)
{
  if (response_index >= (APP_ESP_RESPONSE_BUFFER_SIZE - 1U))
  {
    memmove(response_buffer, &response_buffer[1], APP_ESP_RESPONSE_BUFFER_SIZE - 2U);
    response_index = APP_ESP_RESPONSE_BUFFER_SIZE - 2U;
  }

  response_buffer[response_index++] = (char)data;
  response_buffer[response_index] = '\0';

  if (strstr(response_buffer, "OK\r\n") != NULL) response_ok = 1U;
  if (strstr(response_buffer, "ERROR") != NULL ||
      strstr(response_buffer, "FAIL") != NULL) response_error = 1U;
  if (strstr(response_buffer, "CONNECT") != NULL) response_connected = 1U;
  if (strstr(response_buffer, "CLOSED") != NULL) response_closed = 1U;
  if (data == '>') response_prompt = 1U;
  if (strstr(response_buffer, "SEND OK") != NULL) response_send_ok = 1U;

  if (strstr(response_buffer, "+IPD,6:LED ON") != NULL)
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, 0);
    memset(response_buffer, 0, sizeof(response_buffer));
    response_index = 0U;
  }
  else if (strstr(response_buffer, "+IPD,7:LED OFF") != NULL)
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, 1);
    memset(response_buffer, 0, sizeof(response_buffer));
    response_index = 0U;
  }
  else if (strstr(response_buffer, "+IPD,9:BUZZER ON") != NULL)
  {
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_SET);
    memset(response_buffer, 0, sizeof(response_buffer));
    response_index = 0U;
  }
  else if (strstr(response_buffer, "+IPD,10:BUZZER OFF") != NULL)
  {
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_RESET);
    memset(response_buffer, 0, sizeof(response_buffer));
    response_index = 0U;
  }
}

void ESP12F_Task(void)
{
  switch (auto_state)
  {
    case ESP_STATE_BOOT_DELAY:
      state_deadline = HAL_GetTick() + 2000U;
      auto_state = ESP_STATE_RETRY_DELAY;
      break;
    case ESP_STATE_RETRY_DELAY:
      if (ESP12F_DeadlineExpired()) auto_state = ESP_STATE_SEND_AT;
      break;
    case ESP_STATE_SEND_AT:
      ESP12F_SendCommand("AT\r\n", 2000U, ESP_STATE_WAIT_AT);
      break;
    case ESP_STATE_WAIT_AT:
      if (response_ok) auto_state = ESP_STATE_SEND_MODE;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MODE:
      ESP12F_SendCommand("AT+CWMODE=1\r\n", 3000U, ESP_STATE_WAIT_MODE);
      break;
    case ESP_STATE_WAIT_MODE:
      if (response_ok) auto_state = ESP_STATE_SEND_WIFI;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_WIFI:
      ESP12F_SendCommand("AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n",
                         30000U, ESP_STATE_WAIT_WIFI);
      break;
    case ESP_STATE_WAIT_WIFI:
      if (response_ok) auto_state = ESP_STATE_SEND_MUX;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MUX:
      ESP12F_SendCommand("AT+CIPMUX=0\r\n", 3000U, ESP_STATE_WAIT_MUX);
      break;
    case ESP_STATE_WAIT_MUX:
      if (response_ok) auto_state = ESP_STATE_SEND_TCP;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_TCP:
      ESP12F_SendCommand("AT+CIPSTART=\"TCP\",\"" APP_SERVER_IP "\"," APP_SERVER_PORT "\r\n",
                         10000U, ESP_STATE_WAIT_TCP);// 发送 TCP 命令
      break;
    case ESP_STATE_WAIT_TCP:
      if (response_connected)
      {
        status_upload_tick = HAL_GetTick();
        auto_state = ESP_STATE_CONNECTED;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();// 重试延迟状态
      break;
    case ESP_STATE_CONNECTED:
      if (response_closed)
      {
        ESP12F_ScheduleRetry();// 重试延迟状态
      }
      else if ((HAL_GetTick() - status_upload_tick) >= APP_STATUS_UPLOAD_INTERVAL_MS)
      {
        char command[32];
        int payload_length = snprintf(status_payload, sizeof(status_payload),
                                      "STATUS LED=%s BUZZER=%s NOISE=%u\r\n",
                                      status_led_on ? "ON" : "OFF",
                                      status_buzzer_on ? "ON" : "OFF",
                                      (unsigned int)status_noise_percent);
        if ((payload_length > 0) && (payload_length < (int)sizeof(status_payload)))
        {
          snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n",
                   (unsigned int)payload_length);
          ESP12F_SendCommand(command, 3000U, ESP_STATE_WAIT_SEND_PROMPT);
        }
      }
      break;
    case ESP_STATE_WAIT_SEND_PROMPT:
      if (response_prompt)
      {
        response_prompt = 0U;
        response_send_ok = 0U;
        response_error = 0U;
        HAL_UART_Transmit(&huart2, (uint8_t *)status_payload,
                          (uint16_t)strlen(status_payload), 100U);
        state_deadline = HAL_GetTick() + 5000U;
        auto_state = ESP_STATE_WAIT_SEND_OK;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;
    case ESP_STATE_WAIT_SEND_OK:
      if (response_send_ok)
      {
        status_upload_tick = HAL_GetTick();
        auto_state = ESP_STATE_CONNECTED;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;
    default:
      ESP12F_ScheduleRetry();// 重试延迟状态
      break;
  }
}
