# App Layer

The `App` directory contains application logic that is kept outside the STM32CubeMX-generated peripheral initialization code. CubeMX-generated code remains under `Core`, while connection logic, MQTT packet handling, UART bridging, OLED drawing, and application configuration live under `App`.

## Main Loop Integration

`Core/Src/main.c` initializes the application modules:

```c
UART_Bridge_Init();
OLED_Init();
Smoke_Init();
```

The main loop continuously executes:

```c
UART_Bridge_Task();
Smoke_Task();
ESP12F_SetStatus(led_on, buzzer_on, smoke_percent);
ESP12F_Task();
OLED_UpdateScreen();
```

## Module Responsibilities

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
                               |-- AT response parsing
                               |-- +IPD payload parsing
                               |-- MQTT packet parsing
                               `-- cloud downlink hook
```

## Smoke Data Path

The smoke sensor analog output is connected to PA0 / ADC1_IN0. `Smoke_Task()` samples the ADC every `APP_SMOKE_SAMPLE_INTERVAL_MS`, averages values over `APP_SMOKE_WINDOW_MS`, and converts the ADC average to a 0-100 percentage.

That percentage is passed to `ESP12F_SetStatus()`. When the ESP/MQTT state machine is ready, `ESP12F_Task()` publishes it as the Gewu `smokeConcentration` property.

## Current Cloud Payload

The firmware publishes:

```json
{"messageId":"123","params":{"key":"smokeConcentration","value":27}}
```

to:

```text
$sys/cu1e1vp51svlk8zn/X00PdoZ4luWgnux/property/pub
```

## Notes

- The main loop should stay non-blocking. Long `HAL_Delay()` calls will increase UART buffer overflow and MQTT timeout risk.
- USART1 remains useful for watching ESP AT logs.
- Cloud downlink topic subscription is present, but JSON command handling for Gewu `property/set` still needs implementation.
