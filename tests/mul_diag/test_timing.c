#include <assert.h>
#include <string.h>
#include "../../Core/Src/mul_timing.c"
#include "../../ThirdParty/qorvo_dw3/platform/qorvo_irq.c"
MockDwt fakeDwt;
MockDebug fakeDebug;
uint32_t SystemCoreClock=168000000,irqMask;
static char lastLine[192];
void usb_console_write(const char *s) { assert(strlen(s)<sizeof(lastLine)); strcpy(lastLine,s); }
int main(void) {
  mul_timing_init(); assert(DWT->CTRL && CoreDebug->DEMCR);
  DWT->CYCCNT=UINT32_MAX-10;
  decaIrqStatus_t outer=decamutexon(); assert(irqMask==1);
  DWT->CYCCNT=UINT32_MAX-5; decaIrqStatus_t inner=decamutexon();
  DWT->CYCCNT=1; decamutexoff(inner); assert(irqMask==1);
  DWT->CYCCNT=10; decamutexoff(outer); assert(!irqMask && spiMax==21);
  DWT->CYCCNT=30; mul_timing_service(20); assert(serviceMax==10 && calls==1);
  DWT->CYCCNT=80; mul_timing_service(50); assert(serviceMax==30 && gapMax==30);
  mul_timing_report(); assert(strstr(lastLine,"spi_off_cycles=21") && strstr(lastLine,"hz=168000000"));
  mul_timing_init(); assert(!spiMax && !calls);
}
