/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_ads.h"
#include "deca_device_api.h"
#include "dw3000_deca_regs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../Core/Src/mul_ads_radio.c"
static uint32_t tick,status,length;
static uint8_t input[56],output[56];
static uint64_t txMarker,rxMarker;
static uint16_t txAnt,rxAnt;
static bool badAnt;
static unsigned starts,rearms;
static int32_t startResult=DWT_SUCCESS,rxResult=DWT_SUCCESS,writeResult=DWT_SUCCESS;
#ifndef ADS_TEST_LOG_CAPACITY
#define ADS_TEST_LOG_CAPACITY 16384
#endif
static char captured[ADS_TEST_LOG_CAPACITY];
static bool mockRadioActive;
uint32_t HAL_GetTick(void) { return tick; }
int32_t uwb_diag_configure(void) { return DWT_SUCCESS; }
void dwt_settxantennadelay(uint16_t v) { txAnt=v; }
void dwt_setrxantennadelay(uint16_t v) { rxAnt=v; }
uint16_t dwt_gettxantennadelay(void) { return txAnt^(badAnt ? 1u : 0u); }
uint16_t dwt_getrxantennadelay(void) { return rxAnt; }
void dwt_forcetrxoff(void) {mockRadioActive=false;}
void dwt_writesysstatuslo(uint32_t bits) { status&=~bits; }
uint32_t dwt_readsysstatuslo(void) { return status; }
int32_t dwt_rxenable(int mode) { assert(mode==DWT_START_RX_IMMEDIATE); rearms++; mockRadioActive=rxResult==DWT_SUCCESS; return rxResult; }
int32_t dwt_starttx(uint8_t mode) {
 mulAdsPacket_t p; assert(mulAdsDecode(output,56,&p));
 assert(mode==(DWT_START_TX_IMMEDIATE|(p.type==mulAdsReport?0:DWT_RESPONSE_EXPECTED)));
 starts++; mockRadioActive=startResult==DWT_SUCCESS; return startResult;
}
int32_t dwt_writetxdata(uint16_t n,uint8_t *p,uint16_t at) { assert(n==56 && !at); memcpy(output,p,n); return writeResult; }
void dwt_writetxfctrl(uint16_t n,uint16_t at,uint8_t ranging) { assert(n==58 && !at && ranging==1); }
void dwt_setrxaftertxdelay(uint32_t delay) { assert(!delay); }
uint32_t dwt_read_reg(uint32_t reg) { assert(reg==RX_FINFO_ID); return length; }
void dwt_readrxdata(uint8_t *out,uint16_t n,uint16_t at) { assert(n==56 && !at); memcpy(out,input,n); }
void dwt_readrxtimestamp(uint8_t *out,dwt_ip_sts_segment_e segment) { assert(segment==DWT_COMPAT_NONE); mulEncodeTimestamp(out,rxMarker); }
void dwt_readtxtimestamp(uint8_t *out) { mulEncodeTimestamp(out,txMarker); }
void usb_console_write(const char *text) { assert(strlen(text)<(MUL_UNIVERSAL?320:192)); assert(strlen(captured)+strlen(text)+2<sizeof(captured)); strcat(captured,text); strcat(captured,"\n"); }
static void step(void) { tick++; mul_ads_radio_process(); }
static mulAdsPacket_t sentPacket(void) { mulAdsPacket_t p; assert(mulAdsDecode(output,56,&p)); return p; }
static void receive(mulAdsPacket_t *p,uint64_t t) {
 assert(mulAdsEncode(input,sizeof(input),p)); length=58; rxMarker=t; status|=DWT_INT_RXFCG_BIT_MASK; step();
}
int main(void) {
 assert(mul_ads_radio_init()==DWT_SUCCESS && txAnt==16380 && rxAnt==16380);
#if MUL_TWR_ROLE==1
 tick=1000; step(); mulAdsPacket_t p=sentPacket(); assert(p.type==mulAdsPoll && p.source==0x86 && p.destination==0x84);
 p.source=0x84; p.destination=0x86; p.type=mulAdsResponse;
 txMarker=MUL_TIMESTAMP_MASK-99999999; status=DWT_INT_TXFRS_BIT_MASK;
 receive(&p,50000001); /* TX completion and CRC-good together: TX first, no lost RX. */
 assert(state.stamps[0]==txMarker && state.stamps[1]==50000001 && sentPacket().type==mulAdsFinal);
 txMarker=250000001; status=DWT_INT_TXFRS_BIT_MASK; step();
 p.type=mulAdsReport; p.stamps[0]=10000; p.stamps[1]=150000000; p.stamps[2]=350010000;
 receive(&p,300000000); assert(state.completed==1 && state.stage==adsIdle && state.stamps[2]==txMarker);
 tick=2000; startResult=DWT_ERROR; step(); assert(state.stage==adsIdle && state.aborted==1 && errors==1);
 startResult=DWT_SUCCESS; tick=3000; step(); assert(state.pending);
 tick=3100; step(); assert(!state.pending && state.aborted==2); /* Lost TX completion. */
 assert(state.lastAbortReason==adsAbortDeadline && state.lastAbortStage==adsPollTx);
 tick=4000; step(); txMarker=0x344A792200ULL; status=DWT_INT_TXFRS_BIT_MASK; step();
 mulAdsPacket_t neg={.source=0x84,.destination=0x86,.session=state.session,.transaction=state.transaction,.type=mulAdsResponse};
 receive(&neg,0x34502B98D1ULL); txMarker=0x3452F90200ULL;status=DWT_INT_TXFRS_BIT_MASK;step();
 neg.type=mulAdsReport;neg.stamps[0]=0x761756DDC1ULL;neg.stamps[1]=0x761D0963FCULL;neg.stamps[2]=0x761FD6D14DULL;
 receive(&neg,0);assert(state.completed==2 && state.invalidRanges==1 && !state.rangeValid && state.tof<0);
 assert(strstr(captured,"range_valid=0"));
 tick=5000;writeResult=DWT_ERROR;step();assert(state.lastAbortReason==adsAbortTxStart && state.stage==adsIdle);writeResult=DWT_SUCCESS;
 tick=6000;step();txMarker=1000;status=DWT_INT_TXFRS_BIT_MASK;step();
 neg.type=mulAdsResponse;neg.transaction=state.transaction;memset(neg.stamps,0,sizeof(neg.stamps));
 receive(&neg,2000);txMarker=3000;status=DWT_INT_TXFRS_BIT_MASK;step();neg.type=mulAdsReport;
 receive(&neg,4000);assert(state.lastAbortReason==adsAbortMath && state.failedStages[adsReportRx-1]==1);
 assert(strstr(captured,"cause=6 stage=6"));
#if MUL_ADS_TRACE_TIMES
 assert(strstr(captured,"TIMES session="));
#else
 assert(!strstr(captured,"MULH TIMES"));
#endif
#else
 mulAdsPacket_t p={.source=0x84,.destination=0x86,.type=mulAdsPoll,.session=55,.transaction=1};
 receive(&p,MUL_TIMESTAMP_MASK-20); assert(sentPacket().type==mulAdsResponse);
 txMarker=10000000; status=DWT_INT_TXFRS_BIT_MASK; step();
 p.type=mulAdsFinal; receive(&p,20000000);
 mulAdsPacket_t report=sentPacket(); assert(report.type==mulAdsReport && report.stamps[0]==MUL_TIMESTAMP_MASK-20 && report.stamps[1]==txMarker && report.stamps[2]==20000000);
 status=DWT_INT_TXFRS_BIT_MASK; step(); assert(state.completed==1 && state.stage==adsIdle);
 p.type=mulAdsPoll; p.transaction=2; receive(&p,0);
 status=DWT_INT_RXFCE_BIT_MASK|DWT_INT_RXFCG_BIT_MASK; step(); assert(state.stage==adsIdle && errors==1 && state.aborted==1);
 p.transaction=3; receive(&p,1); assert(state.pending); status=DWT_INT_TXFRS_BIT_MASK; step();
 p.type=mulAdsFinal; p.transaction=1; receive(&p,2); assert(state.rejected && state.stage==adsFinalRx);
 status=DWT_INT_RXFTO_BIT_MASK; step(); assert(state.stage==adsIdle);
#endif
 length=127; status=DWT_INT_RXFCG_BIT_MASK; step(); assert(invalid==1);
 rxResult=DWT_ERROR; status=DWT_INT_RXFTO_BIT_MASK; step(); assert(retryRx);
 rxResult=DWT_SUCCESS; unsigned before=rearms; tick=(tick/50+1)*50-1; step(); assert(rearms==before+1 && !retryRx);
 unsigned n=starts; mul_ads_radio_process(); assert(n==starts);
 // Bounded formatting at maximal counter and session values, not just small runs.
 state.stage=adsIdle;state.transaction=UINT32_MAX;state.attempts=state.completed=state.aborted=state.rejected=state.invalidRanges=UINT32_MAX;
 state.session=UINT64_MAX;errors=invalid=timeouts=UINT32_MAX;status=0;tick+=2000;step();
 assert(strstr(captured,"bad_range=4294967295")); /* Detect truncation, not just buffer overflow. */
 for(unsigned j=0;j<7;j++) state.failedStages[j]=UINT16_MAX;
 logAbort();assert(strstr(captured,"by_stage=65535,65535,65535,65535,65535,65535,65535"));
#if MUL_ADS_TRACE_TIMES
 mulAdsPacket_t maximum={.stamps={MUL_TIMESTAMP_MASK,MUL_TIMESTAMP_MASK,MUL_TIMESTAMP_MASK}};
 for(unsigned j=0;j<3;j++)state.stamps[j]=MUL_TIMESTAMP_MASK;
 logTimes(&maximum);assert(strstr(captured,"t6=FFFFFFFFFF"));
#endif
 badAnt=true; assert(mul_ads_radio_init()==DWT_ERROR);
 puts("DWM3000 ADS measured timestamp/auto-RX/start failure/CRC priority/timeout/recovery adapter test passed");
 return 0;
}
