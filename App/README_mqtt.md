# MQTT 与格物 Topic

固件通过 ESP-12F TCP 模式连接中国联通格物 MQTT Broker，并发送由 `App/Src/mqtt.c` 生成的 MQTT 3.1.1 报文。

## Broker

```text
Host: dmp-mqtt.cuiot.cn
Port: 1883
TLS:  disabled
```

## 凭据格式

当前采用一机一密登录：

```text
clientId = {deviceId}|{productKey}|{signMethod}|{authType}|{operator}
username = {deviceKey}|{productKey}
password = hmac_sha256(deviceId + deviceKey + productKey, deviceSecret)
```

本固件使用：

```text
signMethod = 0
authType   = 0
operator   = 0
```

真实密码只保存在 `App/Inc/app_secrets.h`，该文件已被 Git 忽略。

## 属性上报

请求 Topic：

```text
$sys/{productKey}/{deviceKey}/property/pub
```

当前设备 Topic：

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/pub
```

格物要求的载荷格式：

```json
{
  "messageId": "123",
  "params": {
    "key": "smokeConcentration",
    "value": 27
  }
}
```

`smokeConcentration` 是产品模型属性标识，应放在 `params.key` 中，而不是 MQTT Topic 中。

响应 Topic：

```text
$sys/{productKey}/{deviceKey}/property/pub_reply
```

固件当前未订阅或处理 `property/pub_reply`。

## 属性设置下行

下行 Topic：

```text
$sys/{productKey}/{deviceKey}/property/set
```

当前设备 Topic：

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/set
```

固件在 MQTT CONNECT 后会订阅该 Topic。当前解析器尚未解码格物 JSON 下行载荷，仍只处理本地 MQTT 测试阶段使用的旧纯文本命令。

## 批量属性上报

格物还支持：

```text
$sys/{productKey}/{deviceKey}/property/batch
$sys/{productKey}/{deviceKey}/property/batch_reply
```

当前固件只使用单属性上传。

## MQTT.fx / Mosquitto 测试

可以在 PC 上使用相同凭据测试云端上报：

```powershell
mosquitto_pub -h dmp-mqtt.cuiot.cn -p 1883 -V mqttv311 `
  -i "DEVICE_ID|PRODUCT_KEY|0|0|0" `
  -u "DEVICE_KEY|PRODUCT_KEY" `
  -P "HMAC_SHA256_PASSWORD" `
  -t '$sys/PRODUCT_KEY/DEVICE_KEY/property/pub' `
  -m '{"messageId":"123","params":{"key":"smokeConcentration","value":27}}' `
  -q 1 -d
```

真实设备在线时，不要使用相同 client ID 运行 PC 测试客户端，除非你明确希望临时断开真实设备。
