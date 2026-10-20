/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_ads_radio.h"
#if MUL_TWR_ROLE
#include "app_config.h"
#if !MUL_UWB_ONLY
#error "ADS requires explicit UWB-only safety policy"
#endif
#include "mul_ads.h"
#include "qorvo_platform.h"
#include "uwb_diag_profile.h"
#include "usb_console.h"
#include "mul_timing.h"
#include "dw3000_deca_regs.h"
#include <inttypes.h>
#include <stdio.h>
#ifndef MUL_ADS_DWM_ANT_DTU
#define MUL_ADS_DWM_ANT_DTU 16380u /* Existing platform nominal, not calibrated */
#endif
#if MUL_ADS_DWM_ANT_DTU > 65535
#error "DWM antenna delay must fit the hardware register"
#endif
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXOVRR_BIT_MASK)
static mulAds_t state;
static uint32_t lastPoll,lastStart,lastReport,errors,invalid,timeouts;
static bool retryRx;
static void logAbort(void) {
  char text[192];
  snprintf(text,sizeof(text),"MULH ABORT session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " cause=%u stage=%u by_stage=%u,%u,%u,%u,%u,%u,%u",
    (uint32_t)(state.session>>32),(uint32_t)state.session,state.transaction,(unsigned)state.lastAbortReason,(unsigned)state.lastAbortStage,(unsigned)state.failedStages[0],(unsigned)state.failedStages[1],(unsigned)state.failedStages[2],(unsigned)state.failedStages[3],(unsigned)state.failedStages[4],(unsigned)state.failedStages[5],(unsigned)state.failedStages[6]);
  usb_console_write(text);
}
static void logTimes(const mulAdsPacket_t *p) {
  char text[192];
  snprintf(text,sizeof(text),"MULH TIMES session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " t1=%02X%08" PRIX32 " t2=%02X%08" PRIX32 " t3=%02X%08" PRIX32 " t4=%02X%08" PRIX32 " t5=%02X%08" PRIX32 " t6=%02X%08" PRIX32,
    (uint32_t)(state.session>>32),(uint32_t)state.session,state.transaction,(unsigned)(state.stamps[0]>>32),(uint32_t)state.stamps[0],(unsigned)(p->stamps[0]>>32),(uint32_t)p->stamps[0],(unsigned)(p->stamps[1]>>32),(uint32_t)p->stamps[1],(unsigned)(state.stamps[1]>>32),(uint32_t)state.stamps[1],(unsigned)(state.stamps[2]>>32),(uint32_t)state.stamps[2],(unsigned)(p->stamps[2]>>32),(uint32_t)p->stamps[2]);
  usb_console_write(text);
}
static void rearm(void) {
  dwt_forcetrxoff(); dwt_writesysstatuslo(RX_EVENTS);
  retryRx=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;
  if(retryRx) errors++;
}
static void abortExchange(mulAdsAbortReason_t cause) {
  uint32_t before=state.aborted;
  mulAdsAbortWithReason(&state,cause); rearm();
  if(state.aborted!=before) logAbort();
}
int32_t mul_ads_radio_init(void) {
  if(uwb_diag_configure()!=DWT_SUCCESS) return DWT_ERROR;
  dwt_settxantennadelay(MUL_ADS_DWM_ANT_DTU); dwt_setrxantennadelay(MUL_ADS_DWM_ANT_DTU);
  if(dwt_gettxantennadelay()!=MUL_ADS_DWM_ANT_DTU || dwt_getrxantennadelay()!=MUL_ADS_DWM_ANT_DTU) return DWT_ERROR;
  mulAdsInit(&state,0x86,0x84,MUL_TWR_ROLE==1,MUL_TWR_SESSION ? MUL_TWR_SESSION : 1);
  errors=invalid=timeouts=0; lastPoll=lastStart=lastReport=HAL_GetTick();
  char line[192];
  snprintf(line,sizeof(line),"MULH INIT radio=DWM3000 role=%u session=%08" PRIX32 "%08" PRIX32 " ant=%u ch=5 version=1 bytes=56 uwb_only=1 uncalibrated=1",MUL_TWR_ROLE,(uint32_t)(state.session>>32),(uint32_t)state.session,MUL_ADS_DWM_ANT_DTU);
  usb_console_write(line); mul_timing_init(); rearm(); return DWT_SUCCESS;
}
void mul_ads_radio_process(void) {
  uint32_t now=HAL_GetTick();
  if(now==lastPoll) return;
  lastPoll=now; uint32_t started=mul_timing_now(); uint32_t completed=state.completed;
  if(mulAdsTick(&state,now)) { rearm(); logAbort(); }
  uint32_t status=dwt_readsysstatuslo();
  if(status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
    errors++; dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK); abortExchange(adsAbortRadio);
  } else {
    if(status&DWT_INT_TXFRS_BIT_MASK) {
      uint8_t t[5]; dwt_readtxtimestamp(t); dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
      mulAdsTxDone(&state,mulDecodeTimestamp(t),now);
      // Auto-RX remains active; capture concurrent good RX before any rearm.
      if(state.stage==adsIdle && !(status&DWT_INT_RXFCG_BIT_MASK)) rearm();
    }
    if(status&DWT_INT_RXFCG_BIT_MASK) {
      uint8_t frame[MUL_ADS_LENGTH],t[5]; mulAdsPacket_t p;
      uint32_t before=state.aborted; bool log=false; unsigned n=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
      if(n==MUL_ADS_LENGTH+2) {
        dwt_readrxdata(frame,sizeof(frame),0); dwt_readrxtimestamp(t,DWT_COMPAT_NONE);
        if(!mulAdsDecode(frame,sizeof(frame),&p)) invalid++;
        else {
          log=MUL_TWR_ROLE==1 && state.stage==adsReportRx && !state.pending && p.type==mulAdsReport &&
            p.session==state.session && p.transaction==state.transaction && p.source==state.peer && p.destination==state.local;
          mulAdsReceive(&state,&p,mulDecodeTimestamp(t),now);
        }
      } else invalid++;
      rearm();
      if(state.aborted!=before) logAbort();
      if(MUL_ADS_TRACE_TIMES && log) logTimes(&p);
    } else if(status&SYS_STATUS_ALL_RX_TO) { timeouts++; abortExchange(adsAbortRxTimeout); }
    else if(retryRx && now%50U==0) rearm();
  }
  if(MUL_TWR_ROLE==1 && state.stage==adsIdle && (uint32_t)(now-lastStart)>=1000U) {
    lastStart=now; mulAdsBegin(&state,now);
  }
  mulAdsPacket_t p;
  if(mulAdsTakeTx(&state,now,&p)) {
    uint8_t frame[MUL_ADS_LENGTH]; dwt_forcetrxoff();
    dwt_writesysstatuslo(RX_EVENTS|DWT_INT_TXFRS_BIT_MASK);
    if(!mulAdsEncode(frame,sizeof(frame),&p)) {
      errors++; abortExchange(adsAbortEncode);
    } else if(dwt_writetxdata(sizeof(frame),frame,0)!=DWT_SUCCESS) {
      errors++; abortExchange(adsAbortTxStart);
    } else {
      dwt_setrxaftertxdelay(0);
      dwt_writetxfctrl(sizeof(frame)+2,0,1);
      uint8_t mode=DWT_START_TX_IMMEDIATE | (p.type!=mulAdsReport ? DWT_RESPONSE_EXPECTED : 0);
      if(dwt_starttx(mode)!=DWT_SUCCESS) { errors++; abortExchange(adsAbortTxStart); }
      else retryRx=false;
    }
  }
  char line[192];
  if(MUL_TWR_ROLE==1 && state.completed!=completed) {
    snprintf(line,sizeof(line),"MULH RESULT session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " tof_dtu=%.6f ratio=%.9f nominal_m=%.4f uncalibrated=1 range_valid=%u",(uint32_t)(state.session>>32),(uint32_t)state.session,state.transaction,state.tof,state.rateRatio,state.tof*(299792458.0/63897600000.0),(unsigned)state.rangeValid);
    usb_console_write(line);
  }
  if((uint32_t)(now-lastReport)>=1000U) {
    lastReport=now;
    snprintf(line,sizeof(line),"MULH STAT attempt=%" PRIu32 " done=%" PRIu32 " abort=%" PRIu32 " reject=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32 " stage=%u session=%08" PRIX32 "%08" PRIX32 " bad_range=%" PRIu32,state.attempts,state.completed,state.aborted,state.rejected,invalid,errors,timeouts,state.stage,(uint32_t)(state.session>>32),(uint32_t)state.session,state.invalidRanges);
    usb_console_write(line);
    mul_timing_report();
  }
  mul_timing_service(started);
}
#endif
