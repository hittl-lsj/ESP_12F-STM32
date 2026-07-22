#include "uart_bridge.h"

#include "app_config.h"
#include "esp12f.h"
#include "usart.h"

/* 单生产者、单消费者环形缓冲区。
 *
 * 生产者：HAL_UART_RxCpltCallback() 串口接收中断。
 * 消费者：主循环中的 UART_Bridge_Task()。
 *
 * head 指向下一次写入位置，tail 指向下一次读取位置。
 * 环形缓冲区始终保留一个空槽，用 head 的下一位置等于 tail 表示“已满”。
 */
typedef struct
{
  uint8_t data[APP_UART_BRIDGE_BUFFER_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint32_t overflow_count;  /* 缓冲区满时丢弃字节的累计次数。 */
} UART_BridgeBuffer;

/* HAL 的中断接收接口每次只接收 1 字节，因此为两个串口各保留一个接收槽。 */
static uint8_t uart1_rx_byte;
static uint8_t uart2_rx_byte;

/* PC -> ESP 和 ESP -> PC 使用独立缓冲区，避免两个方向相互阻塞。 */
static UART_BridgeBuffer uart1_to_uart2;
static UART_BridgeBuffer uart2_to_uart1;

/* 在接收中断中向环形缓冲区压入一个字节。
 * 缓冲区已满时不能等待主循环消费，只记录溢出并丢弃当前字节。
 */
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

/* 从一个环形缓冲区取出并转发最多 1 个字节。
 *
 * 每次任务调用只发一个字节，可以限制单次阻塞时间，让主循环继续处理 ADC、
 * ESP 状态机和 OLED。主循环运行足够快时，队列仍会持续排空。
 *
 * parse_esp_data 非 0 表示数据来自 ESP-12F。只有字节成功转发到 PC 后，
 * 才把它交给 ESP12F_OnRxByte()，并推进 tail，保证失败时可以下次重试。
 */
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
  /* 启动两个串口的单字节中断接收。每次接收完成后会进入
   * HAL_UART_RxCpltCallback()，回调末尾必须再次调用 Receive_IT 才能继续接收。
   */
  HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);
  HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
}

void UART_Bridge_Task(void)
{
  /* USART1 是 PC 调试口：PC 输入可直接穿透到 ESP，便于手工测试 AT 指令。 */
  UART_BridgeForward(&uart1_to_uart2, &huart2, 0U);

  /* USART2 是 ESP 口：ESP 输出既转发给 PC 查看，也送入协议解析器。 */
  UART_BridgeForward(&uart2_to_uart1, &huart1, 1U);
}

/* HAL 串口接收完成回调。
 * 回调运行在中断上下文中，因此只做快速入队和重新挂接接收，不在这里发送或解析协议。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    UART_BridgePush(&uart1_to_uart2, uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1U);
  }
  else if (huart->Instance == USART2)
  {
    UART_BridgePush(&uart2_to_uart1, uart2_rx_byte);
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
  }
}

/* 串口发生帧错误、溢出等异常后，HAL 可能停止当前中断接收。
 * 这里重新挂接对应串口的单字节接收，使桥接可以继续工作。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
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
