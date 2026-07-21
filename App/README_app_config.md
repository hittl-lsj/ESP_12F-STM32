# Application Configuration

Application configuration is split into two files:

- `App/Inc/app_config.h`: tracked defaults for pins, timing, buffers, and fallback MQTT values.
- `App/Inc/app_secrets.h`: local Wi-Fi and cloud credentials, ignored by Git.

Create the local secret file from the template:

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

## Hardware Macros

```c
#define APP_LED_GPIO_PORT     GPIOB
#define APP_LED_GPIO_PIN      GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT  GPIOA
#define APP_BUZZER_GPIO_PIN   GPIO_PIN_8
```

PB8 is treated as an active-low LED. PA8 is treated as an active-high buzzer output.

## Smoke Sampling

```c
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
```

The firmware samples PA0 every 1 ms and averages the ADC readings over a 100 ms window. The result is converted to a relative smoke percentage from 0 to 100.

## Upload and Keepalive Timing

```c
#define APP_STATUS_UPLOAD_INTERVAL_MS 100U
#define APP_MQTT_KEEP_ALIVE_SECONDS  30U
```

The current property upload interval is 100 ms. For normal cloud use, 1000 ms or 5000 ms is often more practical.

The MQTT keepalive is 30 seconds. If there is no MQTT activity for about half the keepalive interval, the firmware sends a PINGREQ.

## Buffer Sizes

```c
#define APP_UART_BRIDGE_BUFFER_SIZE 256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 96U
#define APP_MQTT_PACKET_BUFFER_SIZE  192U
#define APP_MQTT_RX_BUFFER_SIZE      192U
```

The current Gewu CONNECT packet and property upload payload fit within the 192-byte MQTT packet buffer. Increase these sizes if longer topics, larger JSON payloads, or more subscribed topics are added.

## Gewu Credentials

`app_secrets.h` stores:

- Wi-Fi SSID and password.
- MQTT broker host and port.
- MQTT client ID.
- MQTT username.
- MQTT HMAC-SHA256 password.
- Property publish and downlink topics.

The tracked template uses placeholders:

```c
#define APP_WIFI_SSID       "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define APP_SERVER_IP       "dmp-mqtt.cuiot.cn"
#define APP_SERVER_PORT     "1883"

#define APP_MQTT_CLIENT_ID  "DEVICE_ID|PRODUCT_KEY|0|0|0"
#define APP_MQTT_USERNAME   "DEVICE_KEY|PRODUCT_KEY"
#define APP_MQTT_PASSWORD   "HMAC_SHA256_PASSWORD"
```

Never commit the real `app_secrets.h`.
