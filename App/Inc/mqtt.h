#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>

#define MQTT_MAX_REMAINING_LENGTH_BYTES 4U

uint16_t MQTT_EncodeConnect(uint8_t *buffer, uint16_t size,
                            const char *client_id, const char *username,
                            const char *password, uint16_t keep_alive_seconds);
uint16_t MQTT_EncodeSubscribe(uint8_t *buffer, uint16_t size,
                              uint16_t packet_id, const char *topic);
uint16_t MQTT_EncodePublish(uint8_t *buffer, uint16_t size,
                            const char *topic, const uint8_t *payload,
                            uint16_t payload_length);
uint16_t MQTT_EncodePingReq(uint8_t *buffer, uint16_t size);

#endif
