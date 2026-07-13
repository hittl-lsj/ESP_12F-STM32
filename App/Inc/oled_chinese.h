#ifndef OLED_CHINESE_H
#define OLED_CHINESE_H

#include <stdint.h>

#define OLED_CHINESE_WIDTH  16U
#define OLED_CHINESE_HEIGHT 16U

typedef struct
{
  char utf8[4];
  uint8_t data[32];
} OLED_Chinese16x16_t;

extern const OLED_Chinese16x16_t OLED_ChineseFont[];
extern const uint8_t OLED_ChineseFontCount;

#endif
