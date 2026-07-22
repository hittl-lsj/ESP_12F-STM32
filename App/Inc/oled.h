#ifndef OLED_H
#define OLED_H

#include "main.h"
#include <stdint.h>

/* 本驱动按 128x32 单色 OLED 编写，显存按页组织：
 * 1 页 = 8 行像素，因此 32 行屏幕共有 4 页。
 */
#define OLED_WIDTH  128U
#define OLED_HEIGHT 32U

typedef enum
{
  OLED_COLOR_BLACK = 0,
  OLED_COLOR_WHITE = 1
} OLED_Color_t;

/* 初始化 OLED 控制器、清空显存并刷新屏幕。 */
void OLED_Init(void);

/* 将 oled_buffer 中的整屏显存写入 OLED。 */
void OLED_UpdateScreen(void);

/* 清空显存并把光标重置到左上角。 */
void OLED_Clear(OLED_Color_t color);

/* OLED_Clear() 的语义别名，便于绘图代码表达“填充整屏”。 */
void OLED_Fill(OLED_Color_t color);

/* 绘制单个像素点。超出屏幕范围的坐标会被忽略。 */
void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color);

/* 使用 Bresenham 算法绘制直线，支持部分坐标在屏幕外。 */
void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OLED_Color_t color);

/* 绘制矩形边框。 */
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color);

/* 绘制填充矩形。超出屏幕部分会自动裁剪。 */
void OLED_FillRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color);

/* 设置后续字符绘制的左上角坐标。 */
void OLED_SetCursor(uint8_t x, uint8_t y);

/* 写入一个 5x7 ASCII 字符，成功返回实际写入字符，空间不足返回 0。 */
char OLED_WriteChar(char ch, OLED_Color_t color);

/* 从当前光标连续写入 ASCII 字符串，遇到屏幕空间不足时停止。 */
void OLED_WriteString(const char *str, OLED_Color_t color);

/* 从当前光标写入 16x16 中文字模字符串。
 * 当前实现按 UTF-8 三字节汉字逐个匹配字库，成功返回 1，字库缺字或空间不足返回 0。
 */
uint8_t OLED_WriteChinese16x16(const char *str, OLED_Color_t color);

/* 设置 OLED 对比度，范围由控制器命令决定，通常为 0x00-0xFF。 */
void OLED_SetContrast(uint8_t contrast);

/* 打开/关闭 OLED 面板显示；显存内容不会被清除。 */
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);

/* 设置反显模式：enable 非 0 时白黑反转，0 时恢复正常显示。 */
void OLED_Invert(uint8_t enable);

#endif
