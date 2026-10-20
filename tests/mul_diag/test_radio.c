#include "mul_diag.h"
#include "mul_core.h"
#include "deca_device_api.h"
#include "dw3000_deca_regs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../Core/Src/mul_diag.c"
static uint32_t ticks,status,length;
static uint8_t incoming[46];
static unsigned int rxStarts,txStarts,logs;
static int32_t startResult=DWT_SUCCESS,rxResult=DWT_SUCCESS;
static dwt_config_t profile;
uint32_t HAL_GetTick(void) { return ticks; }
int32_t qorvo_probe_and_init(void) { return DWT_SUCCESS; }
void qorvo_spi_set_fastrate(void) {}
int32_t dwt_configure(dwt_config_t *c) { profile=*c; return DWT_SUCCESS; }
void dwt_forcetrxoff(void) {}
void dwt_setinterrupt(uint32_t lo,uint32_t hi,dwt_INT_options_e enable) { assert(lo==0 && hi==0 && enable==DWT_ENABLE_INT_ONLY); }
void dwt_configureframefilter(uint16_t enable,uint16_t filter) { assert(enable==0 && filter==0); }
void dwt_setrxtimeout(uint32_t t) { assert(t==50000); }
void dwt_setpreambledetecttimeout(uint16_t t) { assert(t==0); }
void dwt_writesysstatuslo(uint32_t bits) { status&=~bits; }
uint32_t dwt_readsysstatuslo(void) { return status; }
int32_t dwt_rxenable(int mode) { assert(mode==DWT_START_RX_IMMEDIATE); rxStarts++; return rxResult; }
int32_t dwt_starttx(uint8_t mode) { assert(mode==DWT_START_TX_IMMEDIATE); txStarts++; return startResult; }
int32_t dwt_writetxdata(uint16_t len,uint8_t *frame,uint16_t at) {
  mulCoreDiag_t message; assert(at==0 && len==44 && mulCoreDecodeDiag(frame,len,&message)==mulCoreOk && message.source[0]==0x86); if (message.sequence==1) { assert(message.markerValid && message.markerSequence==0 && message.txMarker==UINT64_C(0xfefefefefe)); } return DWT_SUCCESS;
}
void dwt_writetxfctrl(uint16_t len,uint16_t at,uint8_t ranging) { assert(len==46 && at==0 && ranging==0); }
uint32_t dwt_read_reg(uint32_t reg) { assert(reg==RX_FINFO_ID); return length; }
void dwt_readrxdata(uint8_t *out,uint16_t len,uint16_t at) { assert(len==44 && at==0); memcpy(out,incoming,len); }
void dwt_readrxtimestamp(uint8_t *out,dwt_ip_sts_segment_e segment) { (void)segment; memset(out,0xfe,5); }
void usb_console_write(const char *text) { assert(strlen(text)<192); logs++; }
void dwt_readtxtimestamp(uint8_t *out) { memset(out,0xfe,5); }
static void step(void) { ticks++; mul_diag_process(); }
#if MUL_DIAG_ROLE==2
static void encodeIncoming(uint32_t sequence) {
  mulCoreDiag_t message={.source={0x84,0,0,0,0,0,0xcf,0xbc},.radio=MUL_CORE_DW1000,.sequence=sequence};
  assert(mulCoreEncodeDiag(incoming,sizeof(incoming),&message)==mulCoreOk);
}
#endif
int main(void) {
  assert(mul_diag_init()==DWT_SUCCESS);
  assert(profile.chan==5 && profile.txPreambLength==128 && profile.rxPAC==DWT_PAC8);
  assert(profile.txCode==9 && profile.rxCode==9 && profile.sfdType==DWT_SFD_IEEE_4A);
  assert(profile.dataRate==DWT_BR_6M8 && profile.phrMode==DWT_PHRMODE_STD && profile.phrRate==DWT_PHRRATE_STD);
  assert(profile.sfdTO==129 && profile.stsMode==DWT_STS_MODE_OFF && profile.pdoaMode==DWT_PDOA_M0);
#if MUL_DIAG_ROLE==1
  ticks=1000; step(); assert(txStarts==1 && state.pending);
  status=DWT_INT_TXFRS_BIT_MASK; step(); assert(state.done==1 && !state.pending);
  ticks=2000; step(); ticks=2100; step(); assert(state.errors==1 && !state.pending);
  ticks=3000; startResult=DWT_ERROR; step(); assert(!state.pending && state.errors==2);
  assert(rxStarts==0);
#else
  length=46; encodeIncoming(UINT32_MAX); status=DWT_INT_RXFCG_BIT_MASK; step(); assert(state.rx.valid==1);
  encodeIncoming(0); status=DWT_INT_RXFCG_BIT_MASK; step(); assert(state.rx.gaps==0 && state.rx.valid==2);
  incoming[43]^=1; status=DWT_INT_RXFCG_BIT_MASK; step(); assert(state.invalid==1);
  length=127; status=DWT_INT_RXFCG_BIT_MASK; step(); assert(state.invalid==2);
  status=DWT_INT_RXFCE_BIT_MASK; step(); assert(state.errors==1 && crcErrors==1);
  status=DWT_INT_RXPHE_BIT_MASK; step(); assert(state.errors==2 && phrErrors==1);
  status=DWT_INT_RXFTO_BIT_MASK; step(); assert(state.timeouts==1 && rxStarts==8);
  assert(txStarts==0);
  length=46; encodeIncoming(1);
  status=DWT_INT_RXFCG_BIT_MASK|DWT_INT_RXFCE_BIT_MASK; step();
  assert(state.rx.valid==2 && state.errors==3 && crcErrors==2);
  rxResult=DWT_ERROR; status=DWT_INT_RXFTO_BIT_MASK; step(); assert(state.pending);
  unsigned int before=rxStarts; step(); assert(rxStarts==before);
  rxResult=DWT_SUCCESS; status=DWT_INT_CP_LOCK_BIT_MASK; /* unrelated sticky status */ ticks+=50; step(); assert(!state.pending && rxStarts==before+1);
#endif
#if MUL_DIAG_ROLE==2
  encodeIncoming(2); incoming[33]=MUL_CORE_DW3000; status=DWT_INT_RXFCG_BIT_MASK; step();
  assert(state.invalid==3); /* valid core packet, wrong peer family */
#endif
#if MUL_DIAG_ROLE==2
  length=46; encodeIncoming(3000); status=DWT_INT_RXFCG_BIT_MASK; step();
  encodeIncoming(0); status=DWT_INT_RXFCG_BIT_MASK; step(); assert(state.rx.old==1 && state.rx.last==3000);
  ticks+=5000; encodeIncoming(1); status=DWT_INT_RXFCG_BIT_MASK; step();
  assert(state.rx.old==1 && state.rx.last==1 && state.rx.valid==5); /* Explicit idle epoch, totals retained. */
#endif
  unsigned int unchanged=logs; mul_diag_process(); assert(logs==unchanged); /* same-ms no SPI/log work */
  state.lastTxMs=UINT32_MAX-500; ticks=600; step(); assert(logs>0);
}
