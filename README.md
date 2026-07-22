# STM32F103 + ESP-12F 格物 MQTT 烟雾节点

本项目是面向烟雾传感器节点的 STM32F103C8T6 固件。STM32 通过 ESP8266 AT 指令控制 ESP-12F 模块，连接中国联通格物 MQTT 平台，并上报产品模型中定义的 `smokeConcentration` 属性。

当前固件使用明文 MQTT/TCP，目标服务为 `dmp-mqtt.cuiot.cn:1883`。

## 功能特性

- 基于 STM32CubeMX 生成的 STM32F103C8T6 HAL 工程。
- 通过 USART2 和 ESP8266 AT 固件控制 ESP-12F 的 Wi-Fi/TCP 连接。
- USART1 调试桥接，可在 PC 与 ESP-12F 之间转发串口数据。
- MQTT 3.1.1 最小客户端实现：CONNECT、SUBSCRIBE、PUBLISH、PINGREQ。
- PA0 / ADC1 烟雾传感器模拟采样。
- PB8 LED 控制，PA8 蜂鸣器控制。
- OLED 状态显示：ESP 连接状态、LED、蜂鸣器和烟雾百分比。
- 按格物属性上报格式上传 `smokeConcentration`。

## 硬件连接

| STM32 引脚 | 设备 |
| --- | --- |
| PA2 | ESP-12F RX，USART2_TX |
| PA3 | ESP-12F TX，USART2_RX |
| PA9 | USB-TTL RX，USART1_TX |
| PA10 | USB-TTL TX，USART1_RX |
| PA0 | 烟雾传感器 AO，ADC1_IN0 |
| PB8 | LED，低电平有效 |
| PA8 | 蜂鸣器，高电平有效 |
| PB10 | OLED CS |
| PB11 | OLED DC |
| PB12 | OLED RST |
| PB13 | OLED SCK |
| PB14 | OLED MOSI |
| GND | 共地 |

烟雾传感器 AO 输出电压不能超过 3.3 V。ESP-12F 需要稳定的 3.3 V 供电，建议供电能力不低于 500 mA。

## 数据流

```text
烟雾传感器 AO -> STM32 ADC1/PA0 -> smoke_percent
STM32 USART2 -> ESP-12F -> Wi-Fi -> 格物 MQTT Broker
格物平台 -> 设备影子 / 属性数据
```

固件发布到以下 Topic：

```text
$sys/{productKey}/{deviceKey}/property/pub
```

当前设备 Topic：

```text
$sys/{productKey}/{deviceKey}/property/pub
```

上报载荷遵循格物属性格式：

```json
{
  "messageId": "123",
  "params": {
    "key": "smokeConcentration",
    "value": 27
  }
}
```

## 配置

先从模板创建本地密钥配置：

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

然后编辑 `App/Inc/app_secrets.h`，填写：

- Wi-Fi SSID 和密码。
- 格物 MQTT Broker、产品 Key、设备 Key、客户端 ID、用户名和 HMAC-SHA256 密码。
- 格物属性上报 Topic 和属性设置 Topic。

`App/Inc/app_secrets.h` 已被 Git 忽略，不能提交真实密钥。

当前时序默认值在 `App/Inc/app_config.h` 中：

```c
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
#define APP_STATUS_UPLOAD_INTERVAL_MS 100U
```

当前属性上传间隔为 100 ms。如果需要降低云端流量，可改为 `1000U` 或 `5000U`。

## 构建

安装 GNU Arm Embedded Toolchain 和 GNU Make 后执行：

```powershell
make -j4
```

构建产物生成在 `build/` 目录：

```text
build/ESP_12f.elf
build/ESP_12f.hex
build/ESP_12f.bin
```

`build/` 已被 Git 忽略。

## 运行流程

启动后，ESP-12F 状态机按以下顺序运行：

```text
AT
AT+CWMODE=1
AT+CWJAP="SSID","PASSWORD"
AT+CIPMUX=0
AT+CIPSTART="TCP","dmp-mqtt.cuiot.cn",1883
MQTT CONNECT
MQTT SUBSCRIBE property/set
MQTT PUBLISH property/pub
```

如果 Wi-Fi、TCP 或 MQTT 失败，固件等待 5 秒后重试。

## 平台说明

- MQTT 客户端不会直接与 PC 或手机通信。设备和用户客户端都连接到云端 Broker。
- 格物 Topic 中的 `$sys` 前缀是必需的。
- `smokeConcentration` 是产品模型属性标识，应放在载荷中，而不是 Topic 中。
- 固件已订阅 `property/set`，当前支持解析 `dbmLimit` 阈值设置。收到后会更新本地烟雾报警阈值，并向 `property/set_reply` 返回执行结果。
- 当 `smokeConcentration` 大于或等于 `dbmLimit` 时，固件会打开 PA8 蜂鸣器；低于阈值时关闭蜂鸣器。

## 模块文档

- [App 应用层概览](App/README.md)
- [应用配置](App/README_app_config.md)
- [UART 桥接](App/README_uart_bridge.md)
- [ESP-12F 模块](App/README_esp12f.md)
- [MQTT 协议与格物 Topic](App/README_mqtt.md)
