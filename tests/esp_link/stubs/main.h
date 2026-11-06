#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_RX 1U
#define HAL_UART_ERROR_NONE 0U
#define HAL_OK 0U
#define HAL_BUSY 1U
#define USART1_IRQn 37
#define USART3_IRQn 39
extern uint32_t tick, irq_mask, clear_calls, enable_calls, rx_calls, tx_calls;
typedef struct {uint32_t remaining,pending;} DMA_HandleTypeDef;
typedef struct { uint32_t RxState, ErrorCode; DMA_HandleTypeDef *hdmarx;uint32_t event; } UART_HandleTypeDef;
#define HAL_UART_ERROR_DMA 16U
#define HAL_UART_RXEVENT_TC 1U
#define HAL_UART_RXEVENT_IDLE 2U
#define HAL_UART_RXEVENT_HT 3U
#define DMA_FLAG_TCIF1_5 1U
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->remaining)
#define __HAL_DMA_GET_FLAG(dma,flag) ((void)(flag),(dma)->pending)
uint32_t HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *);
uint32_t HAL_UART_AbortReceive(UART_HandleTypeDef *);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *,uint16_t);
extern UART_HandleTypeDef huart1;
static inline uint32_t __get_PRIMASK(void) { return irq_mask; }
static inline void __disable_irq(void) { irq_mask = 1; }
static inline void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }
uint32_t HAL_GetTick(void);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_DisableIRQ(int irq);
uint32_t HAL_UART_Abort(UART_HandleTypeDef *uart);
void mock_clear(UART_HandleTypeDef *uart);
#define __HAL_UART_CLEAR_OREFLAG(uart) mock_clear(uart)
uint32_t HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *buffer, uint16_t size);
uint32_t HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *buffer, uint16_t size);
#endif
