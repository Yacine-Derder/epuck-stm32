#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ThirdParty/qorvo_dw3/platform/qorvo_irq.c"
#include "../../ThirdParty/qorvo_dw3/platform/qorvo_spi.c"
SPI_HandleTypeDef hspi1;
uint32_t spiMask;static unsigned serviced,pending,overruns,selected,busy;
static HAL_StatusTypeDef spiResult=HAL_OK;
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *h){(void)h;return HAL_OK;}
unsigned HAL_SPI_GetState(SPI_HandleTypeDef *h){(void)h;return busy;}
void HAL_GPIO_WritePin(void *port,unsigned pin,unsigned state){assert(port==SPI1_NCS_GPIO_Port&&pin==SPI1_NCS_Pin);selected=state==GPIO_PIN_RESET;}
static HAL_StatusTypeDef transfer(unsigned n,uint32_t timeout) {
 assert(selected);
#if MUL_UNIVERSAL
 assert(timeout==2);
#else
 assert(timeout==HAL_MAX_DELAY);
#endif
 /* At /64, fourteen SPI bytes take about one 115200 UART byte interval. */
 for(unsigned i=0;i<n/14;i++)if(spiMask){if(pending++)overruns++;}else serviced++;
 return spiResult;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t timeout){(void)h;(void)p;return transfer(n,timeout);}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t timeout){(void)h;memset(p,0,n);return transfer(n,timeout);}
int main(void) {
 uint8_t header[2]={0},body[84]={0};qorvo_spi_set_fastrate();assert(hspi1.Init.BaudRatePrescaler==64);
 assert(readfromspi(2,header,84,body)==DWT_SUCCESS&&!selected&&!spiMask);
#if MUL_UNIVERSAL
 assert(serviced==6&&!overruns);busy=1;assert(readfromspi(2,header,84,body)==DWT_ERROR&&!selected&&!spiMask);busy=0;
#else
 assert(!serviced&&overruns>=1);
#endif
 spiResult=HAL_TIMEOUT;assert(writetospi(2,header,84,body)==DWT_ERROR&&!selected&&!spiMask);
 assert(writetospiwithcrc(2,header,84,body,0)==DWT_ERROR&&!selected&&!spiMask);
 spiMask=1;decaIrqStatus_t saved=decamutexon();decamutexoff(saved);assert(spiMask==1);
 puts("Actual Qorvo SPI/IRQ: universal UART preemption, bounded timeout/busy failure, caller mask and legacy mutex behavior passed");return 0;
}
