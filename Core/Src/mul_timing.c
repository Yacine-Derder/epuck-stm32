/* SPDX-License-Identifier: MIT */
#include "mul_timing.h"
#if MUL_TIMING_TRACE
#include "main.h"
#include "usb_console.h"
#include <stdio.h>
#include <inttypes.h>
static uint32_t spiStart,spiMax,serviceMax,gapMax,lastStart,calls;
static uint8_t active,hasStart;
void mul_timing_init(void) {
  CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
  spiMax=serviceMax=gapMax=calls=0; hasStart=0; active=1;
}
uint32_t mul_timing_now(void) { return DWT->CYCCNT; }
void mul_timing_spi_begin(void) { if (active) spiStart=mul_timing_now(); }
void mul_timing_spi_end(void) {
  if (active) { uint32_t elapsed=mul_timing_now()-spiStart; if (elapsed>spiMax) spiMax=elapsed; }
}
void mul_timing_service(uint32_t start) {
  uint32_t elapsed=mul_timing_now()-start;
  if (elapsed>serviceMax) serviceMax=elapsed;
  if (hasStart && start-lastStart>gapMax) gapMax=start-lastStart;
  lastStart=start; hasStart=1; calls++;
}
void mul_timing_report(void) {
  char line[192];
  snprintf(line,sizeof(line),"MULG TIME hz=%" PRIu32 " spi_off_cycles=%" PRIu32 " service_cycles=%" PRIu32 " gap_cycles=%" PRIu32 " calls=%" PRIu32,
    SystemCoreClock,spiMax,serviceMax,gapMax,calls);
  usb_console_write(line);
}
#endif
