#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"
#include "app_secrets.h"

/* LED 和蜂鸣器的硬件映射。
 * LED 接在 PB8，当前电路为低电平点亮；蜂鸣器接在 PA8，高电平鸣叫。
 */
#define APP_LED_GPIO_PORT             GPIOB
#define APP_LED_GPIO_PIN              GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT          GPIOA
#define APP_BUZZER_GPIO_PIN           GPIO_PIN_8

/* 烟雾传感器采样参数。
 * SAMPLE_INTERVAL_MS 表示 ADC 采样周期；WINDOW_MS 表示滑动统计窗口。
 * 当前配置下每 1 ms 采样一次，并每 100 ms 计算一次平均值。
 */
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
#define APP_SMOKE_ALARM_LIMIT_DEFAULT 30U  /* 默认报警阈值，单位：相对烟雾百分比。 */

/* 云端状态上传周期。
 * 100 ms 适合调试实时性；正式接入云平台时通常建议改为 1000U 或 5000U，
 * 以减少 MQTT 消息数量和平台流量。
 */
#define APP_STATUS_UPLOAD_INTERVAL_MS 100U

/* 软件缓冲区大小。
 * UART 桥接缓冲区用于中断接收与主循环转发解耦。
 * MQTT 缓冲区需要同时容纳 CONNECT 报文、PUBLISH Topic 和 JSON 载荷。
 */
#define APP_UART_BRIDGE_BUFFER_SIZE   256U
#define APP_ESP_RESPONSE_BUFFER_SIZE  96U
#define APP_MQTT_PACKET_BUFFER_SIZE   192U
#define APP_MQTT_RX_BUFFER_SIZE       192U

/* MQTT keepalive，单位：秒。
 * ESP/MQTT 状态机在约半个 keepalive 周期无 MQTT 活动时主动发送 PINGREQ。
 */
#define APP_MQTT_KEEP_ALIVE_SECONDS   30U

/* 下面这些 MQTT 默认值只在 app_secrets.h 没有提供对应宏时生效。
 * 正常项目应在 App/Inc/app_secrets.h 中填写真实 Wi-Fi 和格物平台凭据。
 */
#ifndef APP_MQTT_CLIENT_ID
#define APP_MQTT_CLIENT_ID            "stm32-esp12f"
#endif
#ifndef APP_MQTT_USERNAME
#define APP_MQTT_USERNAME             ""
#endif
#ifndef APP_MQTT_PASSWORD
#define APP_MQTT_PASSWORD             ""
#endif
#ifndef APP_MQTT_STATUS_TOPIC
#define APP_MQTT_STATUS_TOPIC         "stm32/esp12f/status"
#endif
#ifndef APP_MQTT_COMMAND_TOPIC
#define APP_MQTT_COMMAND_TOPIC        "stm32/esp12f/command"
#endif
#ifndef APP_MQTT_COMMAND_REPLY_TOPIC
#define APP_MQTT_COMMAND_REPLY_TOPIC  "stm32/esp12f/command_reply"
#endif

#endif
