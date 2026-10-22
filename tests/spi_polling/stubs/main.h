#ifndef SPI_TEST_MAIN_H
#define SPI_TEST_MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef struct {uint32_t BaudRatePrescaler;} SPI_InitTypeDef;
typedef struct {SPI_InitTypeDef Init;} SPI_HandleTypeDef;
#define SPI_BAUDRATEPRESCALER_64 64
#define HAL_SPI_STATE_READY 0
#define HAL_MAX_DELAY UINT32_MAX
#define SPI1_NCS_GPIO_Port ((void *)1)
#define SPI1_NCS_Pin 1
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
extern uint32_t spiMask;
static inline uint32_t __get_PRIMASK(void){return spiMask;}
static inline void __disable_irq(void){spiMask=1;}
static inline void __enable_irq(void){spiMask=0;}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *h);
unsigned HAL_SPI_GetState(SPI_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t timeout);
void HAL_GPIO_WritePin(void *port,unsigned pin,unsigned state);
#endif
