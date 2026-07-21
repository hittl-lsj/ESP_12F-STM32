# UART Bridge

Files:

- `App/Inc/uart_bridge.h`
- `App/Src/uart_bridge.c`

The UART bridge connects the PC debug serial port and ESP-12F serial port:

```text
PC USB-TTL <-> USART1 <-> STM32 <-> USART2 <-> ESP-12F
```

## UART Roles

| UART | Purpose |
| --- | --- |
| USART1 | PC debug serial port |
| USART2 | ESP-12F AT command serial port |

Both UARTs are configured for 115200 baud, 8 data bits, no parity, 1 stop bit.

## Receive Path

`UART_Bridge_Init()` starts interrupt-based one-byte receives for USART1 and USART2.

Received bytes are pushed into ring buffers:

```text
USART1 RX -> uart1_to_uart2 buffer
USART2 RX -> uart2_to_uart1 buffer
```

`UART_Bridge_Task()` forwards queued bytes:

```text
uart1_to_uart2 -> USART2
uart2_to_uart1 -> USART1
```

When bytes from ESP-12F are forwarded to USART1, the bridge also passes them to:

```c
ESP12F_OnRxByte(data);
```

This lets the state machine parse ESP AT responses and MQTT `+IPD` packets while still showing logs on the PC serial terminal.

## Debug Use

USART1 is useful for watching:

```text
AT
OK
WIFI CONNECTED
WIFI GOT IP
CONNECT
SEND OK
+IPD,...
```

Manual AT commands can be sent from the PC through USART1, but doing this while the automatic state machine is running can confuse response parsing. For normal operation, treat USART1 mainly as a log output.

## Buffering

The bridge uses `APP_UART_BRIDGE_BUFFER_SIZE`, currently 256 bytes. If logs are very bursty or the main loop is blocked for too long, overflow can occur.
