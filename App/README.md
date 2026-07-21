# App 应用层

`App` 保存独立于 STM32CubeMX 外设初始化代码的应用逻辑。CubeMX 生成的外设初始化仍位于 `Core`，业务状态机、串口桥接、MQTT 编解码和 OLED 驱动位于 `App`。

## 模块关系

```text
main.c
  |-- UART_Bridge_Init()
  |-- OLED_Init()
  |-- Smoke_Init()
  `-- while (1)
      |-- UART_Bridge_Task()
      |-- Smoke_Task()
      |-- ESP12F_SetStatus()
      |-- ESP12F_Task()
      `-- OLED_UpdateScreen()

USART1 <-> uart_bridge <-> USART2 <-> ESP-12F
                         |
                         `-> ESP12F_OnRxByte()
                               |-- AT 响应解析
                               |-- +IPD 长度解析
                               |-- MQTT 报文解析
                               `-- LED/BUZZER 命令执行
```

## 主循环职责

外设初始化完成后调用一次：

```c
UART_Bridge_Init();
OLED_Init();
Smoke_Init();
```

主循环持续调用：

```c
UART_Bridge_Task();
Smoke_Task();
ESP12F_SetStatus(led_on, buzzer_on, smoke_percent);
ESP12F_Task();
```

`UART_Bridge_Task()` 从串口环形缓冲区取数据并转发；来自 ESP-12F 的字节会在主循环中交给 `ESP12F_OnRxByte()` 解析。`ESP12F_Task()` 负责 Wi-Fi、TCP、MQTT 连接和周期状态上报。主循环中不应加入长时间 `HAL_Delay()`，否则会增加串口缓冲区溢出和 MQTT 超时风险。

## 文档

- [配置模块](README_app_config.md)
- [串口桥接模块](README_uart_bridge.md)
- [ESP-12F 模块](README_esp12f.md)
- [MQTT 通信](README_mqtt.md)
