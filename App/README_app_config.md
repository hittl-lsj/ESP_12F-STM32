# app_config 配置模块

文件：

- `App/Inc/app_config.h`
- `App/Inc/app_secrets.h`（本地配置，不提交到 Git）
- `App/Inc/app_secrets.example.h`（配置模板）

该模块集中保存应用层参数，避免网络地址、GPIO、MQTT 主题和缓冲区大小散落在业务代码中。

## 网络和 MQTT 配置

首次克隆仓库后，把 `app_secrets.example.h` 复制为 `app_secrets.h`，再填写本地网络参数：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

```c
#define APP_WIFI_SSID          "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define APP_SERVER_IP          "192.168.1.100"
#define APP_SERVER_PORT        "1883"

#define APP_MQTT_CLIENT_ID     "stm32-esp12f"
#define APP_MQTT_USERNAME      ""
#define APP_MQTT_PASSWORD      ""
#define APP_MQTT_STATUS_TOPIC  "stm32/esp12f/status"
#define APP_MQTT_COMMAND_TOPIC "stm32/esp12f/command"
```

`APP_SERVER_IP` 应填写 MQTT Broker 在局域网中的 IPv4 地址。电脑重新连接热点后 IPv4 地址可能变化，可运行 `ipconfig` 查看 WLAN IPv4，并同步修改后重新编译和烧录。

如果没有在 `app_secrets.h` 中定义 MQTT 客户端、账号、密码或主题，`app_config.h` 会提供默认值。多个设备连接同一个 Broker 时，必须使用不同的 `APP_MQTT_CLIENT_ID`。

## GPIO 配置

```c
#define APP_LED_GPIO_PORT    GPIOB
#define APP_LED_GPIO_PIN     GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT GPIOA
#define APP_BUZZER_GPIO_PIN  GPIO_PIN_8
```

当前控制逻辑：

- `LED ON`：PB8 输出低电平。
- `LED OFF`：PB8 输出高电平。
- `BUZZER ON`：PA8 输出高电平。
- `BUZZER OFF`：PA8 输出低电平。

更换引脚时必须同时修改 `app_config.h` 和 STM32CubeMX GPIO 配置。

## 周期参数

```c
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
#define APP_STATUS_UPLOAD_INTERVAL_MS 1000U
#define APP_MQTT_KEEP_ALIVE_SECONDS   30U
```

烟雾传感器任务每 1 ms 采样一次 PA0，在 100 ms 窗口内计算 ADC 平均值并换算为相对烟雾百分比。状态上报每 1000 ms 发布一次 MQTT QoS 0 JSON。保活间隔配置为 30 秒，固件会在无收发活动时发送 MQTT PINGREQ。

## 缓冲区配置

```c
#define APP_UART_BRIDGE_BUFFER_SIZE   256U
#define APP_ESP_RESPONSE_BUFFER_SIZE  96U
#define APP_MQTT_PACKET_BUFFER_SIZE   192U
#define APP_MQTT_RX_BUFFER_SIZE       192U
```

- 串口桥接缓冲区分别用于 USART1 -> USART2 和 USART2 -> USART1。
- ESP 响应缓冲区用于识别 AT 响应、`>` 发送提示、`SEND OK` 和断线提示。
- MQTT 发送缓冲区保存 CONNECT、SUBSCRIBE、PUBLISH、PINGREQ 报文。
- MQTT 接收缓冲区保存 `+IPD` 载荷中的 MQTT 报文。

增大缓冲区会增加 SRAM 占用；减小缓冲区前需要确认 MQTT 客户端 ID、主题和状态 JSON 的最大长度仍能放入。

## 安全说明

Wi-Fi 密码和 MQTT 账号密码会以明文编译进固件，适合实验学习，不适合直接用于正式产品。`app_secrets.h` 已被 `.gitignore` 排除，避免意外提交本地凭据。
