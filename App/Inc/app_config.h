#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"
#include "app_secrets.h"

#define APP_LED_GPIO_PORT          GPIOB
#define APP_LED_GPIO_PIN           GPIO_PIN_8

#define APP_UART_BRIDGE_BUFFER_SIZE 256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 64U

#endif
