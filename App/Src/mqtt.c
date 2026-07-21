#include "mqtt.h"

#include <string.h>

static uint16_t MQTT_WriteLength(uint8_t *buffer, uint32_t value)
{
  uint16_t count = 0U;
  do
  {
    uint8_t byte = (uint8_t)(value % 128U);
    value /= 128U;
    if (value != 0U) byte |= 0x80U;
    buffer[count++] = byte;
  } while ((value != 0U) && (count < MQTT_MAX_REMAINING_LENGTH_BYTES));
  return count;
}

static uint16_t MQTT_StringSize(const char *value)
{
  return (uint16_t)(2U + strlen(value));
}

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
  remaining_size = (uint16_t)(10U + payload_size);
  if (size < (uint16_t)(remaining_size + 5U)) return 0U;

  buffer[0] = 0x10U;
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));
  index += MQTT_WriteString(&buffer[index], "MQTT");
  buffer[index++] = 0x04U;
  buffer[index++] = flags;
  buffer[index++] = (uint8_t)(keep_alive_seconds >> 8);
  buffer[index++] = (uint8_t)keep_alive_seconds;
  index += MQTT_WriteString(&buffer[index], client_id);
  if ((flags & 0x80U) != 0U) index += MQTT_WriteString(&buffer[index], username);
  if ((flags & 0x40U) != 0U) index += MQTT_WriteString(&buffer[index], password);
  return index;
}

uint16_t MQTT_EncodeSubscribe(uint8_t *buffer, uint16_t size,
                              uint16_t packet_id, const char *topic)
{
  uint16_t remaining_size = (uint16_t)(2U + MQTT_StringSize(topic) + 1U);
  uint16_t index;
  if (size < (uint16_t)(remaining_size + 5U)) return 0U;
  buffer[0] = 0x82U;
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));
  buffer[index++] = (uint8_t)(packet_id >> 8);
  buffer[index++] = (uint8_t)packet_id;
  index += MQTT_WriteString(&buffer[index], topic);
  buffer[index++] = 0x00U;
  return index;
}

uint16_t MQTT_EncodePublish(uint8_t *buffer, uint16_t size,
                            const char *topic, const uint8_t *payload,
                            uint16_t payload_length)
{
  uint16_t remaining_size = (uint16_t)(MQTT_StringSize(topic) + payload_length);
  uint16_t index;
  if (size < (uint16_t)(remaining_size + 5U)) return 0U;
  buffer[0] = 0x30U;
  index = (uint16_t)(1U + MQTT_WriteLength(&buffer[1], remaining_size));
  index += MQTT_WriteString(&buffer[index], topic);
  memcpy(&buffer[index], payload, payload_length);
  return (uint16_t)(index + payload_length);
}

uint16_t MQTT_EncodePingReq(uint8_t *buffer, uint16_t size)
{
  if (size < 2U) return 0U;
  buffer[0] = 0xC0U;
  buffer[1] = 0x00U;
  return 2U;
}
