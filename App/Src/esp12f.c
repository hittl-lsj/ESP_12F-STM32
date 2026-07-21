#include "esp12f.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "mqtt.h"
#include "usart.h"

typedef enum
{
  ESP_STATE_BOOT_DELAY,// 启动延迟状态
  ESP_STATE_SEND_AT,// 发送AT命令状态
   ESP_STATE_WAIT_AT,// 等待AT命令响应状态
  ESP_STATE_SEND_MODE, // 发送模式命令状态
  ESP_STATE_WAIT_MODE,// 等待模式命令响应状态
  ESP_STATE_SEND_WIFI, // 发送wifi命令状态
  ESP_STATE_WAIT_WIFI,// 等待wifi命令响应状态
  ESP_STATE_SEND_MUX, // 发送多路复用命令状态
  ESP_STATE_WAIT_MUX,// 等待多路复用命令响应状态
  ESP_STATE_SEND_TCP, // 发送TCP命令状态
  ESP_STATE_WAIT_TCP,// 等待TCP命令响应状态
  ESP_STATE_SEND_MQTT_CONNECT, // 发送MQTT连接命令状态
  ESP_STATE_WAIT_MQTT_CONNECT,// 等待MQTT连接命令响应状态
  ESP_STATE_SEND_MQTT_SUBSCRIBE, // 发送MQTT订阅命令状态
  ESP_STATE_WAIT_MQTT_SUBSCRIBE,// 等待MQTT订阅命令响应状态
  ESP_STATE_READY,// 就绪状态
  ESP_STATE_WAIT_SEND_PROMPT, // 等待发送提示状态
  ESP_STATE_WAIT_SEND_OK,// 等待发送成功状态
  ESP_STATE_RETRY_DELAY,// 重试延迟状态
} ESP_AutoState;

typedef enum
{
  IPD_SEARCH,
  IPD_LENGTH,
  IPD_PAYLOAD
} ESP_IpdState;

static char response_buffer[APP_ESP_RESPONSE_BUFFER_SIZE];
static uint16_t response_index;
static volatile uint8_t response_ok;
static volatile uint8_t response_error;
static volatile uint8_t response_connected;
static volatile uint8_t response_closed;
static volatile uint8_t response_prompt;
static volatile uint8_t response_send_ok;

static ESP_AutoState auto_state = ESP_STATE_BOOT_DELAY;
static ESP_AutoState state_after_send;
static uint32_t state_deadline;
static uint32_t status_upload_tick;
static uint32_t mqtt_last_activity_tick;
static uint8_t status_led_on;
static uint8_t status_buzzer_on;
static uint8_t status_smoke_percent;
static uint8_t tx_packet[APP_MQTT_PACKET_BUFFER_SIZE];
static uint16_t tx_packet_length;

static ESP_IpdState ipd_state;
static uint8_t ipd_prefix_index;
static uint32_t ipd_length;
static uint32_t ipd_received;

static uint8_t mqtt_rx_buffer[APP_MQTT_RX_BUFFER_SIZE];
static uint16_t mqtt_rx_index;
static uint32_t mqtt_rx_remaining;
static uint32_t mqtt_rx_multiplier;
static uint8_t mqtt_rx_length_bytes;
static uint8_t mqtt_rx_header_done;
static volatile uint8_t mqtt_connack_ok;
static volatile uint8_t mqtt_suback_ok;

static uint8_t ESP12F_DeadlineExpired(void)
{
  return ((int32_t)(HAL_GetTick() - state_deadline) >= 0) ? 1U : 0U;
}

static void ESP12F_ClearResponse(void)
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
}

static void ESP12F_ScheduleRetry(void)
{
  state_deadline = HAL_GetTick() + 5000U;// 5000ms 延时
  auto_state = ESP_STATE_RETRY_DELAY;// 重试延迟状态
}

static void ESP12F_SendCommand(const char *command, uint32_t timeout_ms,
                               ESP_AutoState wait_state)
{
  ESP12F_ClearResponse();
  HAL_UART_Transmit(&huart2, (uint8_t *)command, (uint16_t)strlen(command), 100U);
  state_deadline = HAL_GetTick() + timeout_ms;
  auto_state = wait_state;
}

static uint8_t ESP12F_StartPacketSend(uint16_t length, ESP_AutoState next_state)
{
  char command[32];
  if ((length == 0U) || (length > sizeof(tx_packet))) return 0U;
  tx_packet_length = length;
  state_after_send = next_state;
  snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n", (unsigned int)length);
  ESP12F_SendCommand(command, 3000U, ESP_STATE_WAIT_SEND_PROMPT);
  return 1U;
}

static void ESP12F_ApplyCommand(const uint8_t *payload, uint16_t length)
{
  if ((length == 6U) && (memcmp(payload, "LED ON", 6U) == 0))
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);
  else if ((length == 7U) && (memcmp(payload, "LED OFF", 7U) == 0))
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_SET);
  else if ((length == 9U) && (memcmp(payload, "BUZZER ON", 9U) == 0))
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_SET);
  else if ((length == 10U) && (memcmp(payload, "BUZZER OFF", 10U) == 0))
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_RESET);
}

static void MQTT_HandlePacket(const uint8_t *packet, uint16_t length,
                              uint16_t payload_index)
{
  uint8_t type = (uint8_t)(packet[0] >> 4);
  mqtt_last_activity_tick = HAL_GetTick();

  if ((type == 2U) && (payload_index <= length) &&
      ((uint16_t)(length - payload_index) >= 2U) &&
      (packet[payload_index + 1U] == 0U))
  {
    mqtt_connack_ok = 1U;
  }
  else if (type == 9U)
  {
    mqtt_suback_ok = 1U;
  }
  else if ((type == 3U) && (payload_index <= length) &&
           ((uint16_t)(length - payload_index) >= 2U))
  {
    uint16_t topic_length = (uint16_t)(((uint16_t)packet[payload_index] << 8) |
                                       packet[payload_index + 1U]);
    uint16_t topic_index = (uint16_t)(payload_index + 2U);
    uint16_t data_index;
    const char *command_topic = APP_MQTT_COMMAND_TOPIC;
    uint16_t command_topic_length = (uint16_t)strlen(command_topic);

    if (topic_length > (uint16_t)(length - topic_index))
      return;

    data_index = (uint16_t)(topic_index + topic_length);
    if (((packet[0] & 0x06U) == 0x06U) || ((packet[0] & 0x06U) == 0x04U))
      return;
    if (((packet[0] & 0x06U) != 0U) && ((uint16_t)(length - data_index) >= 2U))
      data_index = (uint16_t)(data_index + 2U);
    else if ((packet[0] & 0x06U) != 0U)
      return;

    if ((data_index <= length) && (topic_length == command_topic_length) &&
        (memcmp(&packet[topic_index], command_topic, topic_length) == 0))
      ESP12F_ApplyCommand(&packet[data_index], (uint16_t)(length - data_index));
  }
}

static void MQTT_OnRxByte(uint8_t data)
{
  if (mqtt_rx_index == 0U)
  {
    mqtt_rx_buffer[mqtt_rx_index++] = data;
    mqtt_rx_remaining = 0U;
    mqtt_rx_multiplier = 1U;
    mqtt_rx_length_bytes = 0U;
    mqtt_rx_header_done = 0U;
    return;
  }

  if (mqtt_rx_header_done == 0U)
  {
    if (mqtt_rx_length_bytes >= MQTT_MAX_REMAINING_LENGTH_BYTES)
    {
      mqtt_rx_index = 0U;
      return;
    }
    if (mqtt_rx_index >= sizeof(mqtt_rx_buffer)) { mqtt_rx_index = 0U; return; }
    mqtt_rx_buffer[mqtt_rx_index++] = data;
    mqtt_rx_remaining += (uint32_t)(data & 0x7FU) * mqtt_rx_multiplier;
    mqtt_rx_multiplier *= 128U;
    mqtt_rx_length_bytes++;
    if ((data & 0x80U) != 0U) return;
    mqtt_rx_header_done = 1U;
    if (mqtt_rx_remaining == 0U)
    {
      MQTT_HandlePacket(mqtt_rx_buffer, mqtt_rx_index, mqtt_rx_index);
      mqtt_rx_index = 0U;
    }
    return;
  }

  if (mqtt_rx_index >= sizeof(mqtt_rx_buffer)) { mqtt_rx_index = 0U; return; }
  mqtt_rx_buffer[mqtt_rx_index++] = data;
  if (mqtt_rx_index == (uint16_t)(1U + mqtt_rx_length_bytes + mqtt_rx_remaining))
  {
    MQTT_HandlePacket(mqtt_rx_buffer, mqtt_rx_index,
                      (uint16_t)(1U + mqtt_rx_length_bytes));
    mqtt_rx_index = 0U;
  }
}

static void ESP12F_ParseAtByte(uint8_t data)
{
  if (response_index >= (APP_ESP_RESPONSE_BUFFER_SIZE - 1U))
  {
    memmove(response_buffer, &response_buffer[1], APP_ESP_RESPONSE_BUFFER_SIZE - 2U);
    response_index = APP_ESP_RESPONSE_BUFFER_SIZE - 2U;
  }
  response_buffer[response_index++] = (char)data;
  response_buffer[response_index] = '\0';
  if (strstr(response_buffer, "OK\r\n") != NULL) response_ok = 1U;
  if ((strstr(response_buffer, "ERROR") != NULL) ||
      (strstr(response_buffer, "FAIL") != NULL)) response_error = 1U;
  if ((strstr(response_buffer, "CONNECT") != NULL) &&
      (strstr(response_buffer, "CLOSED") == NULL)) response_connected = 1U;
  if (strstr(response_buffer, "CLOSED") != NULL) response_closed = 1U;
  if (data == '>') response_prompt = 1U;
  if (strstr(response_buffer, "SEND OK") != NULL) response_send_ok = 1U;
}

void ESP12F_OnRxByte(uint8_t data)
{
  static const char prefix[] = "+IPD,";

  if (ipd_state == IPD_PAYLOAD)
  {
    MQTT_OnRxByte(data);
    if (++ipd_received >= ipd_length) ipd_state = IPD_SEARCH;
    return;
  }
  if (ipd_state == IPD_LENGTH)
  {
    if ((data >= '0') && (data <= '9')) ipd_length = ipd_length * 10U + (data - '0');
    else if ((data == ':') && (ipd_length > 0U))
    {
      ipd_received = 0U;
      ipd_state = IPD_PAYLOAD;
    }
    else ipd_state = IPD_SEARCH;
    return;
  }

  ESP12F_ParseAtByte(data);
  if (data == (uint8_t)prefix[ipd_prefix_index])
  {
    if (++ipd_prefix_index == (sizeof(prefix) - 1U))
    {
      ipd_prefix_index = 0U;
      ipd_length = 0U;
      ipd_state = IPD_LENGTH;
    }
  }
  else ipd_prefix_index = (data == '+') ? 1U : 0U;
}

uint8_t ESP12F_IsConnected(void)
{
  return (auto_state == ESP_STATE_READY) ? 1U : 0U;
}

void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t smoke_percent)
{
  status_led_on = led_on ? 1U : 0U;
  status_buzzer_on = buzzer_on ? 1U : 0U;
  status_smoke_percent = (smoke_percent > 100U) ? 100U : smoke_percent;
}

void ESP12F_Task(void)
{
  switch (auto_state)// ESP12F 状态机
  {
    case ESP_STATE_BOOT_DELAY:
      state_deadline = HAL_GetTick() + 2000U;// 2000ms 延时
      auto_state = ESP_STATE_RETRY_DELAY;// 重试延迟状态
      break;
    case ESP_STATE_RETRY_DELAY:
      if (ESP12F_DeadlineExpired()) auto_state = ESP_STATE_SEND_AT;// 重试超时，发送 AT 命令
      break;
    case ESP_STATE_SEND_AT:
      ESP12F_SendCommand("AT\r\n", 2000U, ESP_STATE_WAIT_AT);// 发送 AT 命令
      break;
    case ESP_STATE_WAIT_AT:
      if (response_ok) auto_state = ESP_STATE_SEND_MODE;// AT 命令执行成功，发送模式命令
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();// AT 命令执行失败，调度重试
      break;
    case ESP_STATE_SEND_MODE:
      ESP12F_SendCommand("AT+CWMODE=1\r\n", 3000U, ESP_STATE_WAIT_MODE);// 发送模式命令
      break;
    case ESP_STATE_WAIT_MODE:
      if (response_ok) auto_state = ESP_STATE_SEND_WIFI;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_WIFI:
      ESP12F_SendCommand("AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n",
                         30000U, ESP_STATE_WAIT_WIFI);
      break;
    case ESP_STATE_WAIT_WIFI:// 等待wifi连接状态
      if (response_ok) auto_state = ESP_STATE_SEND_MUX;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MUX:// 发送多路复用命令
      ESP12F_SendCommand("AT+CIPMUX=0\r\n", 3000U, ESP_STATE_WAIT_MUX);
      break;
    case ESP_STATE_WAIT_MUX:
      if (response_ok) auto_state = ESP_STATE_SEND_TCP;
      else if (response_error || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_TCP:
      ESP12F_SendCommand("AT+CIPSTART=\"TCP\",\"" APP_SERVER_IP "\"," APP_SERVER_PORT "\r\n",
                         10000U, ESP_STATE_WAIT_TCP);
      break;
    case ESP_STATE_WAIT_TCP:
      if (response_connected) auto_state = ESP_STATE_SEND_MQTT_CONNECT;
      else if (response_error || response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MQTT_CONNECT:
      mqtt_connack_ok = 0U;
      if (!ESP12F_StartPacketSend(
            MQTT_EncodeConnect(tx_packet, sizeof(tx_packet), APP_MQTT_CLIENT_ID,
                               APP_MQTT_USERNAME, APP_MQTT_PASSWORD,
                               APP_MQTT_KEEP_ALIVE_SECONDS),
            ESP_STATE_WAIT_MQTT_CONNECT)) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_WAIT_MQTT_CONNECT:
      if (mqtt_connack_ok) auto_state = ESP_STATE_SEND_MQTT_SUBSCRIBE;
      else if (response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_SEND_MQTT_SUBSCRIBE:
      mqtt_suback_ok = 0U;
      if (!ESP12F_StartPacketSend(
            MQTT_EncodeSubscribe(tx_packet, sizeof(tx_packet), 1U,
                                 APP_MQTT_COMMAND_TOPIC),
            ESP_STATE_WAIT_MQTT_SUBSCRIBE)) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_WAIT_MQTT_SUBSCRIBE:
      if (mqtt_suback_ok)
      {
        mqtt_last_activity_tick = HAL_GetTick();
        status_upload_tick = 0U;
        auto_state = ESP_STATE_READY;
      }
      else if (response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_READY:
      if (response_closed) ESP12F_ScheduleRetry();
      else if ((HAL_GetTick() - status_upload_tick) >= APP_STATUS_UPLOAD_INTERVAL_MS)
      {
        char payload[96];
        int length = snprintf(payload, sizeof(payload),
                              "{\"messageId\":\"%lu\",\"params\":{\"key\":\"smokeConcentration\",\"value\":%u}}",
                              (unsigned long)HAL_GetTick(),
                              (unsigned int)status_smoke_percent);
        uint16_t packet_length = (length > 0) ?
          MQTT_EncodePublish(tx_packet, sizeof(tx_packet), APP_MQTT_STATUS_TOPIC,
                             (uint8_t *)payload, (uint16_t)length) : 0U;
        if (ESP12F_StartPacketSend(packet_length, ESP_STATE_READY))
          status_upload_tick = HAL_GetTick();
      }
      else if ((HAL_GetTick() - mqtt_last_activity_tick) >=
               ((uint32_t)APP_MQTT_KEEP_ALIVE_SECONDS * 500U))
      {
        ESP12F_StartPacketSend(MQTT_EncodePingReq(tx_packet, sizeof(tx_packet)),
                               ESP_STATE_READY);
      }
      break;
    case ESP_STATE_WAIT_SEND_PROMPT:
      if (response_prompt)
      {
        response_prompt = 0U;
        response_send_ok = 0U;
        response_error = 0U;
        HAL_UART_Transmit(&huart2, tx_packet, tx_packet_length, 1000U);
        state_deadline = HAL_GetTick() + 5000U;
        auto_state = ESP_STATE_WAIT_SEND_OK;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    case ESP_STATE_WAIT_SEND_OK:
      if (response_send_ok)
      {
        mqtt_last_activity_tick = HAL_GetTick();
        state_deadline = HAL_GetTick() + 5000U;
        auto_state = state_after_send;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired()) ESP12F_ScheduleRetry();
      break;
    default:
      ESP12F_ScheduleRetry();
      break;
  }
}
