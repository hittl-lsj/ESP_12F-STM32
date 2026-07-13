#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"
#include "app_secrets.h"

#define APP_LED_GPIO_PORT          GPIOB
#define APP_LED_GPIO_PIN           GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT       GPIOA
#define APP_BUZZER_GPIO_PIN        GPIO_PIN_8

#define APP_SOUND_SAMPLE_INTERVAL_MS  1U
#define APP_SOUND_WINDOW_MS           100U
#define APP_STATUS_UPLOAD_INTERVAL_MS 1000U

#define APP_UART_BRIDGE_BUFFER_SIZE 256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 64U

#endif
