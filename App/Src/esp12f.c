#include "esp12f.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "mqtt.h"
#include "usart.h"

/* ESP-12F 自动连接状态机。
 *
 * 状态机按“发送命令 -> 等待响应”的节奏推进：
 * 1. AT 检查模块是否在线；
 * 2. 设置 STA 模式；
 * 3. 连接 Wi-Fi；
 * 4. 建立 TCP 连接；
 * 5. 发送 MQTT CONNECT；
 * 6. 订阅云端下行 Topic；
 * 7. READY 后周期性发布属性和发送 PINGREQ。
 *
 * 所有等待都通过 HAL_GetTick() 做超时判断，主循环不会被长时间阻塞。
 */
typedef enum
{
  ESP_STATE_BOOT_DELAY,             /* 上电后给 ESP-12F 预留启动时间。 */
  ESP_STATE_SEND_AT,                /* 发送 AT，确认 AT 固件可响应。 */
  ESP_STATE_WAIT_AT,                /* 等待 AT 返回 OK。 */
  ESP_STATE_SEND_MODE,              /* 发送 AT+CWMODE=1，设置为 Station 模式。 */
  ESP_STATE_WAIT_MODE,              /* 等待 Wi-Fi 模式设置完成。 */
  ESP_STATE_SEND_WIFI,              /* 发送 AT+CWJAP，连接路由器。 */
  ESP_STATE_WAIT_WIFI,              /* 等待 Wi-Fi 入网结果。 */
  ESP_STATE_SEND_MUX,               /* 发送 AT+CIPMUX=0，使用单 TCP 连接模式。 */
  ESP_STATE_WAIT_MUX,               /* 等待单连接模式设置完成。 */
  ESP_STATE_SEND_TCP,               /* 发送 AT+CIPSTART，连接 MQTT Broker。 */
  ESP_STATE_WAIT_TCP,               /* 等待 TCP CONNECT 文本响应。 */
  ESP_STATE_SEND_MQTT_CONNECT,      /* 通过 AT+CIPSEND 发送 MQTT CONNECT 报文。 */
  ESP_STATE_WAIT_MQTT_CONNECT,      /* 等待 MQTT CONNACK。 */
  ESP_STATE_SEND_MQTT_SUBSCRIBE,    /* 发送 MQTT SUBSCRIBE，订阅属性设置 Topic。 */
  ESP_STATE_WAIT_MQTT_SUBSCRIBE,    /* 等待 MQTT SUBACK。 */
  ESP_STATE_READY,                  /* MQTT 已连接，可上传属性并接收下行。 */
  ESP_STATE_WAIT_SEND_PROMPT,       /* 等待 AT+CIPSEND 返回 '>' 提示符。 */
  ESP_STATE_WAIT_SEND_OK,           /* 等待 ESP 返回 SEND OK。 */
  ESP_STATE_RETRY_DELAY,            /* 失败后的统一延迟重试状态。 */
} ESP_AutoState;

/* ESP AT 固件收到 TCP 数据后，会输出：
 * +IPD,<length>:<binary payload>
 * 下面的枚举用于从普通 AT 文本流中识别 +IPD 前缀、长度和实际 MQTT 数据。
 */
typedef enum
{
  IPD_SEARCH,   /* 正在普通文本流中搜索 "+IPD," 前缀。 */
  IPD_LENGTH,   /* 已识别前缀，正在读取十进制长度。 */
  IPD_PAYLOAD   /* 正在把 MQTT 二进制载荷逐字节交给 MQTT 解析器。 */
} ESP_IpdState;

typedef enum
{
  ESP_PROPERTY_ABSENT,
  ESP_PROPERTY_PRESENT,
  ESP_PROPERTY_INVALID
} ESP_PropertyStatus;

/* AT 文本响应滑动窗口。
 * 缓冲区只保存最近一小段文本，足够匹配 OK/ERROR/CONNECT/SEND OK 等关键字。
 */
static char response_buffer[APP_ESP_RESPONSE_BUFFER_SIZE];
static uint16_t response_index;
static volatile uint8_t response_ok;
static volatile uint8_t response_error;
static volatile uint8_t response_connected;
static volatile uint8_t response_closed;
static volatile uint8_t response_prompt;
static volatile uint8_t response_send_ok;

/* 自动连接状态机运行时上下文。 */
static ESP_AutoState auto_state = ESP_STATE_BOOT_DELAY;
static ESP_AutoState state_after_send;       /* 通用发送流程完成后要回到的业务状态。 */
static uint32_t state_deadline;              /* 当前等待状态的超时时刻。 */
static uint32_t status_upload_tick;          /* 上一次属性上报时间。 */
static uint32_t mqtt_last_activity_tick;     /* 上一次 MQTT 收发活动时间，用于保活。 */
static uint8_t status_led_on;                /* 保留状态：当前 LED 是否点亮。 */
static uint8_t status_buzzer_on;             /* 保留状态：当前蜂鸣器是否打开。 */
static uint8_t status_smoke_percent;         /* 下一次上报的烟雾百分比。 */
static uint8_t smoke_alarm_limit = APP_SMOKE_ALARM_LIMIT_DEFAULT;
static uint8_t buzzer_manual_override;
static uint8_t tx_packet[APP_MQTT_PACKET_BUFFER_SIZE];
static uint16_t tx_packet_length;
static uint8_t set_reply_pending;
static char set_reply_message_id[12] = "0";
static char set_reply_code[8] = "000000";
static char set_reply_message[32] = "";

/* +IPD 帧解析状态。 */
static ESP_IpdState ipd_state;
static uint8_t ipd_prefix_index;
static uint32_t ipd_length;
static uint32_t ipd_received;

/* MQTT 接收报文解析状态。
 * MQTT Remaining Length 是变长编码，所以接收端需要先逐字节解出固定头长度，
 * 再按剩余长度累计完整报文。
 */
static uint8_t mqtt_rx_buffer[APP_MQTT_RX_BUFFER_SIZE];
static uint16_t mqtt_rx_index;
static uint32_t mqtt_rx_remaining;
static uint32_t mqtt_rx_multiplier;
static uint8_t mqtt_rx_length_bytes;
static uint8_t mqtt_rx_header_done;
static volatile uint8_t mqtt_connack_ok;
static volatile uint8_t mqtt_suback_ok;

/* 使用有符号差值判断超时，可正确处理 HAL_GetTick() 回绕。 */
static uint8_t ESP12F_DeadlineExpired(void)
{
  return ((int32_t)(HAL_GetTick() - state_deadline) >= 0) ? 1U : 0U;
}

/* 清空 AT 响应缓冲和关键响应标志。
 * 发送新命令前调用，避免上一条命令的 OK/ERROR 影响当前等待状态。
 */
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

/* 进入统一重试路径。
 * 不在失败处立即重发，可以给 ESP、路由器和 Broker 留出恢复时间。
 */
static void ESP12F_ScheduleRetry(void)
{
  state_deadline = HAL_GetTick() + 5000U;
  auto_state = ESP_STATE_RETRY_DELAY;
}

/* 发送一条 AT 文本命令，并切换到指定等待状态。 */
static void ESP12F_SendCommand(const char *command, uint32_t timeout_ms,
                               ESP_AutoState wait_state)
{
  ESP12F_ClearResponse();
  HAL_UART_Transmit(&huart2, (uint8_t *)command, (uint16_t)strlen(command), 100U);
  state_deadline = HAL_GetTick() + timeout_ms;
  auto_state = wait_state;
}

/* 使用 ESP8266 AT+CIPSEND 发送一段 MQTT 二进制报文。
 * length 是 tx_packet 中已编码好的报文长度；next_state 是 SEND OK 后继续等待的状态。
 */
static uint8_t ESP12F_StartPacketSend(uint16_t length, ESP_AutoState next_state)
{
  char command[32];

  if ((length == 0U) || (length > sizeof(tx_packet)))
  {
    return 0U;
  }

  tx_packet_length = length;
  state_after_send = next_state;
  snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n", (unsigned int)length);
  ESP12F_SendCommand(command, 3000U, ESP_STATE_WAIT_SEND_PROMPT);
  return 1U;
}

static uint8_t ESP12F_IsJsonSpace(char value)
{
  return ((value == ' ') || (value == '\r') || (value == '\n') || (value == '\t')) ? 1U : 0U;
}

static void ESP12F_CopyText(char *destination, uint16_t destination_size,
                            const char *source)
{
  uint16_t index = 0U;

  if (destination_size == 0U)
  {
    return;
  }

  while ((source[index] != '\0') && (index < (uint16_t)(destination_size - 1U)))
  {
    destination[index] = source[index];
    index++;
  }
  destination[index] = '\0';
}

static const char *ESP12F_FindJsonField(const char *json, const char *field)
{
  char token[24];

  snprintf(token, sizeof(token), "\"%s\"", field);
  return strstr(json, token);
}

static const char *ESP12F_FindJsonValueStart(const char *json, const char *field)
{
  const char *position = ESP12F_FindJsonField(json, field);

  if (position == NULL)
  {
    return NULL;
  }

  position = strchr(position, ':');
  if (position == NULL)
  {
    return NULL;
  }

  position++;
  while (ESP12F_IsJsonSpace(*position) != 0U)
  {
    position++;
  }

  return position;
}

static uint8_t ESP12F_CopyJsonValue(const char *json, const char *field,
                                    char *destination, uint16_t destination_size)
{
  const char *position = ESP12F_FindJsonValueStart(json, field);
  uint16_t index = 0U;
  char terminator = ',';

  if ((position == NULL) || (destination_size == 0U))
  {
    return 0U;
  }

  if (*position == '"')
  {
    position++;
    terminator = '"';
  }

  while ((*position != '\0') && (*position != terminator) &&
         (*position != '}') && (*position != ']') &&
         (index < (uint16_t)(destination_size - 1U)))
  {
    if ((terminator != '"') && (ESP12F_IsJsonSpace(*position) != 0U))
    {
      break;
    }
    destination[index++] = *position++;
  }

  destination[index] = '\0';
  return (index > 0U) ? 1U : 0U;
}

static uint8_t ESP12F_ParseJsonUint(const char *json, const char *field,
                                    uint32_t *value)
{
  const char *position = ESP12F_FindJsonValueStart(json, field);
  char *end_position;
  unsigned long parsed_value;

  if ((position == NULL) || (value == NULL))
  {
    return 0U;
  }

  if (*position == '"')
  {
    position++;
  }

  parsed_value = strtoul(position, &end_position, 10);
  if (end_position == position)
  {
    return 0U;
  }

  *value = (uint32_t)parsed_value;
  return 1U;
}

static ESP_PropertyStatus ESP12F_ParsePropertyUint(const char *json, const char *property_key,
                                                   uint32_t *value)
{
  char token[24];
  const char *property;

  snprintf(token, sizeof(token), "\"%s\"", property_key);
  property = strstr(json, token);
  if (property == NULL)
  {
    return ESP_PROPERTY_ABSENT;
  }

  if (ESP12F_ParseJsonUint(property, "value", value) == 0U)
  {
    return ESP_PROPERTY_INVALID;
  }

  return ESP_PROPERTY_PRESENT;
}

static void ESP12F_QueueSetReply(const char *message_id, const char *code,
                                 const char *message)
{
  ESP12F_CopyText(set_reply_message_id, sizeof(set_reply_message_id), message_id);
  ESP12F_CopyText(set_reply_code, sizeof(set_reply_code), code);
  ESP12F_CopyText(set_reply_message, sizeof(set_reply_message), message);
  set_reply_pending = 1U;
}

static uint8_t ESP12F_SendPendingSetReply(void)
{
  char payload[112];
  int length = snprintf(payload, sizeof(payload),
                        "{\"code\":\"%s\",\"message\":\"%s\",\"messageId\":\"%s\",\"data\":[]}",
                        set_reply_code, set_reply_message, set_reply_message_id);
  uint16_t packet_length;

  if ((length <= 0) || (length >= (int)sizeof(payload)))
  {
    return 0U;
  }

  packet_length = MQTT_EncodePublish(tx_packet, sizeof(tx_packet), APP_MQTT_COMMAND_REPLY_TOPIC,
                                     (uint8_t *)payload, (uint16_t)length);

  return ESP12F_StartPacketSend(packet_length, ESP_STATE_READY);
}

static void ESP12F_UpdateAlarmOutput(void)
{
  if (buzzer_manual_override != 0U)
  {
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN,
                      status_buzzer_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return;
  }

  if (status_smoke_percent >= smoke_alarm_limit)
  {
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_SET);
    status_buzzer_on = 1U;
  }
  else
  {
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_RESET);
    status_buzzer_on = 0U;
  }
}

static uint8_t ESP12F_ApplyOperationCode(uint32_t operation_code)
{
  switch (operation_code)
  {
    case 1UL:
      HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);
      status_led_on = 1U;
      break;

    case 2UL:
      HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_SET);
      status_led_on = 0U;
      break;

    case 3UL:
      buzzer_manual_override = 1U;
      status_buzzer_on = 1U;
      HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_SET);
      break;

    case 4UL:
      buzzer_manual_override = 1U;
      status_buzzer_on = 0U;
      HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_RESET);
      break;

    case 5UL:
      buzzer_manual_override = 0U;
      ESP12F_UpdateAlarmOutput();
      break;

    default:
      return 0U;
  }

  return 1U;
}

static void ESP12F_ApplyPropertySetJson(const uint8_t *payload, uint16_t length)
{
  static char json[APP_MQTT_RX_BUFFER_SIZE + 1U];
  char message_id[12] = "0";
  uint32_t limit_value;
  uint32_t operation_code;
  ESP_PropertyStatus limit_status;
  ESP_PropertyStatus operation_status;

  if (length >= sizeof(json))
  {
    ESP12F_QueueSetReply(message_id, "100001", "payload too large");
    return;
  }

  memcpy(json, payload, length);
  json[length] = '\0';
  (void)ESP12F_CopyJsonValue(json, "messageId", message_id, sizeof(message_id));

  limit_status = ESP12F_ParsePropertyUint(json, "dbmLimit", &limit_value);
  operation_status = ESP12F_ParsePropertyUint(json, "operationCode", &operation_code);

  if ((limit_status == ESP_PROPERTY_ABSENT) &&
      (operation_status == ESP_PROPERTY_ABSENT))
  {
    ESP12F_QueueSetReply(message_id, "100001", "unsupported key");
    return;
  }

  if ((limit_status == ESP_PROPERTY_INVALID) ||
      (operation_status == ESP_PROPERTY_INVALID) ||
      ((limit_status == ESP_PROPERTY_PRESENT) && (limit_value > 100UL)) ||
      ((operation_status == ESP_PROPERTY_PRESENT) &&
       ((operation_code < 1UL) || (operation_code > 5UL))))
  {
    ESP12F_QueueSetReply(message_id, "100001", "invalid value");
    return;
  }

  if (limit_status == ESP_PROPERTY_PRESENT)
  {
    smoke_alarm_limit = (uint8_t)limit_value;
    buzzer_manual_override = 0U;
    ESP12F_UpdateAlarmOutput();
  }

  if (operation_status == ESP_PROPERTY_PRESENT)
  {
    (void)ESP12F_ApplyOperationCode(operation_code);
  }

  ESP12F_QueueSetReply(message_id, "000000", "");
}

/* 执行云端下行命令。
 * 保留本地 MQTT 测试阶段使用的纯文本命令，同时支持格物 property/set JSON。
 */
static void ESP12F_ApplyCommand(const uint8_t *payload, uint16_t length)
{
  if ((length == 6U) && (memcmp(payload, "LED ON", 6U) == 0))
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);
    status_led_on = 1U;
  }
  else if ((length == 7U) && (memcmp(payload, "LED OFF", 7U) == 0))
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_SET);
    status_led_on = 0U;
  }
  else if ((length == 9U) && (memcmp(payload, "BUZZER ON", 9U) == 0))
  {
    buzzer_manual_override = 1U;
    status_buzzer_on = 1U;
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_SET);
  }
  else if ((length == 10U) && (memcmp(payload, "BUZZER OFF", 10U) == 0))
  {
    buzzer_manual_override = 1U;
    status_buzzer_on = 0U;
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, GPIO_PIN_RESET);
  }
  else
  {
    ESP12F_ApplyPropertySetJson(payload, length);
  }
}

/* 处理一帧完整 MQTT 报文。
 *
 * packet[0] 的高 4 位是 MQTT 报文类型：
 * 2 = CONNACK，3 = PUBLISH，9 = SUBACK。
 *
 * payload_index 指向 MQTT 固定头之后的位置。对 PUBLISH 来说，这里先是 Topic 长度；
 * 对 CONNACK/SUBACK 来说，这里就是可变头或载荷起点。
 */
static void MQTT_HandlePacket(const uint8_t *packet, uint16_t length,
                              uint16_t payload_index)
{
  uint8_t type = (uint8_t)(packet[0] >> 4);

  mqtt_last_activity_tick = HAL_GetTick();

  if ((type == 2U) && (payload_index <= length) &&
      ((uint16_t)(length - payload_index) >= 2U) &&
      (packet[payload_index + 1U] == 0U))
  {
    /* CONNACK 返回码为 0 表示连接被 Broker 接受。 */
    mqtt_connack_ok = 1U;
  }
  else if (type == 9U)
  {
    /* 当前只订阅一个 Topic，收到 SUBACK 即认为订阅阶段完成。 */
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
    {
      return;
    }

    data_index = (uint16_t)(topic_index + topic_length);

    /* PUBLISH 低 4 位含 DUP/QoS/RETAIN。这里只接受 QoS0 或 QoS1：
     * QoS2 和保留的 QoS3 不处理；QoS1 需要跳过 2 字节 Packet Identifier。
     */
    if (((packet[0] & 0x06U) == 0x06U) || ((packet[0] & 0x06U) == 0x04U))
    {
      return;
    }
    if (((packet[0] & 0x06U) != 0U) && ((uint16_t)(length - data_index) >= 2U))
    {
      data_index = (uint16_t)(data_index + 2U);
    }
    else if ((packet[0] & 0x06U) != 0U)
    {
      return;
    }

    if ((data_index <= length) && (topic_length == command_topic_length) &&
        (memcmp(&packet[topic_index], command_topic, topic_length) == 0))
    {
      ESP12F_ApplyCommand(&packet[data_index], (uint16_t)(length - data_index));
    }
  }
}

/* MQTT 字节流接收器。
 *
 * ESP 的 +IPD 帧可能刚好包含一帧 MQTT，也可能以后扩展为连续多帧。
 * 因此这里按 MQTT 固定头格式逐字节累积：
 * - 第 1 字节：报文类型和标志；
 * - 后续 1-4 字节：Remaining Length 变长字段；
 * - 剩余字节：可变头和载荷。
 */
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
    if (mqtt_rx_index >= sizeof(mqtt_rx_buffer))
    {
      mqtt_rx_index = 0U;
      return;
    }

    mqtt_rx_buffer[mqtt_rx_index++] = data;
    mqtt_rx_remaining += (uint32_t)(data & 0x7FU) * mqtt_rx_multiplier;
    mqtt_rx_multiplier *= 128U;
    mqtt_rx_length_bytes++;

    if ((data & 0x80U) != 0U)
    {
      return;
    }

    mqtt_rx_header_done = 1U;
    if (mqtt_rx_remaining == 0U)
    {
      MQTT_HandlePacket(mqtt_rx_buffer, mqtt_rx_index, mqtt_rx_index);
      mqtt_rx_index = 0U;
    }
    return;
  }

  if (mqtt_rx_index >= sizeof(mqtt_rx_buffer))
  {
    mqtt_rx_index = 0U;
    return;
  }

  mqtt_rx_buffer[mqtt_rx_index++] = data;
  if (mqtt_rx_index == (uint16_t)(1U + mqtt_rx_length_bytes + mqtt_rx_remaining))
  {
    MQTT_HandlePacket(mqtt_rx_buffer, mqtt_rx_index,
                      (uint16_t)(1U + mqtt_rx_length_bytes));
    mqtt_rx_index = 0U;
  }
}

/* 解析普通 AT 文本响应。
 * response_buffer 是一个滑动窗口：满了就丢掉最早的 1 字节，保留最近文本用于 strstr 匹配。
 */
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

  /* 已进入 +IPD 载荷区时，所有字节都是 MQTT 二进制数据，不能再按 AT 文本解析。 */
  if (ipd_state == IPD_PAYLOAD)
  {
    MQTT_OnRxByte(data);
    if (++ipd_received >= ipd_length)
    {
      ipd_state = IPD_SEARCH;
    }
    return;
  }

  /* 读取 +IPD 后面的十进制长度，遇到 ':' 后进入载荷区。 */
  if (ipd_state == IPD_LENGTH)
  {
    if ((data >= '0') && (data <= '9'))
    {
      ipd_length = ipd_length * 10U + (data - '0');
    }
    else if ((data == ':') && (ipd_length > 0U))
    {
      ipd_received = 0U;
      ipd_state = IPD_PAYLOAD;
    }
    else
    {
      ipd_state = IPD_SEARCH;
    }
    return;
  }

  ESP12F_ParseAtByte(data);

  /* 在普通文本流中匹配 "+IPD," 前缀。匹配失败时，如果当前字节是 '+',
   * 立即把它当作新前缀的第 1 个字节，避免漏掉相邻的 +IPD。
   */
  if (data == (uint8_t)prefix[ipd_prefix_index])
  {
    if (++ipd_prefix_index == (sizeof(prefix) - 1U))
    {
      ipd_prefix_index = 0U;
      ipd_length = 0U;
      ipd_state = IPD_LENGTH;
    }
  }
  else
  {
    ipd_prefix_index = (data == '+') ? 1U : 0U;
  }
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
  ESP12F_UpdateAlarmOutput();

  (void)status_led_on;
  (void)status_buzzer_on;
}

void ESP12F_Task(void)
{
  switch (auto_state)
  {
    case ESP_STATE_BOOT_DELAY:
      /* 上电后 ESP-12F 需要时间启动 AT 固件。复用重试延时状态做初始等待。 */
      state_deadline = HAL_GetTick() + 2000U;
      auto_state = ESP_STATE_RETRY_DELAY;
      break;

    case ESP_STATE_RETRY_DELAY:
      if (ESP12F_DeadlineExpired())
      {
        auto_state = ESP_STATE_SEND_AT;
      }
      break;

    case ESP_STATE_SEND_AT:
      ESP12F_SendCommand("AT\r\n", 2000U, ESP_STATE_WAIT_AT);
      break;

    case ESP_STATE_WAIT_AT:
      if (response_ok)
      {
        auto_state = ESP_STATE_SEND_MODE;
      }
      else if (response_error || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_MODE:
      ESP12F_SendCommand("AT+CWMODE=1\r\n", 3000U, ESP_STATE_WAIT_MODE);
      break;

    case ESP_STATE_WAIT_MODE:
      if (response_ok)
      {
        auto_state = ESP_STATE_SEND_WIFI;
      }
      else if (response_error || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_WIFI:
      /* 使用 app_secrets.h 中配置的 Wi-Fi SSID 和密码连接路由器。 */
      ESP12F_SendCommand("AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n",
                         30000U, ESP_STATE_WAIT_WIFI);
      break;

    case ESP_STATE_WAIT_WIFI:
      if (response_ok)
      {
        auto_state = ESP_STATE_SEND_MUX;
      }
      else if (response_error || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_MUX:
      /* CIPMUX=0 表示单连接模式，后续 CIPSEND 不需要带连接 ID。 */
      ESP12F_SendCommand("AT+CIPMUX=0\r\n", 3000U, ESP_STATE_WAIT_MUX);
      break;

    case ESP_STATE_WAIT_MUX:
      if (response_ok)
      {
        auto_state = ESP_STATE_SEND_TCP;
      }
      else if (response_error || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_TCP:
      ESP12F_SendCommand("AT+CIPSTART=\"TCP\",\"" APP_SERVER_IP "\"," APP_SERVER_PORT "\r\n",
                         10000U, ESP_STATE_WAIT_TCP);
      break;

    case ESP_STATE_WAIT_TCP:
      if (response_connected)
      {
        auto_state = ESP_STATE_SEND_MQTT_CONNECT;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_MQTT_CONNECT:
      mqtt_connack_ok = 0U;
      if (!ESP12F_StartPacketSend(
            MQTT_EncodeConnect(tx_packet, sizeof(tx_packet), APP_MQTT_CLIENT_ID,
                               APP_MQTT_USERNAME, APP_MQTT_PASSWORD,
                               APP_MQTT_KEEP_ALIVE_SECONDS),
            ESP_STATE_WAIT_MQTT_CONNECT))
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_WAIT_MQTT_CONNECT:
      if (mqtt_connack_ok)
      {
        auto_state = ESP_STATE_SEND_MQTT_SUBSCRIBE;
      }
      else if (response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_SEND_MQTT_SUBSCRIBE:
      mqtt_suback_ok = 0U;
      if (!ESP12F_StartPacketSend(
            MQTT_EncodeSubscribe(tx_packet, sizeof(tx_packet), 1U,
                                 APP_MQTT_COMMAND_TOPIC),
            ESP_STATE_WAIT_MQTT_SUBSCRIBE))
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_WAIT_MQTT_SUBSCRIBE:
      if (mqtt_suback_ok)
      {
        mqtt_last_activity_tick = HAL_GetTick();
        status_upload_tick = 0U;
        auto_state = ESP_STATE_READY;
      }
      else if (response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_READY:
      if (response_closed)
      {
        ESP12F_ScheduleRetry();
      }
      else if (set_reply_pending)
      {
        if (ESP12F_SendPendingSetReply())
        {
          set_reply_pending = 0U;
        }
      }
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
        {
          status_upload_tick = HAL_GetTick();
        }
      }
      else if ((HAL_GetTick() - mqtt_last_activity_tick) >=
               ((uint32_t)APP_MQTT_KEEP_ALIVE_SECONDS * 500U))
      {
        /* 约半个 keepalive 周期没有任何 MQTT 活动时主动 ping，提前维持链路。 */
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
      else if (response_error || response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    case ESP_STATE_WAIT_SEND_OK:
      if (response_send_ok)
      {
        mqtt_last_activity_tick = HAL_GetTick();
        state_deadline = HAL_GetTick() + 5000U;
        auto_state = state_after_send;
      }
      else if (response_error || response_closed || ESP12F_DeadlineExpired())
      {
        ESP12F_ScheduleRetry();
      }
      break;

    default:
      ESP12F_ScheduleRetry();
      break;
  }
}
