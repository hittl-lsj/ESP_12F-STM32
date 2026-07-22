#include "oled.h"
#include "oled_font.h"
#include "oled_chinese.h"

#include <stdlib.h>
#include <string.h>

/* SSD1306 类控制器按“页”组织显存，每页包含 8 行像素。 */
#define OLED_PAGES (OLED_HEIGHT / 8U)

/* 单色帧缓冲区：
 * index = x + page * OLED_WIDTH；
 * 每个字节的 bit0-bit7 对应该页从上到下的 8 个像素。
 *
 * 所有绘图函数只修改该缓冲区，调用 OLED_UpdateScreen() 后才真正刷新面板。
 */
static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];
static uint8_t oled_cursor_x;
static uint8_t oled_cursor_y;

/* 使用 GPIO 模拟 SPI，按 MSB First 写出一个字节。
 * OLED 没有 MISO 返回线，因此这里只需要时钟和数据输出。
 */
static void OLED_SPI_WriteByte(uint8_t data)
{
  uint8_t bit;

  for (bit = 0; bit < 8U; bit++)
  {
    HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OLED_MOSI_GPIO_Port, OLED_MOSI_Pin,
                      (data & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, GPIO_PIN_SET);
    data <<= 1;
  }
}

/* 向 OLED 连续写入命令或数据。
 * is_data = 0：DC 拉低，发送控制器命令；
 * is_data != 0：DC 拉高，发送显存数据。
 */
static void OLED_Write(uint8_t is_data, const uint8_t *data, uint16_t size)
{
  HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin,
                    is_data ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);

  while (size-- > 0U)
  {
    OLED_SPI_WriteByte(*data++);
  }

  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

/* 发送单字节控制命令。 */
static void OLED_Command(uint8_t command)
{
  OLED_Write(0U, &command, 1U);
}

void OLED_Init(void)
{
  /* 初始化序列面向 128x32 SSD1306 类屏幕：
   * 关闭显示、设置时钟和复用率、配置偏移和寻址模式、打开电荷泵、
   * 设置扫描方向/COM 引脚/对比度，最后打开显示。
   */
  static const uint8_t init_commands[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
    0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E, 0xAF
  };

  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, GPIO_PIN_SET);

  /* RST 低脉冲复位控制器。 */
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);

  OLED_Write(0U, init_commands, sizeof(init_commands));
  OLED_Clear(OLED_COLOR_BLACK);
  OLED_UpdateScreen();
}

void OLED_UpdateScreen(void)
{
  /* 0x21 设置列地址范围，0x22 设置页地址范围。
   * 初始化时已选择水平寻址模式，因此之后可以一次性写完整块显存。
   */
  static const uint8_t address_commands[] = {
    0x21, 0x00, OLED_WIDTH - 1U,
    0x22, 0x00, OLED_PAGES - 1U
  };

  OLED_Write(0U, address_commands, sizeof(address_commands));
  OLED_Write(1U, oled_buffer, sizeof(oled_buffer));
}

void OLED_Clear(OLED_Color_t color)
{
  memset(oled_buffer, color == OLED_COLOR_WHITE ? 0xFF : 0x00, sizeof(oled_buffer));
  oled_cursor_x = 0U;
  oled_cursor_y = 0U;
}

void OLED_Fill(OLED_Color_t color)
{
  OLED_Clear(color);
}

void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color)
{
  uint16_t index;
  uint8_t mask;

  if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
  {
    return;
  }

  index = x + ((uint16_t)(y / 8U) * OLED_WIDTH);
  mask = (uint8_t)(1U << (y % 8U));

  if (color == OLED_COLOR_WHITE)
  {
    oled_buffer[index] |= mask;
  }
  else
  {
    oled_buffer[index] &= (uint8_t)~mask;
  }
}

void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OLED_Color_t color)
{
  /* Bresenham 直线算法只使用整数加减，适合没有硬件浮点单元的 MCU。 */
  int16_t dx = (int16_t)abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = (int16_t)-abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;

  for (;;)
  {
    /* 允许调用者传入屏幕外坐标，但只绘制落在可见范围内的点。 */
    if ((x0 >= 0) && (y0 >= 0) &&
        (x0 < (int16_t)OLED_WIDTH) && (y0 < (int16_t)OLED_HEIGHT))
    {
      OLED_DrawPixel((uint8_t)x0, (uint8_t)y0, color);
    }

    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }
    if ((2 * error) >= dy)
    {
      error += dy;
      x0 += sx;
    }
    if ((2 * error) <= dx)
    {
      error += dx;
      y0 += sy;
    }
  }
}

void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                        OLED_Color_t color)
{
  if ((width == 0U) || (height == 0U))
  {
    return;
  }

  OLED_DrawLine(x, y, (int16_t)x + width - 1, y, color);
  OLED_DrawLine(x, (int16_t)y + height - 1,
                (int16_t)x + width - 1, (int16_t)y + height - 1, color);
  OLED_DrawLine(x, y, x, (int16_t)y + height - 1, color);
  OLED_DrawLine((int16_t)x + width - 1, y,
                (int16_t)x + width - 1, (int16_t)y + height - 1, color);
}

void OLED_FillRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                        OLED_Color_t color)
{
  uint16_t end_x = (uint16_t)x + width;
  uint16_t end_y = (uint16_t)y + height;
  uint16_t px;
  uint16_t py;

  /* 使用 uint16_t 计算终点，避免 x + width 在 uint8_t 范围内提前回绕。 */
  for (py = y; (py < end_y) && (py < OLED_HEIGHT); py++)
  {
    for (px = x; (px < end_x) && (px < OLED_WIDTH); px++)
    {
      OLED_DrawPixel((uint8_t)px, (uint8_t)py, color);
    }
  }
}

void OLED_SetCursor(uint8_t x, uint8_t y)
{
  oled_cursor_x = x;
  oled_cursor_y = y;
}

char OLED_WriteChar(char ch, OLED_Color_t color)
{
  uint8_t column;
  uint8_t row;
  uint8_t pixels;

  /* 字库只包含 ASCII 0x20-0x7E，其他字符使用 '?' 代替。 */
  if ((ch < ' ') || (ch > '~'))
  {
    ch = '?';
  }

  /* 每个字符实际占 OLED_FONT_WIDTH + 1 列，最后一列作为字符间距。 */
  if ((oled_cursor_x + OLED_FONT_WIDTH >= OLED_WIDTH) ||
      (oled_cursor_y + OLED_FONT_HEIGHT > OLED_HEIGHT))
  {
    return 0;
  }

  for (column = 0U; column < OLED_FONT_WIDTH; column++)
  {
    pixels = OLED_Font5x7[(uint8_t)ch - 32U][column];
    for (row = 0U; row < OLED_FONT_HEIGHT; row++)
    {
      OLED_DrawPixel((uint8_t)(oled_cursor_x + column),
                     (uint8_t)(oled_cursor_y + row),
                     (pixels & (1U << row)) ? color : (OLED_Color_t)!color);
    }
  }

  /* 使用反色清除字符间距，避免覆盖旧内容时留下残影。 */
  for (row = 0U; row < OLED_FONT_HEIGHT; row++)
  {
    OLED_DrawPixel((uint8_t)(oled_cursor_x + OLED_FONT_WIDTH),
                   (uint8_t)(oled_cursor_y + row),
                   (OLED_Color_t)!color);
  }

  oled_cursor_x += OLED_FONT_WIDTH + 1U;
  return ch;
}

void OLED_WriteString(const char *str, OLED_Color_t color)
{
  if (str == NULL)
  {
    return;
  }

  while ((*str != '\0') && (OLED_WriteChar(*str, color) != 0))
  {
    str++;
  }
}

uint8_t OLED_WriteChinese16x16(const char *str, OLED_Color_t color)
{
  uint8_t index;
  uint8_t column;
  uint8_t row;
  uint8_t found;
  uint8_t pixels;

  if (str == NULL)
  {
    return 0U;
  }

  while (*str != '\0')
  {
    if ((oled_cursor_x + OLED_CHINESE_WIDTH > OLED_WIDTH) ||
        (oled_cursor_y + OLED_CHINESE_HEIGHT > OLED_HEIGHT))
    {
      return 0U;
    }

    found = 0U;
    for (index = 0U; index < OLED_ChineseFontCount; index++)
    {
      /* 当前字库面向常用三字节 UTF-8 汉字，逐个比较 3 个编码字节。 */
      if (memcmp(str, OLED_ChineseFont[index].utf8, 3U) == 0)
      {
        found = 1U;

        for (column = 0U; column < OLED_CHINESE_WIDTH; column++)
        {
          for (row = 0U; row < OLED_CHINESE_HEIGHT; row++)
          {
            /* 每 8 行组成一页：row / 8 选择上下页，column 选择页内列。 */
            pixels = OLED_ChineseFont[index].data[
              column + ((row / 8U) * OLED_CHINESE_WIDTH)];
            OLED_DrawPixel((uint8_t)(oled_cursor_x + column),
                           (uint8_t)(oled_cursor_y + row),
                           (pixels & (1U << (row % 8U))) ?
                           color : (OLED_Color_t)!color);
          }
        }

        oled_cursor_x += OLED_CHINESE_WIDTH;
        str += 3;
        break;
      }
    }

    if (found == 0U)
    {
      return 0U;
    }
  }

  return 1U;
}

void OLED_SetContrast(uint8_t contrast)
{
  OLED_Command(0x81);
  OLED_Command(contrast);
}

void OLED_DisplayOn(void)
{
  OLED_Command(0xAF);
}

void OLED_DisplayOff(void)
{
  OLED_Command(0xAE);
}

void OLED_Invert(uint8_t enable)
{
  OLED_Command(enable ? 0xA7 : 0xA6);
}
