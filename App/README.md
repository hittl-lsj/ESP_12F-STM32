# App 应用层

`App` 目录保存独立于 STM32CubeMX 生成代码之外的应用逻辑。CubeMX 生成的外设初始化代码位于 `Core`，连接流程、MQTT 报文处理、UART 桥接、OLED 绘制和应用配置位于 `App`。

## 主循环集成

`Core/Src/main.c` 初始化应用模块：

```c
UART_Bridge_Init();
OLED_Init();
Smoke_Init();
```

主循环持续执行：

```c
UART_Bridge_Task();
Smoke_Task();
ESP12F_SetStatus(led_on, buzzer_on, smoke_percent);
ESP12F_Task();
OLED_UpdateScreen();
```

## 模块职责

```text
main.c
  |-- Smoke_Init()
  |-- Smoke_Task()
  |-- UART_Bridge_Init()
  |-- UART_Bridge_Task()
  |-- ESP12F_SetStatus()
  |-- ESP12F_Task()
  `-- OLED_UpdateScreen()

USART1 <-> uart_bridge <-> USART2 <-> ESP-12F
                         |
                         `-> ESP12F_OnRxByte()
                               |-- AT 响应解析
                               |-- +IPD 载荷解析
                               |-- MQTT 报文解析
                               `-- 云端下行处理入口
```

## 烟雾数据路径

烟雾传感器模拟输出连接到 PA0 / ADC1_IN0。`Smoke_Task()` 每隔 `APP_SMOKE_SAMPLE_INTERVAL_MS` 采样一次 ADC，在 `APP_SMOKE_WINDOW_MS` 窗口内取平均值，并把 ADC 平均值转换为 0-100 的百分比。

该百分比会传给 `ESP12F_SetStatus()`。当 ESP/MQTT 状态机进入就绪状态后，`ESP12F_Task()` 会将它作为格物 `smokeConcentration` 属性发布。

## 当前云端载荷

固件发布：

```json
{"messageId":"123","params":{"key":"smokeConcentration","value":27}}
```

到：

```text
$sys/{productKey}/{deviceKey}/property/pub
```

## 注意事项

- 主循环应保持非阻塞。较长的 `HAL_Delay()` 会增加 UART 缓冲区溢出和 MQTT 超时风险。
- USART1 仍然适合用于查看 ESP AT 日志。
- 云端下行 Topic 已订阅，当前支持格物 `property/set` 中的 `dbmLimit` 阈值设置，并会发布 `property/set_reply`。
- 当烟雾百分比大于或等于 `dbmLimit` 时，PA8 蜂鸣器打开；低于阈值时关闭。
