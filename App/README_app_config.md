# app_config 配置模块

文件：

- `App/Inc/app_config.h`
- `App/Inc/app_secrets.h`（本地配置，不提交到 Git）
- `App/Inc/app_secrets.example.h`（配置模板）

该模块集中保存应用层参数，避免网络地址、GPIO 和缓冲区大小散落在业务代码中。

## 网络配置

首次克隆仓库后，把 `app_secrets.example.h` 复制为 `app_secrets.h`，再填写本地网络参数。

电脑重新连接热点后 IPv4 地址可能变化。运行 `ipconfig` 查看 WLAN IPv4，并同步修改
`APP_SERVER_IP`，然后重新编译和烧录。

## LED 配置

```c
#define APP_LED_GPIO_PORT GPIOB
#define APP_LED_GPIO_PIN  GPIO_PIN_8
```

当前 CubeMX 工程已经将 PB8 配置为推挽输出。更换引脚时必须同时修改 CubeMX GPIO
配置和这里的宏。

## 缓冲区配置

```c
#define APP_UART_BRIDGE_BUFFER_SIZE  256U
#define APP_ESP_RESPONSE_BUFFER_SIZE 64U
```

- 串口桥接缓冲区分别用于 USART1 -> USART2 和 USART2 -> USART1。
- ESP 响应缓冲区采用滑动窗口，用来识别 AT 响应和简单的 `+IPD` 命令。
- 增大缓冲区会增加 SRAM 占用。

## 安全说明

Wi-Fi 密码会以明文编译进固件，适合实验学习，不适合直接用于正式产品。
`app_secrets.h` 已被 `.gitignore` 排除，避免意外提交本地凭据。
