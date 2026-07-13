# ESP-12F STM32 TCP Bridge

基于 STM32F103 和 ESP-12F（ESP8266 AT 固件）的 UART-to-TCP 通信项目。

STM32 通过 USART2 控制 ESP-12F，通过 USART1 与电脑串口工具通信。项目支持双向串口
透明透传、上电自动连接 Wi-Fi 和 TCP 服务器、断线重连，以及服务器远程控制 GPIO。

## 功能

- USART1 与 USART2 双向不定长数据透传
- 环形缓冲区和 UART 接收中断
- ESP-12F AT 响应解析
- 上电自动连接 Wi-Fi
- 自动连接 TCP 服务器并在断线后重试
- 服务器发送 `LED ON` / `LED OFF` 控制 PB8
- STM32CubeMX `.ioc`、GCC Makefile 和 HAL 驱动
- 模块化应用层与中文模块文档

## 数据链路

```text
Serial Tool <-> USART1 <-> STM32F103 <-> USART2 <-> ESP-12F
                                                     |
                                                     Wi-Fi
                                                     |
                                                  TCP Server
```

## 硬件连接

```text
STM32 PA2  (USART2_TX) -> ESP-12F RX
STM32 PA3  (USART2_RX) <- ESP-12F TX
STM32 PA9  (USART1_TX) -> USB-TTL RX
STM32 PA10 (USART1_RX) <- USB-TTL TX
STM32 PB8               -> LED / controlled output
GND                     -> common ground
```

ESP-12F 使用稳定的 3.3 V 电源，建议供电能力不低于 500 mA。所有 UART 均为
115200、8N1、无流控。

## 配置

首次克隆后创建本地配置：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

编辑 `App/Inc/app_secrets.h`：

```c
#define APP_WIFI_SSID       "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define APP_SERVER_IP       "192.168.1.100"
#define APP_SERVER_PORT     "8080"
```

该文件已被 `.gitignore` 排除，不会提交 Wi-Fi 密码。

## 构建

安装 GNU Arm Embedded Toolchain 和 Make，然后运行：

```powershell
make -j4
```

固件输出：

```text
build/ESP_12f.elf
build/ESP_12f.hex
build/ESP_12f.bin
```

## 模块文档

- [应用层总览](App/README.md)
- [配置模块](App/README_app_config.md)
- [串口桥接模块](App/README_uart_bridge.md)
- [ESP-12F 模块](App/README_esp12f.md)

## TCP 测试服务器

PowerShell 中启动简单服务器：

```powershell
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Any, 8080)
$listener.Start()
$client = $listener.AcceptTcpClient()
$stream = $client.GetStream()
```

发送 GPIO 命令：

```powershell
$data = [Text.Encoding]::ASCII.GetBytes("LED ON")
$stream.Write($data, 0, $data.Length)
```

## 当前限制

`+IPD` 当前使用固定短字符串匹配，适合学习和短命令验证。二进制数据、长包和正式产品
应改为按长度解析的协议状态机。
