# 应用配置

应用配置分为两个文件：

- `App/Inc/app_config.h`：纳入版本管理的默认配置，包括引脚、时序、缓冲区和兜底 MQTT 值。
- `App/Inc/app_secrets.h`：本地 Wi-Fi 和云端凭据，已被 Git 忽略。

从模板创建本地密钥文件：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

## 硬件宏

```c
#define APP_LED_GPIO_PORT     GPIOB
#define APP_LED_GPIO_PIN      GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT  GPIOA
#define APP_BUZZER_GPIO_PIN   GPIO_PIN_8
```

PB8 作为低电平有效 LED。PA8 作为高电平有效蜂鸣器输出。

## 烟雾采样

```c
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
```

固件每 1 ms 采样一次 PA0，并在 100 ms 窗口内对 ADC 读数求平均值。结果会转换为 0-100 的相对烟雾百分比。

## 上传与保活时序

```c
#define APP_STATUS_UPLOAD_INTERVAL_MS 100U
#define APP_MQTT_KEEP_ALIVE_SECONDS  30U
```

当前属性上传间隔为 100 ms。正常云端使用时，1000 ms 或 5000 ms 通常更实用。

MQTT keepalive 为 30 秒。如果约半个 keepalive 周期内没有 MQTT 活动，固件会发送 PINGREQ。

## 缓冲区大小

```c
#define APP_UART_BRIDGE_BUFFER_SIZE 256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 96U
#define APP_MQTT_PACKET_BUFFER_SIZE  192U
#define APP_MQTT_RX_BUFFER_SIZE      192U
```

当前格物 CONNECT 报文和属性上报载荷可以放入 192 字节 MQTT 报文缓冲区。如果后续增加更长 Topic、更大 JSON 载荷或更多订阅 Topic，需要增大这些缓冲区。

## 格物凭据

`app_secrets.h` 保存：

- Wi-Fi SSID 和密码。
- MQTT Broker 地址和端口。
- MQTT 客户端 ID。
- MQTT 用户名。
- MQTT HMAC-SHA256 密码。
- 属性上报和下行 Topic。

纳入版本管理的模板使用占位值：

```c
#define APP_WIFI_SSID       "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define APP_SERVER_IP       "dmp-mqtt.cuiot.cn"
#define APP_SERVER_PORT     "1883"

#define APP_MQTT_CLIENT_ID  "DEVICE_ID|PRODUCT_KEY|0|0|0"
#define APP_MQTT_USERNAME   "DEVICE_KEY|PRODUCT_KEY"
#define APP_MQTT_PASSWORD   "HMAC_SHA256_PASSWORD"
```

不要提交真实的 `app_secrets.h`。
