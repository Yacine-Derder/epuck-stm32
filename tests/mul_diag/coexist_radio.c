/* Radio-only mocks linked with the real UART/USB/safety regression source. */
#include "deca_device_api.h"
#include "dw3000_deca_regs.h"
#include "mul_core.h"
#include <assert.h>
#include <string.h>
#include "../../Core/Src/mul_diag.c"
static uint32_t syntheticStatus,nextRx,syntheticSequence;
static uint8_t syntheticFrame[44];
int32_t qorvo_probe_and_init(void) { return DWT_SUCCESS; }
void qorvo_spi_set_fastrate(void) {}
int32_t dwt_configure(dwt_config_t *c) { assert(c->chan==5); return DWT_SUCCESS; }
void dwt_forcetrxoff(void) {}
void dwt_setinterrupt(uint32_t a,uint32_t b,dwt_INT_options_e c) { (void)a;(void)b;(void)c; }
void dwt_configureframefilter(uint16_t a,uint16_t b) { (void)a;(void)b; }
void dwt_setrxtimeout(uint32_t a) { assert(a==50000); }
void dwt_setpreambledetecttimeout(uint16_t a) { assert(a==0); }
void dwt_writesysstatuslo(uint32_t mask) { syntheticStatus&=~mask; }
uint32_t dwt_readsysstatuslo(void) {
  uint32_t now=HAL_GetTick();
  if (!syntheticStatus && (int32_t)(now-nextRx)>=0) {
    nextRx=now+1000;
    mulCoreDiag_t packet={.source={0x84,0,0,0,0,0,0xcf,0xbc},.sequence=syntheticSequence++,.radio=MUL_CORE_DW1000};
    assert(mulCoreEncodeDiag(syntheticFrame,sizeof(syntheticFrame),&packet)==mulCoreOk);
    syntheticStatus=syntheticSequence%5 ? DWT_INT_RXFCG_BIT_MASK : DWT_INT_RXFCE_BIT_MASK;
  }
  return syntheticStatus;
}
int32_t dwt_rxenable(int mode) { assert(mode==DWT_START_RX_IMMEDIATE); return DWT_SUCCESS; }
uint32_t dwt_read_reg(uint32_t reg) { assert(reg==RX_FINFO_ID); return 46; }
void dwt_readrxdata(uint8_t *out,uint16_t n,uint16_t at) { assert(n==44 && !at); memcpy(out,syntheticFrame,n); }
void dwt_readrxtimestamp(uint8_t *out,dwt_ip_sts_segment_e segment) { assert(segment==DWT_COMPAT_NONE); memset(out,0xfe,5); }
void coexist_check(void) { assert(state.rx.valid>0 && state.errors>0 && !state.pending); }
