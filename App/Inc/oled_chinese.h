#ifndef OLED_CHINESE_H
#define OLED_CHINESE_H

#include <stdint.h>

/* 16x16 中文点阵字模。
 * 每个汉字 32 字节：前 16 字节为上半页，后 16 字节为下半页；
 * 每字节低位对应页面内更靠上的像素。
 */
#define OLED_CHINESE_WIDTH  16U
#define OLED_CHINESE_HEIGHT 16U

typedef struct
{
  char utf8[4];      /* UTF-8 汉字，3 字节加字符串结束符。 */
  uint8_t data[32];  /* 16x16 点阵数据。 */
} OLED_Chinese16x16_t;

extern const OLED_Chinese16x16_t OLED_ChineseFont[];
extern const uint8_t OLED_ChineseFontCount;

#endif
