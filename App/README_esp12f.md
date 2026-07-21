# esp12f 通信模块

文件：

- `App/Inc/esp12f.h`
- `App/Src/esp12f.c`

该模块负责 ESP-12F AT 响应解析、Wi-Fi/TCP 自动连接、MQTT 建连订阅、断线重试、状态上报和命令执行。

## 公共接口

### ESP12F_OnRxByte

```c
void ESP12F_OnRxByte(uint8_t data);
```

逐字节输入 USART2 收到的数据。当前由 `uart_bridge` 在主循环转发 ESP-12F 数据时调用，不运行在 UART 中断上下文。函数会识别 AT 响应文本、`+IPD,<length>:` 头，并把 `+IPD` 载荷继续交给 MQTT 解码器。

### ESP12F_Task

```c
void ESP12F_Task(void);
```

执行自动连接和发送状态机，应在主循环中持续调用。

### ESP12F_SetStatus

```c
void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t smoke_percent);
```

由主循环周期调用，更新 MQTT 状态上报使用的 LED、蜂鸣器和烟雾值。

### ESP12F_IsConnected

```c
uint8_t ESP12F_IsConnected(void);
```

返回当前 MQTT 状态机是否处于 READY，用于 OLED 显示。

## 自动连接流程

```text
上电等待 2 秒
  -> AT
  -> AT+CWMODE=1
  -> AT+CWJAP="SSID","PASSWORD"
  -> AT+CIPMUX=0
  -> AT+CIPSTART="TCP","BROKER_IP",1883
  -> MQTT CONNECT
  -> MQTT SUBSCRIBE command topic
  -> READY
```

主要超时：

- 上电等待：2 秒。
- AT 检测：2 秒。
- Wi-Fi 连接：30 秒。
- TCP 连接：10 秒。
- `AT+CIPSEND` 等待 `>`：3 秒。
- MQTT 报文发送等待 `SEND OK`：5 秒。
- 失败或断开后的重试间隔：5 秒。

Wi-Fi 连接阶段会等待 `CWJAP` 最终返回 `OK`，避免在 `WIFI CONNECTED` 后、`WIFI GOT IP` 前过早发送下一条命令。收到 `ERROR`、`FAIL`、`CLOSED` 或发生超时后，状态机会重新从 AT 检测开始。

## MQTT 接收流程

ESP AT 固件收到 TCP 数据后输出 `+IPD,<length>:`，模块按长度读取后续载荷，不再使用固定短字符串匹配 GPIO 命令。

```text
+IPD,<mqtt_packet_length>:<mqtt_binary_packet>
```

MQTT 解码器支持当前场景需要的报文：

- `CONNACK`：确认 MQTT 连接成功。
- `SUBACK`：确认订阅流程返回。
- `PUBLISH`：解析命令主题和载荷。

PUBLISH 处理会先验证剩余长度、主题长度、QoS 包 ID 长度和接收缓冲区边界。格式异常或长度不足的报文会被忽略，不会继续按越界地址匹配主题。

## 命令执行

Broker 向 `APP_MQTT_COMMAND_TOPIC` 发布以下 QoS 0 文本：

```text
LED ON
LED OFF
BUZZER ON
BUZZER OFF
```

模块识别后控制 `app_config.h` 指定的 GPIO：

- `LED ON`：PB8 输出低电平。
- `LED OFF`：PB8 输出高电平。
- `BUZZER ON`：PA8 输出高电平。
- `BUZZER OFF`：PA8 输出低电平。

命令区分大小写，载荷中不要添加多余空格或换行。

## 状态上报和保活

READY 状态下，固件每 `APP_STATUS_UPLOAD_INTERVAL_MS` 向 `APP_MQTT_STATUS_TOPIC` 发布一次 QoS 0 JSON：

```json
{"led":"ON","buzzer":"OFF","smoke":27}
```

当 MQTT 活动空闲时间达到 `APP_MQTT_KEEP_ALIVE_SECONDS * 500 ms` 时，固件会发送 PINGREQ 保活。

## 手动调试

USART1 透明透传仍然保留，可以从串口工具观察 ESP AT 输出，也可以手动发送 AT 指令。但自动连接状态机运行时，手动命令的响应可能影响状态判断，调试时应等待自动连接完成后再操作。

## 限制

- 当前实现按 QoS 0 使用；订阅请求的最大 QoS 为 0。
- MQTT 编解码只覆盖本项目需要的 CONNECT、SUBSCRIBE、PUBLISH 和 PINGREQ。
- 当前使用明文 TCP，没有 TLS。
- 接收 MQTT 报文必须放入 `APP_MQTT_RX_BUFFER_SIZE`。
