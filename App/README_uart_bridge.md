# uart_bridge 串口桥接模块

文件：

- `App/Inc/uart_bridge.h`
- `App/Src/uart_bridge.c`

该模块负责 USART1 和 USART2 的不定长双向透明透传。

```text
电脑/VOFA+ -- USART1 -- STM32 -- USART2 -- ESP-12F
```

## 公共接口

### UART_Bridge_Init

```c
void UART_Bridge_Init(void);
```

启动 USART1 和 USART2 的单字节中断接收。必须在两个串口完成初始化后调用一次。

### UART_Bridge_Task

```c
void UART_Bridge_Task(void);
```

从两个环形缓冲区取出数据并转发，应在主循环中持续调用。

## 接收流程

`HAL_UART_RxCpltCallback()` 由本模块统一实现：

- USART1 收到字节：放入 USART1 -> USART2 缓冲区。
- USART2 收到字节：放入 USART2 -> USART1 缓冲区，主循环转发时再交给
  `ESP12F_OnRxByte()` 解析，避免在中断里执行字符串搜索。
- 每次收到一个字节后立即重新启动下一字节接收。

因此 ESP 的返回内容既会被应用解析，也会原样显示在 VOFA+ 中。

## 错误恢复

`HAL_UART_ErrorCallback()` 会重新启动对应串口的中断接收。它只负责恢复接收，不记录
具体错误类型。

## 缓冲区满

环形缓冲区满时丢弃新字节，并增加内部 `overflow_count`。当前没有公开读取该计数的
接口；调试时可通过 Watch 窗口查看模块内变量。

## 限制

- 发送端目前使用逐字节阻塞式 `HAL_UART_Transmit()`。
- HAL 的 UART 完成和错误回调由本模块占用，其他模块不能重复定义同名回调。
- 更高吞吐量场景建议升级为 DMA + 空闲线接收和中断/DMA 发送队列。
