#include "oled.h"
#include "oled_font.h"
#include "oled_chinese.h"
#include <stdlib.h>
#include <string.h>

#define OLED_PAGES (OLED_HEIGHT / 8U)

static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];
static uint8_t oled_cursor_x;
static uint8_t oled_cursor_y;

static void OLED_SPI_WriteByte(uint8_t data)//写入字节
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

static void OLED_Write(uint8_t is_data, const uint8_t *data, uint16_t size)//写入数据
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

static void OLED_Command(uint8_t command)//写入命令
{
  OLED_Write(0U, &command, 1U);
}

void OLED_Init(void)//初始化OLED
{
  static const uint8_t init_commands[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
    0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E, 0xAF
  };

  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);

  OLED_Write(0U, init_commands, sizeof(init_commands));
  OLED_Clear(OLED_COLOR_BLACK);
  OLED_UpdateScreen();
}

void OLED_UpdateScreen(void)//更新屏幕
{
  static const uint8_t address_commands[] = {
    0x21, 0x00, OLED_WIDTH - 1U,
    0x22, 0x00, OLED_PAGES - 1U
  };

  OLED_Write(0U, address_commands, sizeof(address_commands));
  OLED_Write(1U, oled_buffer, sizeof(oled_buffer));
}

void OLED_Clear(OLED_Color_t color)//清除屏幕
{
  memset(oled_buffer, color == OLED_COLOR_WHITE ? 0xFF : 0x00, sizeof(oled_buffer));
  oled_cursor_x = 0U;
  oled_cursor_y = 0U;
}

void OLED_Fill(OLED_Color_t color)//填充屏幕
{
  OLED_Clear(color);
}

void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color)//绘制像素点
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
  int16_t dx = (int16_t)abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = (int16_t)-abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;

  for (;;)
  {
    if ((x0 >= 0) && (y0 >= 0) && (x0 < (int16_t)OLED_WIDTH) && (y0 < (int16_t)OLED_HEIGHT))
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

void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color)
{
  if ((width == 0U) || (height == 0U))
  {
    return;
  }
  OLED_DrawLine(x, y, (int16_t)x + width - 1, y, color);
  OLED_DrawLine(x, (int16_t)y + height - 1, (int16_t)x + width - 1, (int16_t)y + height - 1, color);
  OLED_DrawLine(x, y, x, (int16_t)y + height - 1, color);
  OLED_DrawLine((int16_t)x + width - 1, y, (int16_t)x + width - 1, (int16_t)y + height - 1, color);
}

void OLED_FillRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color)
{
  uint16_t end_x = (uint16_t)x + width;
  uint16_t end_y = (uint16_t)y + height;
  uint16_t px;
  uint16_t py;

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

  if ((ch < ' ') || (ch > '~'))
  {
    ch = '?';
  }
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
      OLED_DrawPixel((uint8_t)(oled_cursor_x + column), (uint8_t)(oled_cursor_y + row),
                     (pixels & (1U << row)) ? color : (OLED_Color_t)!color);
    }
  }
  for (row = 0U; row < OLED_FONT_HEIGHT; row++)
  {
    OLED_DrawPixel((uint8_t)(oled_cursor_x + OLED_FONT_WIDTH), (uint8_t)(oled_cursor_y + row),
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
      if (memcmp(str, OLED_ChineseFont[index].utf8, 3U) == 0)
      {
        found = 1U;
        for (column = 0U; column < OLED_CHINESE_WIDTH; column++)
        {
          for (row = 0U; row < OLED_CHINESE_HEIGHT; row++)
          {
            pixels = OLED_ChineseFont[index].data[column + ((row / 8U) * OLED_CHINESE_WIDTH)];
            OLED_DrawPixel((uint8_t)(oled_cursor_x + column), (uint8_t)(oled_cursor_y + row),
                           (pixels & (1U << (row % 8U))) ? color : (OLED_Color_t)!color);
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
