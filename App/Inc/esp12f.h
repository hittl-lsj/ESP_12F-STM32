#ifndef ESP12F_H
#define ESP12F_H

#include <stdint.h>

void ESP12F_OnRxByte(uint8_t data);
void ESP12F_Task(void);
uint8_t ESP12F_IsConnected(void);

#endif
