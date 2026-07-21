# MQTT 通信

固件在 ESP-12F 建立 TCP 连接后，以 MQTT 3.1.1 客户端身份连接 Broker。当前支持：

- CONNECT / CONNACK
- QoS 0 PUBLISH
- QoS 0 SUBSCRIBE / SUBACK
- PINGREQ 保活
- TCP 或 MQTT 建连失败后自动重连

## 配置

在 `App/Inc/app_secrets.h` 中配置：

```c
#define APP_SERVER_IP          "192.168.1.100"
#define APP_SERVER_PORT        "1883"
#define APP_MQTT_CLIENT_ID     "stm32-esp12f"
#define APP_MQTT_USERNAME      ""
#define APP_MQTT_PASSWORD      ""
#define APP_MQTT_STATUS_TOPIC  "stm32/esp12f/status"
#define APP_MQTT_COMMAND_TOPIC "stm32/esp12f/command"
```

`APP_SERVER_IP` 是 MQTT Broker 地址，不是普通 TCP 回显服务器地址。未配置新增的 MQTT 宏时会使用 `app_config.h` 中的默认值。不同设备必须使用不同的 `APP_MQTT_CLIENT_ID`，否则 Broker 会断开使用相同 ID 的旧连接。

## 消息

设备每秒向状态主题发布一次 QoS 0 JSON：

```json
{"led":"ON","buzzer":"OFF","smoke":27}
```

向命令主题发布以下 QoS 0 文本可控制 GPIO：

```text
LED ON
LED OFF
BUZZER ON
BUZZER OFF
```

命令区分大小写，载荷中不要带多余空格或换行。

## 本机 Broker 验证

仓库提供了 `mosquitto-test.conf`：

```text
listener 1883
allow_anonymous true
```

在电脑上启动 Broker：

```powershell
mosquitto -c .\mosquitto-test.conf -v
```

STM32 固件里的 `APP_SERVER_IP` 必须配置为这台电脑在 Wi-Fi 局域网中的 IPv4 地址。运行在同一台电脑上的测试客户端可以使用 `127.0.0.1` 连接 Broker：

```powershell
mosquitto_sub -h 127.0.0.1 -t stm32/esp12f/status -v
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "LED ON"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "LED OFF"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "BUZZER ON"
mosquitto_pub -h 127.0.0.1 -t stm32/esp12f/command -m "BUZZER OFF"
```

也可以使用项目自带 PowerShell 控制台：

```powershell
.\esp_server.ps1 -Broker 127.0.0.1
```

如果控制台运行在另一台电脑上，`-Broker` 应改成 Broker 的局域网 IPv4 地址。

## 预期现象

- `mosquitto_sub` 或 `esp_server.ps1` 每秒收到一条状态 JSON。
- 发布 `LED ON/OFF` 后，PB8 LED 状态切换，并在下一条状态 JSON 中体现。
- 发布 `BUZZER ON/OFF` 后，PA8 蜂鸣器状态切换，并在下一条状态 JSON 中体现。
- 改变 PA0 烟雾传感器 AO 电压时，状态 JSON 中的 `smoke` 和 OLED 上的 `SMOKE` 数值变化。
- 关闭 Broker 后串口会看到连接断开；重新启动 Broker 后设备会按 5 秒重试间隔自动恢复连接。

## 限制

当前实现使用明文 TCP，未实现 TLS；消息发送和订阅均按 QoS 0 设计。`PUBLISH` 接收会验证 MQTT 包长度和主题边界，异常报文会被忽略。
