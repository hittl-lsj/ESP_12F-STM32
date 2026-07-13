# ESP-12F STM32 TCP 通信桥

本项目基于 STM32F103 和 ESP-12F（ESP8266 AT 固件）。STM32 通过 USART2 控制 ESP-12F，通过 USART1 与电脑串口工具通信。设备上电后会自动连接 Wi-Fi 和 TCP 服务器，并接收服务器发送的 GPIO 控制指令。

## 主要功能

- USART1 与 USART2 双向透明传输
- ESP8266 AT 指令响应解析
- 自动连接 Wi-Fi 和 TCP 服务器
- 连接失败或断开后自动重试
- 通过 TCP 控制 PB8 LED
- 通过 TCP 控制 PA8 蜂鸣器
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
| PB8 | LED 或其他高电平有效输出 |
| PA8 | 蜂鸣器控制端，高电平有效 |
| GND | 所有设备共地 |

PA8 和 PB8 均配置为推挽输出，上电默认输出低电平。

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
| `LED ON` | PB8 输出高电平，打开 LED |
| `LED OFF` | PB8 输出低电平，关闭 LED |
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

## PowerShell TCP 测试

下面的示例在电脑上启动 TCP 服务器并等待 ESP-12F 连接：

```powershell
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Any, 8080)
$listener.Start()
$client = $listener.AcceptTcpClient()
$stream = $client.GetStream()
```

打开蜂鸣器：

```powershell
$data = [Text.Encoding]::ASCII.GetBytes("BUZZER ON")
$stream.Write($data, 0, $data.Length)
```

关闭蜂鸣器：

```powershell
$data = [Text.Encoding]::ASCII.GetBytes("BUZZER OFF")
$stream.Write($data, 0, $data.Length)
```

控制 LED 时，将发送内容替换为 `LED ON` 或 `LED OFF`。

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
