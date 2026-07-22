# UART 桥接

文件：

- `App/Inc/uart_bridge.h`
- `App/Src/uart_bridge.c`

UART 桥接模块连接 PC 调试串口和 ESP-12F 串口：

```text
PC USB-TTL <-> USART1 <-> STM32 <-> USART2 <-> ESP-12F
```

## UART 角色

| UART | 用途 |
| --- | --- |
| USART1 | PC 调试串口 |
| USART2 | ESP-12F AT 指令串口 |

两个 UART 均配置为 115200 波特率、8 数据位、无校验、1 停止位。

## 接收路径

`UART_Bridge_Init()` 启动 USART1 和 USART2 的中断式单字节接收。

接收到的字节会压入环形缓冲区：

```text
USART1 RX -> uart1_to_uart2 buffer
USART2 RX -> uart2_to_uart1 buffer
```

`UART_Bridge_Task()` 转发队列中的字节：

```text
uart1_to_uart2 -> USART2
uart2_to_uart1 -> USART1
```

当来自 ESP-12F 的字节被转发到 USART1 时，桥接模块也会将其传给：

```c
ESP12F_OnRxByte(data);
```

这样状态机可以在 PC 串口终端仍能看到日志的同时，解析 ESP AT 响应和 MQTT `+IPD` 报文。

## 调试用途

USART1 适合观察：

```text
AT
OK
WIFI CONNECTED
WIFI GOT IP
CONNECT
SEND OK
+IPD,...
```

可以从 PC 通过 USART1 手动发送 AT 指令，但自动状态机运行期间手动发送指令可能会干扰响应解析。正常运行时，建议主要把 USART1 当作日志输出。

## 缓冲

桥接模块使用 `APP_UART_BRIDGE_BUFFER_SIZE`，当前为 256 字节。如果日志突发量很大，或主循环阻塞太久，可能发生溢出。
