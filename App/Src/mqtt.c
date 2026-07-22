#include "mqtt.h"

#include <string.h>

/* 编码 MQTT Remaining Length 字段。
 *
 * MQTT 使用 7 位有效数据 + 1 位延续标志的变长编码：
 * - 当前字节 bit7 = 1，表示后面还有长度字节；
 * - 当前字节 bit7 = 0，表示这是最后一个长度字节；
 * - 每个字节的低 7 位按 128 的幂次累加。
 *
 * 例如长度 321 会编码为：
 * 321 % 128 = 65，剩余 2，因此第一个字节为 0xC1；
 * 第二个字节为 0x02。
 */
static uint16_t MQTT_WriteLength(uint8_t *buffer, uint32_t value)
{
  uint16_t count = 0U;

  do
  {
    uint8_t byte = (uint8_t)(value % 128U);

    value /= 128U;
    if (value != 0U)
    {
      byte |= 0x80U;
    }
    buffer[count++] = byte;
  } while ((value != 0U) && (count < MQTT_MAX_REMAINING_LENGTH_BYTES));

  return count;
}

/* MQTT UTF-8 字符串的存储格式是：
 * 2 字节大端长度 + 字符串原始字节。
 */
static uint16_t MQTT_StringSize(const char *value)
{
  return (uint16_t)(2U + strlen(value));
}

/* 将 C 字符串写成 MQTT 字符串，并返回占用的总字节数。 */
static uint16_t MQTT_WriteString(uint8_t *buffer, const char *value)
{
  uint16_t length = (uint16_t)strlen(value);

  buffer[0] = (uint8_t)(length >> 8);
  buffer[1] = (uint8_t)length;
  memcpy(&buffer[2], value, length);
  return (uint16_t)(length + 2U);
}

uint16_t MQTT_EncodeConnect(uint8_t *buffer, uint16_t size,
                            const char *client_id, const char *username,
                            const char *password, uint16_t keep_alive_seconds)
{
  /* CONNECT 标志：
   * bit1 = Clean Session；
   * bit7 = Username Flag；
   * bit6 = Password Flag。
   *
   * 当前不使用遗嘱、QoS 和保留标志，因此基础值为 0x02。
   */
  uint8_t flags = 0x02U;
  uint16_t payload_size = MQTT_StringSize(client_id);
  uint16_t remaining_size;
  uint16_t index;

  if ((username != NULL) && (username[0] != '\0'))
  {
    flags |= 0x80U;
    payload_size = (uint16_t)(payload_size + MQTT_StringSize(username));
  }
  if ((password != NULL) && (password[0] != '\0'))
  {
    flags |= 0x40U;
    payload_size = (uint16_t)(payload_size + MQTT_StringSize(password));
  }

  /* CONNECT 可变头固定为：
   * Protocol Name 2 + 4 字节 "MQTT" + Level 1 + Flags 1 + Keep Alive 2 = 10 字节。
   */
  remaining_size = (uint16_t)(10U + payload_size);

  /* 这里额外预留 5 字节，是 MQTT 固定头可能占用的最大空间：
   * 1 字节报文类型 + 4 字节 Remaining Length。
   */
  if (size < (uint16_t)(remaining_size + 5U))
  {
    return 0U;
  }

  buffer[0] = 0x10U;  /* CONNECT 报文类型，低 4 位必须为 0。 */
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));

  index += MQTT_WriteString(&buffer[index], "MQTT");
  buffer[index++] = 0x04U;  /* MQTT 3.1.1 协议版本。 */
  buffer[index++] = flags;
  buffer[index++] = (uint8_t)(keep_alive_seconds >> 8);
  buffer[index++] = (uint8_t)keep_alive_seconds;

  /* CONNECT 载荷顺序固定为 Client ID、可选 Username、可选 Password。 */
  index += MQTT_WriteString(&buffer[index], client_id);
  if ((flags & 0x80U) != 0U)
  {
    index += MQTT_WriteString(&buffer[index], username);
  }
  if ((flags & 0x40U) != 0U)
  {
    index += MQTT_WriteString(&buffer[index], password);
  }

  return index;
}

uint16_t MQTT_EncodeSubscribe(uint8_t *buffer, uint16_t size,
                              uint16_t packet_id, const char *topic)
{
  /* SUBSCRIBE 可变头为 Packet Identifier（2 字节），
   * 载荷为 Topic Filter 字符串和 requested QoS（1 字节）。
   */
  uint16_t remaining_size = (uint16_t)(2U + MQTT_StringSize(topic) + 1U);
  uint16_t index;

  if (size < (uint16_t)(remaining_size + 5U))
  {
    return 0U;
  }

  /* SUBSCRIBE 的固定头低 4 位必须为 0x02。 */
  buffer[0] = 0x82U;
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));
  buffer[index++] = (uint8_t)(packet_id >> 8);
  buffer[index++] = (uint8_t)packet_id;
  index += MQTT_WriteString(&buffer[index], topic);
  buffer[index++] = 0x00U;  /* requested QoS = 0。 */

  return index;
}

uint16_t MQTT_EncodePublish(uint8_t *buffer, uint16_t size,
                            const char *topic, const uint8_t *payload,
                            uint16_t payload_length)
{
  /* 当前使用 QoS 0、非 DUP、非 RETAIN：
   * PUBLISH 固定头为 0x30，载荷中不包含 Packet Identifier。
   */
  uint16_t remaining_size = (uint16_t)(MQTT_StringSize(topic) + payload_length);
  uint16_t index;

  if (size < (uint16_t)(remaining_size + 5U))
  {
    return 0U;
  }

  buffer[0] = 0x30U;
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));
  index += MQTT_WriteString(&buffer[index], topic);
  memcpy(&buffer[index], payload, payload_length);

  return (uint16_t)(index + payload_length);
}

uint16_t MQTT_EncodePingReq(uint8_t *buffer, uint16_t size)
{
  if (size < 2U)
  {
    return 0U;
  }

  /* PINGREQ 的固定头为 0xC0，Remaining Length 必须为 0。 */
  buffer[0] = 0xC0U;
  buffer[1] = 0x00U;
  return 2U;
}
