#ifndef OLED_H
#define OLED_H

#include "main.h"
#include <stdint.h>

#define OLED_WIDTH  128U
#define OLED_HEIGHT 32U

typedef enum
{
  OLED_COLOR_BLACK = 0,
  OLED_COLOR_WHITE = 1
} OLED_Color_t;

void OLED_Init(void);
void OLED_UpdateScreen(void);
void OLED_Clear(OLED_Color_t color);
void OLED_Fill(OLED_Color_t color);
void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color);
void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OLED_Color_t color);
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color);
void OLED_FillRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color);
void OLED_SetCursor(uint8_t x, uint8_t y);
char OLED_WriteChar(char ch, OLED_Color_t color);
void OLED_WriteString(const char *str, OLED_Color_t color);
uint8_t OLED_WriteChinese16x16(const char *str, OLED_Color_t color);
void OLED_SetContrast(uint8_t contrast);
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);
void OLED_Invert(uint8_t enable);

#endif
