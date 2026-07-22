#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

/* 初始化 USART1/USART2 的单字节中断接收。
 * USART1 面向 PC 调试串口，USART2 面向 ESP-12F AT 串口。
 */
void UART_Bridge_Init(void);

/* 在主循环中搬运两个方向的串口数据。
 * PC -> ESP 的数据只转发；ESP -> PC 的数据会同时交给 ESP12F_OnRxByte() 解析。
 */
void UART_Bridge_Task(void);

#endif
