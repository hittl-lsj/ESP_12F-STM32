# ESP-12F STM32 TCP 通信桥

本项目基于 STM32F103 和 ESP-12F（ESP8266 AT 固件）。STM32 通过 USART2 控制 ESP-12F，通过 USART1 与电脑串口工具通信。设备上电后会自动连接 Wi-Fi 和 TCP 服务器，接收服务器发送的 GPIO 控制指令，并上传 LED、蜂鸣器和噪音状态。

## 主要功能

- USART1 与 USART2 双向透明传输
- ESP8266 AT 指令响应解析
- 自动连接 Wi-Fi 和 TCP 服务器
- 连接失败或断开后自动重试
- 通过 TCP 控制 PB8 LED
- 通过 TCP 控制 PA8 蜂鸣器
- 通过 PA0 ADC 采集噪音模块模拟输出
- OLED 显示网络、LED、蜂鸣器和相对噪音大小
- 每秒向 TCP 服务器上传一次设备状态
- 提供可交互的 PowerShell TCP 服务器脚本
- 提供 STM32CubeMX 工程、GNU Arm Makefile 和 HAL 驱动

## 数据链路

```text
电脑串口工具 <-> USART1 <-> STM32F103 <-> USART2 <-> ESP-12F
                                                       |
                                                     Wi-Fi
                                                       |
                                                   TCP 服务器
```

## 硬件接线

| STM32 引脚 | 连接设备 |
|---|---|
| PA2 | ESP-12F RX（USART2_TX） |
| PA3 | ESP-12F TX（USART2_RX） |
| PA9 | USB-TTL RX（USART1_TX） |
| PA10 | USB-TTL TX（USART1_RX） |
| PA0 | 噪音模块模拟输出 AO（ADC1_IN0） |
| PB8 | LED，当前固件按低电平点亮 |
| PA8 | 蜂鸣器控制端，高电平有效 |
| GND | 所有设备共地 |

PA0 必须连接噪音模块的模拟输出 `AO`，不要连接数字比较器输出 `DO`。噪音模块和 STM32 必须共地，PA0 输入电压不得超过 3.3 V。

固件每 1 ms 采集一次 PA0，在 100 ms 窗口内计算 ADC 最大值与最小值之差，然后换算为 `0%` 到 `100%` 的相对音量。该数值用于观察声音变化，并不是经过声压计校准的 dB 值。

如果蜂鸣器工作电流超过 STM32 GPIO 的驱动能力，应使用三极管或 MOSFET 驱动，不能将大功率蜂鸣器直接连接到 PA8。

ESP-12F 应使用稳定的 3.3 V 电源，建议供电能力不低于 500 mA。USART1 和 USART2 均配置为 115200、8N1、无流控。

## 网络配置

首次克隆工程后，复制配置模板：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

然后编辑 `App/Inc/app_secrets.h`：

```c
#define APP_WIFI_SSID       "Wi-Fi名称"
#define APP_WIFI_PASSWORD   "Wi-Fi密码"
#define APP_SERVER_IP       "服务器IP地址"
#define APP_SERVER_PORT     "8080"
```

`app_secrets.h` 已被 `.gitignore` 排除，不会提交到 Git 仓库。

## GPIO 配置

GPIO 引脚定义位于 `App/Inc/app_config.h`：

```c
#define APP_LED_GPIO_PORT    GPIOB
#define APP_LED_GPIO_PIN     GPIO_PIN_8

#define APP_BUZZER_GPIO_PORT GPIOA
#define APP_BUZZER_GPIO_PIN  GPIO_PIN_8
```

修改引脚时，需要同时修改 `app_config.h` 和 STM32CubeMX 的 GPIO 配置。

## TCP 控制指令

TCP 服务器可以向 ESP-12F 发送以下 ASCII 文本。指令区分大小写，发送时不要附加多余空格。

| PC/服务器发送内容 | 执行动作 |
|---|---|
| `LED ON` | PB8 输出低电平，打开 LED |
| `LED OFF` | PB8 输出高电平，关闭 LED |
| `BUZZER ON` | PA8 输出高电平，打开蜂鸣器 |
| `BUZZER OFF` | PA8 输出低电平，关闭蜂鸣器 |

ESP-12F 会将这些数据封装为 `+IPD` 消息，STM32 实际识别的内容如下：

```text
+IPD,6:LED ON
+IPD,7:LED OFF
+IPD,9:BUZZER ON
+IPD,10:BUZZER OFF
```

## PC 串口发送指令

电脑通过 USB-TTL 连接 USART1 后，发送的数据会原样转发到 ESP-12F。串口工具应设置为 115200、8N1、无流控，并为 AT 指令添加 `CRLF` 结尾。

固件自动连接时会依次发送以下全部 AT 指令：

| 顺序 | PC 可手动发送的指令 | 作用 |
|---:|---|---|
| 1 | `AT` | 检查 ESP-12F 是否正常响应 |
| 2 | `AT+CWMODE=1` | 设置为 Wi-Fi Station 模式 |
| 3 | `AT+CWJAP="SSID","PASSWORD"` | 连接指定的 Wi-Fi |
| 4 | `AT+CIPMUX=0` | 设置为单连接模式 |
| 5 | `AT+CIPSTART="TCP","SERVER_IP",PORT` | 连接 TCP 服务器 |

例如：

```text
AT
AT+CWMODE=1
AT+CWJAP="MyWiFi","12345678"
AT+CIPMUX=0
AT+CIPSTART="TCP","192.168.1.100",8080
```

USART1 会同时显示 ESP-12F 返回的数据，常见响应包括：

| ESP 返回内容 | 含义 |
|---|---|
| `OK` | 指令执行成功 |
| `ERROR` | 指令执行失败 |
| `FAIL` | Wi-Fi 等操作失败 |
| `WIFI CONNECTED` | 已连接 Wi-Fi |
| `WIFI GOT IP` | 已获取 IP 地址 |
| `CONNECT` | TCP 连接成功 |
| `CLOSED` | TCP 连接已断开 |

固件正在自动连接时，不建议从 PC 同时发送 AT 指令，否则手动指令的响应可能影响自动连接状态机。

## PowerShell TCP 服务器

工程根目录提供了 [esp_server.ps1](esp_server.ps1)。脚本会监听 TCP 8080 端口、显示 STM32 上传的状态、发送 LED/蜂鸣器控制命令，并在 ESP 断线后继续等待重连。

在 PowerShell 中运行：

```powershell
cd C:\D\Stm32.projects\ESP_12f
powershell -ExecutionPolicy Bypass -File .\esp_server.ps1
```

如果当前 PowerShell 已允许执行本地脚本，也可以直接运行：

```powershell
.\esp_server.ps1
```

运行后可以输入以下命令：

| 输入内容 | 作用 |
|---|---|
| `LED ON` | 打开 LED |
| `LED OFF` | 关闭 LED |
| `BUZZER ON` | 打开蜂鸣器 |
| `BUZZER OFF` | 关闭蜂鸣器 |
| `HELP` | 显示命令列表 |
| `QUIT` | 停止服务器并释放端口 |

ESP 连接后，服务器每秒会收到类似内容：

```text
RX: STATUS LED=ON BUZZER=OFF NOISE=27
```

字段含义：

| 字段 | 含义 |
|---|---|
| `LED` | LED 当前开关状态 |
| `BUZZER` | 蜂鸣器当前开关状态 |
| `NOISE` | PA0 检测到的相对噪音百分比 |

如果 8080 端口已被旧的 PowerShell 监听程序占用，请先关闭旧窗口，或者查找占用端口的进程：

```powershell
Get-NetTCPConnection -LocalPort 8080 -ErrorAction SilentlyContinue |
    Select-Object LocalAddress, LocalPort, State, OwningProcess
```

脚本也可以监听其他端口：

```powershell
.\esp_server.ps1 -Port 8081
```

此时还需要将 `App/Inc/app_secrets.h` 中的 `APP_SERVER_PORT` 改为相同端口并重新编译、烧录。

## 编译工程

安装 GNU Arm Embedded Toolchain 和 GNU Make，然后执行：

```powershell
make -j4
```

编译结果位于：

```text
build/ESP_12f.elf
build/ESP_12f.hex
build/ESP_12f.bin
```

## 模块文档

- [应用层总览](App/README.md)
- [应用配置](App/README_app_config.md)
- [串口通信桥](App/README_uart_bridge.md)
- [ESP-12F 通信模块](App/README_esp12f.md)

## 当前限制

当前版本使用固定短字符串匹配 `+IPD` 数据，适合学习和上述 GPIO 指令测试。它没有完整解析 `+IPD,<长度>:` 协议，因此不适合二进制数据、长数据包或复杂的连续指令。正式产品应改为按数据长度解析的协议状态机。
