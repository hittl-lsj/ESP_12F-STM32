#include "uart_bridge.h"

#include "app_config.h"
#include "esp12f.h"
#include "usart.h"

typedef struct
{
  uint8_t data[APP_UART_BRIDGE_BUFFER_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint32_t overflow_count;
} UART_BridgeBuffer;

static uint8_t uart1_rx_byte;
static uint8_t uart2_rx_byte;
static UART_BridgeBuffer uart1_to_uart2;
static UART_BridgeBuffer uart2_to_uart1;

static void UART_BridgePush(UART_BridgeBuffer *buffer, uint8_t data)
{
  uint16_t next = (uint16_t)((buffer->head + 1U) % APP_UART_BRIDGE_BUFFER_SIZE);

  if (next == buffer->tail)
  {
    buffer->overflow_count++;
    return;
  }

  buffer->data[buffer->head] = data;
  buffer->head = next;
}

static void UART_BridgeForward(UART_BridgeBuffer *buffer,
                               UART_HandleTypeDef *destination,
                               uint8_t parse_esp_data)
{
  uint8_t data;

  if (buffer->tail == buffer->head)
  {
    return;
  }

  data = buffer->data[buffer->tail];
  if (HAL_UART_Transmit(destination, &data, 1U, 10U) == HAL_OK)
  {
    if (parse_esp_data != 0U)
    {
      ESP12F_OnRxByte(data);
    }
    buffer->tail = (uint16_t)((buffer->tail + 1U) % APP_UART_BRIDGE_BUFFER_SIZE);
  }
}

void UART_Bridge_Init(void)
{
  HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);// 启用 UART1 接收中断
  HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);// 启用 UART2 接收中断
}

void UART_Bridge_Task(void)
{
  UART_BridgeForward(&uart1_to_uart2, &huart2, 0U);// 从 UART1 向 UART2 发送数据
  UART_BridgeForward(&uart2_to_uart1, &huart1, 1U);// 从 UART2 向 UART1 发送数据
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)// 接收完成回调函数
{
  if (huart->Instance == USART1)
  {
    UART_BridgePush(&uart1_to_uart2, uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);// 启用 UART1 接收中断
  }
  else if (huart->Instance == USART2)
  {
    UART_BridgePush(&uart2_to_uart1, uart2_rx_byte);
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);// 启用 UART2 接收中断
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)// 错误回调函数
{
  if (huart->Instance == USART1)
  {
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);
  }
  else if (huart->Instance == USART2)
  {
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
  }
}
