#ifndef OLED_FONT_H
#define OLED_FONT_H

#include <stdint.h>

/* 5x7 ASCII 字体宽高。
 * 字模数据按列存储，OLED_WriteChar() 会额外绘制 1 列空白作为字符间距。
 */
#define OLED_FONT_WIDTH  5U
#define OLED_FONT_HEIGHT 7U

/* ASCII 0x20 到 0x7E 的 5x7 点阵字体表。 */
extern const uint8_t OLED_Font5x7[95][5];

#endif
