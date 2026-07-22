#ifndef ESP12F_H
#define ESP12F_H

#include <stdint.h>

/* ESP-12F 应用驱动接口。
 *
 * 该模块负责：
 * 1. 通过 USART2 发送 ESP8266 AT 指令；
 * 2. 建立 Wi-Fi、TCP 和 MQTT 连接；
 * 3. 周期性上报烟雾浓度属性；
 * 4. 接收云端下行 MQTT 消息，并执行本地测试命令。
 *
 * 模块内部使用非阻塞状态机，调用者只需要在主循环中持续调用
 * ESP12F_Task()，并把 ESP 串口收到的每个字节交给 ESP12F_OnRxByte()。
 */

/* 处理从 ESP-12F 串口收到的一个字节。
 * data: USART2 接收到的原始字节，可能属于 AT 文本响应或 +IPD MQTT 数据帧。
 */
void ESP12F_OnRxByte(uint8_t data);

/* 推进 ESP-12F 自动连接和 MQTT 上报状态机。
 * 该函数应在主循环中高频调用，内部通过 HAL_GetTick() 控制超时和周期任务。
 */
void ESP12F_Task(void);

/* 查询 MQTT 是否已经完成连接和订阅。
 * 返回 1 表示状态机处于 READY 状态，可认为云端链路可用；否则返回 0。
 */
uint8_t ESP12F_IsConnected(void);

/* 更新下一次 MQTT 属性上报使用的本地状态。
 * led_on: LED 是否点亮，1 为点亮，0 为熄灭。
 * buzzer_on: 蜂鸣器是否鸣叫，1 为鸣叫，0 为关闭。
 * smoke_percent: 烟雾相对百分比，超过 100 会被限制为 100。
 */
void ESP12F_SetStatus(uint8_t led_on, uint8_t buzzer_on, uint8_t smoke_percent);

#endif
