# App 应用层

`App` 保存独立于 STM32CubeMX 外设初始化代码的应用逻辑。

## 模块关系

```text
main.c
  |-- UART_Bridge_Init()
  |-- UART_Bridge_Task()
  `-- ESP12F_Task()

USART1 <-> uart_bridge <-> USART2 <-> ESP-12F
                         |
                         `-> ESP12F_OnRxByte()
                               |-- AT 响应解析
                               |-- 自动连接状态机
                               `-- LED ON/OFF 命令
```

## 主循环调用

外设初始化完成后调用一次：

```c
UART_Bridge_Init();
```

主循环持续调用：

```c
UART_Bridge_Task();
ESP12F_Task();
```

两个 Task 都是非阻塞周期任务，主循环中不应加入长时间的 `HAL_Delay()`。

## 文档

- [配置模块](README_app_config.md)
- [串口桥接模块](README_uart_bridge.md)
- [ESP-12F 模块](README_esp12f.md)
