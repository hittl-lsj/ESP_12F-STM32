# STM32F103 + ESP-12F Gewu MQTT Smoke Node

This project is an STM32F103C8T6 firmware for a smoke sensor node. The STM32 controls an ESP-12F module through ESP8266 AT commands, connects to the China Unicom Gewu MQTT platform, and reports the `smokeConcentration` property defined in the product model.

The current firmware uses plain MQTT/TCP on `dmp-mqtt.cuiot.cn:1883`.

## Features

- STM32F103C8T6 HAL project generated from STM32CubeMX.
- ESP-12F Wi-Fi/TCP control through USART2 and ESP8266 AT firmware.
- USART1 debug bridge between the PC and ESP-12F.
- MQTT 3.1.1 minimal client implementation: CONNECT, SUBSCRIBE, PUBLISH, PINGREQ.
- Smoke sensor analog sampling on PA0 / ADC1.
- LED control on PB8 and buzzer control on PA8.
- OLED status display for ESP connection, LED, buzzer, and smoke percentage.
- Gewu property report payload for `smokeConcentration`.

## Hardware Wiring

| STM32 pin | Device |
| --- | --- |
| PA2 | ESP-12F RX, USART2_TX |
| PA3 | ESP-12F TX, USART2_RX |
| PA9 | USB-TTL RX, USART1_TX |
| PA10 | USB-TTL TX, USART1_RX |
| PA0 | Smoke sensor AO, ADC1_IN0 |
| PB8 | LED, active low |
| PA8 | Buzzer, active high |
| PB10 | OLED CS |
| PB11 | OLED DC |
| PB12 | OLED RST |
| PB13 | OLED SCK |
| PB14 | OLED MOSI |
| GND | Common ground |

The smoke sensor AO voltage must not exceed 3.3 V. The ESP-12F should use a stable 3.3 V supply; 500 mA or higher supply capacity is recommended.

## Data Flow

```text
Smoke sensor AO -> STM32 ADC1/PA0 -> smoke_percent
STM32 USART2 -> ESP-12F -> Wi-Fi -> Gewu MQTT broker
Gewu platform -> device shadow / property data
```

The firmware publishes to:

```text
$sys/{productKey}/{deviceKey}/property/pub
```

For the current device:

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/pub
```

The reported payload follows the Gewu property format:

```json
{
  "messageId": "123",
  "params": {
    "key": "smokeConcentration",
    "value": 27
  }
}
```

## Configuration

Create a local secret configuration from the template:

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

Then edit `App/Inc/app_secrets.h` with:

- Wi-Fi SSID and password.
- Gewu MQTT broker, product key, device key, client ID, username, and HMAC-SHA256 password.
- Gewu property publish and property set topics.

`App/Inc/app_secrets.h` is ignored by Git and must not be committed.

Current timing defaults are in `App/Inc/app_config.h`:

```c
#define APP_SMOKE_SAMPLE_INTERVAL_MS  1U
#define APP_SMOKE_WINDOW_MS           100U
#define APP_STATUS_UPLOAD_INTERVAL_MS 100U
```

The current upload interval is 100 ms. For lower cloud traffic, change it to `1000U` or `5000U`.

## Build

Install GNU Arm Embedded Toolchain and GNU Make, then run:

```powershell
make -j4
```

Build outputs are generated under `build/`:

```text
build/ESP_12f.elf
build/ESP_12f.hex
build/ESP_12f.bin
```

`build/` is ignored by Git.

## Runtime Flow

After boot, the ESP-12F state machine runs:

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

If Wi-Fi, TCP, or MQTT fails, the firmware waits 5 seconds and retries.

## Platform Notes

- The MQTT client does not talk directly to a PC or phone. The device and user client both connect to the cloud broker.
- The `$sys` prefix in the Gewu topic is required.
- `smokeConcentration` is the product-model property identifier and belongs in the payload, not in the topic.
- The firmware currently subscribes to `property/set`, but cloud downlink JSON parsing is not yet implemented.

## Module Documentation

- [App overview](App/README.md)
- [Application configuration](App/README_app_config.md)
- [UART bridge](App/README_uart_bridge.md)
- [ESP-12F module](App/README_esp12f.md)
- [MQTT protocol and Gewu topics](App/README_mqtt.md)
