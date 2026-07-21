# ESP-12F Communication Module

Files:

- `App/Inc/esp12f.h`
- `App/Src/esp12f.c`

This module controls the ESP-12F through ESP8266 AT commands and runs the Wi-Fi, TCP, and MQTT state machine.

## Public API

```c
void ESP12F_OnRxByte(uint8_t data);
void ESP12F_Task(void);
uint8_t ESP12F_IsConnected(void);
void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t smoke_percent);
```

`ESP12F_OnRxByte()` consumes bytes from ESP USART output. It parses AT response text, ESP `+IPD,<length>:` frames, and MQTT packets.

`ESP12F_Task()` advances the connection state machine and sends periodic MQTT property reports.

`ESP12F_SetStatus()` updates the LED, buzzer, and smoke values used in the next property upload.

## Connection State Machine

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

Important timeouts:

- Boot delay: 2 seconds.
- AT check: 2 seconds.
- Wi-Fi join: 30 seconds.
- TCP connect: 10 seconds.
- `AT+CIPSEND` prompt: 3 seconds.
- MQTT send completion: 5 seconds.
- Retry delay after failure: 5 seconds.

## MQTT Publish

In READY state, the firmware publishes the smoke property at `APP_STATUS_UPLOAD_INTERVAL_MS`:

```json
{"messageId":"123","params":{"key":"smokeConcentration","value":27}}
```

The publish topic is:

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/pub
```

## MQTT Receive

ESP AT firmware delivers TCP data as:

```text
+IPD,<mqtt_packet_length>:<mqtt_binary_packet>
```

The module parses MQTT packets and currently recognizes:

- CONNACK
- SUBACK
- PUBLISH

The current downlink subscription topic is:

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/set
```

The existing command executor still recognizes the old plain-text test commands:

```text
LED ON
LED OFF
BUZZER ON
BUZZER OFF
```

Gewu `property/set` JSON parsing has not been implemented yet.

## Keepalive

When MQTT is idle, the module sends PINGREQ according to `APP_MQTT_KEEP_ALIVE_SECONDS`.

## Limits

- Plain TCP MQTT only; TLS is not implemented.
- MQTT QoS is encoded as QoS 0 in firmware publishes/subscribes.
- The MQTT implementation covers only the packet types needed by this project.
- Long payloads or extra topics may require increasing MQTT buffer sizes.
