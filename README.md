# ESP-12F STM32 MQTT 通信节点

本项目基于 STM32F103C8T6 和 ESP-12F（ESP8266 AT 固件）。STM32 通过 USART2 控制 ESP-12F，通过 USART1 与电脑串口工具双向透传调试数据。设备上电后会自动连接 Wi-Fi，打开到 MQTT Broker 的 TCP 连接，完成 MQTT 3.1.1 建连，订阅命令主题，并每秒发布一次 LED、蜂鸣器和烟雾状态。

## 主要功能

- USART1 与 USART2 双向透明传输，便于电脑串口调试 ESP AT 输出。
- ESP8266 AT 指令响应解析、Wi-Fi/TCP/MQTT 自动连接和断线重试。
- MQTT 3.1.1 基础客户端：CONNECT / CONNACK、QoS 0 PUBLISH、SUBSCRIBE / SUBACK、PINGREQ。
- 通过 MQTT 命令主题控制 PB8 LED 和 PA8 蜂鸣器。
- 通过 PA0 ADC 采集烟雾传感器模拟输出，并换算为相对烟雾百分比。
- OLED 显示 ESP 连接状态、LED、蜂鸣器和相对烟雾浓度。
- 提供 PowerShell MQTT 控制台脚本 `esp_server.ps1`。
- 提供 STM32CubeMX 工程、GNU Arm Makefile 和 HAL 驱动。

## 数据链路

```text
电脑串口工具 <-> USART1 <-> STM32F103 <-> USART2 <-> ESP-12F
                                                       |
                                                     Wi-Fi
                                                       |
                                               MQTT Broker:1883
```

## 硬件接线

| STM32 引脚 | 连接设备 |
|---|---|
| PA2 | ESP-12F RX（USART2_TX） |
| PA3 | ESP-12F TX（USART2_RX） |
| PA9 | USB-TTL RX（USART1_TX） |
| PA10 | USB-TTL TX（USART1_RX） |
| PA0 | 烟雾传感器模拟输出 AO（ADC1_IN0） |
| PB8 | LED，当前固件按低电平点亮 |
| PA8 | 蜂鸣器控制端，高电平有效 |
| PB10 | OLED CS |
| PB11 | OLED DC |
| PB12 | OLED RST |
| PB13 | OLED SCK |
| PB14 | OLED MOSI |
| GND | 所有设备共地 |

PA0 必须连接烟雾传感器模块的模拟输出 `AO`，不要连接数字比较器输出 `DO`。烟雾传感器模块和 STM32 必须共地，PA0 输入电压不得超过 3.3 V；如果模块 AO 输出可能高于 3.3 V，应先分压或做电平转换。

固件每 1 ms 采集一次 PA0，在 100 ms 窗口内计算 ADC 平均值，然后换算为 `0%` 到 `100%` 的相对烟雾浓度。该数值用于观察 AO 电压变化，并不是经过标定的 ppm 浓度值。

如果蜂鸣器工作电流超过 STM32 GPIO 的驱动能力，应使用三极管或 MOSFET 驱动，不能将大功率蜂鸣器直接连接到 PA8。ESP-12F 应使用稳定的 3.3 V 电源，建议供电能力不低于 500 mA。USART1 和 USART2 均配置为 115200、8N1、无流控。

## 网络配置

首次克隆工程后，复制配置模板：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

然后编辑 `App/Inc/app_secrets.h`：

```c
#define APP_WIFI_SSID          "Wi-Fi名称"
#define APP_WIFI_PASSWORD      "Wi-Fi密码"
#define APP_SERVER_IP          "MQTT Broker所在电脑或服务器IP"
#define APP_SERVER_PORT        "1883"

#define APP_MQTT_CLIENT_ID     "stm32-esp12f"
#define APP_MQTT_USERNAME      ""
#define APP_MQTT_PASSWORD      ""
#define APP_MQTT_STATUS_TOPIC  "stm32/esp12f/status"
#define APP_MQTT_COMMAND_TOPIC "stm32/esp12f/command"
```

`app_secrets.h` 已被 `.gitignore` 排除，不会提交到 Git 仓库。多个设备连接同一个 Broker 时，必须给每台设备配置不同的 `APP_MQTT_CLIENT_ID`。

## MQTT 消息

设备每秒向状态主题发布一次 QoS 0 JSON：

```json
{"led":"ON","buzzer":"OFF","smoke":27}
```

向命令主题发布以下 QoS 0 文本可控制 GPIO：

| 命令主题载荷 | 执行动作 |
|---|---|
| `LED ON` | PB8 输出低电平，打开 LED |
| `LED OFF` | PB8 输出高电平，关闭 LED |
| `BUZZER ON` | PA8 输出高电平，打开蜂鸣器 |
| `BUZZER OFF` | PA8 输出低电平，关闭蜂鸣器 |

## PC 串口调试

电脑通过 USB-TTL 连接 USART1 后，USART1 收到的数据会原样转发到 ESP-12F，ESP-12F 返回的数据也会原样显示在 USART1。串口工具应设置为 115200、8N1、无流控，并为手动 AT 指令添加 `CRLF` 结尾。

固件自动连接时会依次发送：

```text
AT
AT+CWMODE=1
AT+CWJAP="SSID","PASSWORD"
AT+CIPMUX=0
AT+CIPSTART="TCP","BROKER_IP",1883
```

TCP 连接成功后，固件会继续通过 `AT+CIPSEND` 发送 MQTT 二进制报文。自动连接状态机运行时，不建议从 PC 同时发送 AT 指令，否则手动指令的响应可能影响状态判断。

## PowerShell MQTT 控制台

工程根目录提供了 [esp_server.ps1](esp_server.ps1)。脚本作为 MQTT 客户端连接现有 Broker，订阅设备状态主题，并将 LED/蜂鸣器控制命令发布到命令主题。脚本使用系统自带 .NET，不依赖 Mosquitto 命令行工具。

在 PowerShell 中运行：

```powershell
cd C:\D\Stm32.projects\ESP_12f
powershell -ExecutionPolicy Bypass -File .\esp_server.ps1 -Broker 192.168.1.100
```

Broker 需要账号密码时：

```powershell
.\esp_server.ps1 -Broker 192.168.1.100 -Port 1883 `
    -Username mqtt-user -Password mqtt-password
```

使用自定义主题时，脚本参数必须与固件配置一致：

```powershell
.\esp_server.ps1 -Broker 192.168.1.100 `
    -StatusTopic devices/node1/status `
    -CommandTopic devices/node1/command
```

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

## 本地完整验证

1. 启动本机 MQTT Broker。使用仓库里的测试配置时，在一个 PowerShell 窗口运行：

```powershell
mosquitto -c .\mosquitto-test.conf -v
```

2. 查看本机无线网卡 IPv4 地址，并把 `App/Inc/app_secrets.h` 中的 `APP_SERVER_IP` 改成这个地址，`APP_SERVER_PORT` 保持 `1883`。

```powershell
ipconfig
```

3. 编译并烧录固件：

```powershell
make -j4
```

4. 打开 USART1 串口，参数为 115200、8N1、无流控。正常情况下可以看到 `AT`、`WIFI GOT IP`、`CONNECT`、`SEND OK` 等 ESP AT 输出，OLED 显示 `ESP: CONNECT` 后表示 MQTT 建连和订阅已完成。

5. 在另一 PowerShell 窗口运行 MQTT 控制台：

```powershell
.\esp_server.ps1 -Broker 127.0.0.1
```

也可以用 Mosquitto 命令行分别订阅和发布：

```powershell
mosquitto_sub -h 127.0.0.1 -t stm32/esp12f/status -v
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "LED ON"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "LED OFF"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "BUZZER ON"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "BUZZER OFF"
```

6. 验收标准：状态主题每秒收到一条 JSON；`LED ON/OFF` 能切换 PB8 LED；`BUZZER ON/OFF` 能切换 PA8 蜂鸣器；改变 PA0 烟雾传感器 AO 电压时，状态 JSON 中的 `smoke` 和 OLED 上的 `SMOKE` 数值会变化；断开 Broker 或 Wi-Fi 后，串口能看到断线并在重试间隔后自动重连。

## 模块文档

- [应用层总览](App/README.md)
- [应用配置](App/README_app_config.md)
- [串口通信桥](App/README_uart_bridge.md)
- [ESP-12F 通信模块](App/README_esp12f.md)
- [MQTT 通信](App/README_mqtt.md)

## 当前限制

当前实现使用明文 MQTT/TCP，未实现 TLS；发布和订阅按 QoS 0 设计；串口桥接发送仍使用逐字节阻塞式 `HAL_UART_Transmit()`；MQTT 编解码为项目内最小实现，只覆盖当前命令和状态上报场景。
