# ESP-12F 通信模块

文件：

- `App/Inc/esp12f.h`
- `App/Src/esp12f.c`

该模块通过 ESP8266 AT 指令控制 ESP-12F，并运行 Wi-Fi、TCP 和 MQTT 状态机。

## 公共 API

```c
void ESP12F_OnRxByte(uint8_t data);
void ESP12F_Task(void);
uint8_t ESP12F_IsConnected(void);
void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t smoke_percent);
```

`ESP12F_OnRxByte()` 消费 ESP 串口输出的字节，解析 AT 响应文本、ESP `+IPD,<length>:` 帧和 MQTT 报文。

`ESP12F_Task()` 推进连接状态机，并周期性发送 MQTT 属性上报。

`ESP12F_SetStatus()` 更新下一次属性上传所使用的 LED、蜂鸣器和烟雾值。

## 连接状态机

```text
boot delay
  -> AT
  -> AT+CWMODE=1
  -> AT+CWJAP="SSID","PASSWORD"
  -> AT+CIPMUX=0
  -> AT+CIPSTART="TCP","dmp-mqtt.cuiot.cn",1883
  -> MQTT CONNECT
  -> MQTT SUBSCRIBE property/set
  -> READY
```

重要超时时间：

- 启动延时：2 秒。
- AT 检查：2 秒。
- Wi-Fi 入网：30 秒。
- TCP 连接：10 秒。
- `AT+CIPSEND` 提示符：3 秒。
- MQTT 发送完成：5 秒。
- 失败后的重试延时：5 秒。

## MQTT 发布

在 READY 状态下，固件按 `APP_STATUS_UPLOAD_INTERVAL_MS` 周期发布烟雾属性：

```json
{"messageId":"123","params":{"key":"smokeConcentration","value":27}}
```

发布 Topic：

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/pub
```

## MQTT 接收

ESP AT 固件按以下格式传递 TCP 数据：

```text
+IPD,<mqtt_packet_length>:<mqtt_binary_packet>
```

该模块会解析 MQTT 报文，当前识别：

- CONNACK
- SUBACK
- PUBLISH

当前下行订阅 Topic：

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/set
```

已有命令执行器仍识别旧的纯文本测试命令：

```text
LED ON
LED OFF
BUZZER ON
BUZZER OFF
```

格物 `property/set` JSON 解析尚未实现。

## 保活

MQTT 空闲时，模块会根据 `APP_MQTT_KEEP_ALIVE_SECONDS` 发送 PINGREQ。

## 限制

- 仅支持明文 TCP MQTT；未实现 TLS。
- 固件发布和订阅的 MQTT QoS 编码为 QoS 0。
- MQTT 实现只覆盖本项目需要的报文类型。
- 如果使用长载荷或额外 Topic，可能需要增大 MQTT 缓冲区。
