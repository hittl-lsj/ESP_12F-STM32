# esp12f 通信模块

文件：

- `App/Inc/esp12f.h`
- `App/Src/esp12f.c`

该模块负责 ESP-12F AT 响应解析、Wi-Fi/TCP 自动连接、断线重试及服务器 LED 命令。

## 公共接口

### ESP12F_OnRxByte

```c
void ESP12F_OnRxByte(uint8_t data);
```

逐字节输入 USART2 收到的数据。由 `uart_bridge` 的串口接收回调调用，运行在中断上下文。

### ESP12F_Task

```c
void ESP12F_Task(void);
```

执行自动连接状态机，应在主循环中持续调用。

## 自动连接流程

```text
上电等待
  -> AT
  -> AT+CWMODE=1
  -> AT+CWJAP="SSID","PASSWORD"
  -> AT+CIPMUX=0
  -> AT+CIPSTART="TCP","SERVER_IP",PORT
  -> CONNECTED
```

主要超时：

- 上电等待：2 秒。
- AT 检测：2 秒。
- Wi-Fi 连接：30 秒。
- TCP 连接：10 秒。
- 失败或断开后的重试间隔：5 秒。

Wi-Fi 连接阶段会等待 `CWJAP` 最终返回 `OK`，避免在 `WIFI CONNECTED` 后、
`WIFI GOT IP` 前过早发送下一条命令。收到 `ERROR`、`FAIL`、`CLOSED` 或发生超时后，
状态机会重新从 AT 检测开始。

## 服务器命令

服务器发送以下原始 TCP 数据：

```text
LED ON
LED OFF
```

ESP AT 固件输出：

```text
+IPD,6:LED ON
+IPD,7:LED OFF
```

模块识别后控制 `app_config.h` 指定的 GPIO：

- `LED ON`：输出高电平。
- `LED OFF`：输出低电平。

## 当前解析方式

当前版本使用 64 字节滑动字符串窗口和 `strstr()`，目的是便于学习。它没有完整解析
`+IPD,<长度>:` 协议，因此适合当前固定短命令，不适合二进制数据、长数据包或复杂的
连续命令。

后续正式版本建议实现长度驱动的 `+IPD` 状态机，并将命令执行移出 UART 中断上下文。

## 手动调试

USART1 透明透传仍然保留，可以继续从 VOFA+ 手动发送 AT 命令。但自动连接状态机运行时，
手动命令的响应可能影响状态判断，调试时应等待自动连接完成后再操作。
