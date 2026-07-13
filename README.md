# ESP-12F STM32 TCP Bridge

STM32F103 uses USART2 to control an ESP-12F (ESP8266 AT firmware), and USART1 as a PC serial bridge. After boot, the firmware joins Wi-Fi, connects to the configured TCP server, and accepts GPIO commands.

## Features

- USART1 <-> USART2 transparent serial bridge
- ESP8266 AT command response parsing
- Automatic Wi-Fi/TCP connection and retry
- TCP control of PB8 LED and PA8 buzzer
- CubeMX project, GNU Arm Makefile, and HAL drivers

## Hardware

| STM32 pin | Connection |
|---|---|
| PA2 | ESP-12F RX (USART2_TX) |
| PA3 | ESP-12F TX (USART2_RX) |
| PA9 | USB-TTL RX (USART1_TX) |
| PA10 | USB-TTL TX (USART1_RX) |
| PB8 | LED or other active-high output |
| PA8 | Buzzer control input, active high |
| GND | Common ground |

PA8 and PB8 are push-pull outputs and start low. Drive a buzzer through a suitable transistor or driver if its current exceeds the STM32 GPIO rating; do not connect a power buzzer directly to the pin.

## Configuration

```powershell
Copy-Item App/Inc/app_secrets.example.h App/Inc/app_secrets.h
```

Set Wi-Fi credentials and the TCP server address in `App/Inc/app_secrets.h`. This file is ignored by Git.

GPIO mappings are kept in `App/Inc/app_config.h`:

```c
#define APP_LED_GPIO_PORT    GPIOB
#define APP_LED_GPIO_PIN     GPIO_PIN_8
#define APP_BUZZER_GPIO_PORT GPIOA
#define APP_BUZZER_GPIO_PIN  GPIO_PIN_8
```

## TCP commands

Send the following plain ASCII commands over the TCP connection. The ESP AT `+IPD` length prefix is handled by the firmware.

| Command | Action |
|---|---|
| `LED ON` | Set PB8 high |
| `LED OFF` | Set PB8 low |
| `BUZZER ON` | Set PA8 high |
| `BUZZER OFF` | Set PA8 low |

Example PowerShell TCP client:

```powershell
$client = [Net.Sockets.TcpClient]::new("STM32_IP", 8080)
$stream = $client.GetStream()
$data = [Text.Encoding]::ASCII.GetBytes("BUZZER ON")
$stream.Write($data, 0, $data.Length)
$client.Close()
```

## PC serial commands

The PC connected to USART1 can send any bytes; the bridge forwards them unchanged to USART2/ESP-12F. For manual setup or diagnostics, these are the AT commands issued automatically by the firmware (each line ends with CRLF):

| Order | Command | Purpose |
|---:|---|---|
| 1 | `AT` | Check that the ESP responds |
| 2 | `AT+CWMODE=1` | Select station mode |
| 3 | `AT+CWJAP="SSID","PASSWORD"` | Join the configured Wi-Fi network |
| 4 | `AT+CIPMUX=0` | Select single TCP connection mode |
| 5 | `AT+CIPSTART="TCP","SERVER_IP",PORT` | Connect to the configured TCP server |

After the TCP connection is established, the PC can send the same payload commands listed above (`LED ON`, `LED OFF`, `BUZZER ON`, and `BUZZER OFF`) through the TCP server. USART1 also displays ESP responses, including `OK`, `ERROR`, `WIFI CONNECTED`, `WIFI GOT IP`, `CONNECT`, and `CLOSED`.

For direct ESP testing, send `AT+CIPSEND` only when manually using the ESP AT protocol; the STM32 automatic state machine already manages connection setup and forwards TCP payloads.

## Build

Install the GNU Arm Embedded toolchain and GNU Make, then run:

```powershell
make -j4
```

Outputs are written to `build/ESP_12f.elf`, `build/ESP_12f.hex`, and `build/ESP_12f.bin`.

## Documentation

- [Application overview](App/README.md)
- [Application configuration](App/README_app_config.md)
- [UART bridge](App/README_uart_bridge.md)
- [ESP-12F module](App/README_esp12f.md)

The current `+IPD` parser is intended for short text commands. A production protocol should use length-driven packet parsing.
