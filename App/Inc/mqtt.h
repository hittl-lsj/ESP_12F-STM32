#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>

/* MQTT 剩余长度字段最多使用 4 个字节，这是 MQTT 3.1.1 协议规定的上限。 */
#define MQTT_MAX_REMAINING_LENGTH_BYTES 4U

/* 将 MQTT CONNECT 报文编码到 buffer。
 * 返回值为编码后的总长度；返回 0 表示 buffer 不足或参数无法编码。
 */
uint16_t MQTT_EncodeConnect(uint8_t *buffer, uint16_t size,
                            const char *client_id, const char *username,
                            const char *password, uint16_t keep_alive_seconds);

/* 将 MQTT SUBSCRIBE 报文编码到 buffer。
 * packet_id 为订阅报文标识；topic 为要订阅的 Topic；当前订阅 QoS 固定为 0。
 */
uint16_t MQTT_EncodeSubscribe(uint8_t *buffer, uint16_t size,
                              uint16_t packet_id, const char *topic);

/* 将 MQTT PUBLISH 报文编码到 buffer。
 * 当前发布 QoS 固定为 0，不携带 packet identifier。
 */
uint16_t MQTT_EncodePublish(uint8_t *buffer, uint16_t size,
                            const char *topic, const uint8_t *payload,
                            uint16_t payload_length);

/* 将 MQTT PINGREQ 保活报文编码到 buffer。 */
uint16_t MQTT_EncodePingReq(uint8_t *buffer, uint16_t size);

#endif
