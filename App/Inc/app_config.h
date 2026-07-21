#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"
#include "app_secrets.h"

#define APP_LED_GPIO_PORT          GPIOB
#define APP_LED_GPIO_PIN           GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT       GPIOA
#define APP_BUZZER_GPIO_PIN        GPIO_PIN_8

#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U// 烟雾传感器采样间隔，单位：毫秒
#define APP_SMOKE_WINDOW_MS           100U
#define APP_STATUS_UPLOAD_INTERVAL_MS 1000U

#define APP_UART_BRIDGE_BUFFER_SIZE 256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 96U
#define APP_MQTT_PACKET_BUFFER_SIZE  192U
#define APP_MQTT_RX_BUFFER_SIZE      192U
#define APP_MQTT_KEEP_ALIVE_SECONDS  30U

#ifndef APP_MQTT_CLIENT_ID
#define APP_MQTT_CLIENT_ID     "stm32-esp12f"
#endif
#ifndef APP_MQTT_USERNAME
#define APP_MQTT_USERNAME      ""
#endif
#ifndef APP_MQTT_PASSWORD
#define APP_MQTT_PASSWORD      ""
#endif
#ifndef APP_MQTT_STATUS_TOPIC
#define APP_MQTT_STATUS_TOPIC  "stm32/esp12f/status"
#endif
#ifndef APP_MQTT_COMMAND_TOPIC
#define APP_MQTT_COMMAND_TOPIC "stm32/esp12f/command"
#endif

#endif
