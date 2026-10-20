/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_diag.h"
#if MUL_DIAG_ROLE
#include "mul_core.h"
#include "mul_timing.h"
#include "uwb_diag_profile.h"
#include "qorvo_platform.h"
#include "usb_console.h"
#include "app_config.h"
#include "dw3000_deca_regs.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static struct {
  mulCoreSequence_t rx;
  uint32_t sequence,attempts,done,invalid,errors,timeouts,lastTxMs,lastReportMs;
  uint8_t frame[MUL_CORE_DIAG_LENGTH+2];
  bool pending,markerValid;
  uint64_t lastTxMarker;
  uint32_t lastMarkerSequence;
} state;
static uint32_t lastPoll,crcErrors,phrErrors,lastErrorStatus,reportedErrors;
static const uint8_t identity[8]={0x86,0,0,0,0,0,0xcf,0xbc};
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXOVRR_BIT_MASK)
_Static_assert(sizeof(state)<=128,"bounded diagnostic state");
static void rearm(void) {
  dwt_forcetrxoff(); dwt_writesysstatuslo(RX_EVENTS); state.lastTxMs=HAL_GetTick();
  state.pending=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;
  if (state.pending) state.errors++;
}
int32_t mul_diag_init(void) {
  memset(&state,0,sizeof(state)); crcErrors=phrErrors=lastErrorStatus=reportedErrors=0;
  if (uwb_diag_configure()!=DWT_SUCCESS) return DWT_ERROR;
  state.lastTxMs=state.lastReportMs=lastPoll=HAL_GetTick();
  mul_timing_init();
  usb_console_write(MUL_DIAG_ROLE==1 ?
    "MULG INIT radio=DWM3000 role=TX src=86 core=1 type=70 version=1 bytes=44 bits=40 unit=DTU ch=5" :
    "MULG INIT radio=DWM3000 role=RX src=86 core=1 type=70 version=1 bytes=44 bits=40 unit=DTU ch=5");
  char policy[96];
  snprintf(policy,sizeof(policy),"MULG POLICY uwb_only=%u esp=%u stream_ms=%u",(unsigned)MUL_UWB_ONLY,(unsigned)APP_ESP_ENABLED,(unsigned)APP_EPUCK_STREAM_PERIOD_MS);
  usb_console_write(policy);
  if (MUL_DIAG_ROLE==2) rearm();
  return DWT_SUCCESS;
}
void mul_diag_process(void) {
  uint32_t now=HAL_GetTick();
  if (now==lastPoll) return; /* At most one status service per host millisecond. */
  lastPoll=now;
  uint32_t started=mul_timing_now();
  uint32_t status=dwt_readsysstatuslo(); char line[192];
  if (MUL_DIAG_ROLE==1) {
    if ((status&DWT_INT_TXFRS_BIT_MASK) && state.pending) {
      uint8_t stamp[5]; dwt_readtxtimestamp(stamp); state.lastTxMarker=mulDecodeTimestamp(stamp);
      state.lastMarkerSequence=state.sequence-1; state.markerValid=true;
      dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK); state.done++; state.pending=false;
      snprintf(line,sizeof(line),"MULG DONE src=86 seq=%" PRIu32 " done=%" PRIu32 " tx=%02X%08" PRIX32 " bits=40 unit=DTU",
        state.lastMarkerSequence,state.done,(unsigned)(state.lastTxMarker>>32),(uint32_t)state.lastTxMarker);
      usb_console_write(line);
    }
    if (state.pending && (uint32_t)(now-state.lastTxMs)>=100) { dwt_forcetrxoff(); state.errors++; state.pending=false; }
    if (!state.pending && (uint32_t)(now-state.lastTxMs)>=1000) {
      mulCoreDiag_t message={.sequence=state.sequence++,.markerSequence=state.lastMarkerSequence,
        .txMarker=state.lastTxMarker,.markerValid=state.markerValid,.radio=MUL_CORE_DW3000};
      memcpy(message.source,identity,8); state.attempts++; state.lastTxMs=now;
      if (mulCoreEncodeDiag(state.frame,sizeof(state.frame),&message)!=mulCoreOk ||
          dwt_writetxdata(MUL_CORE_DIAG_LENGTH,state.frame,0)!=DWT_SUCCESS) state.errors++;
      else {
        dwt_forcetrxoff(); dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
        dwt_writetxfctrl(MUL_CORE_DIAG_LENGTH+2,0,0);
        if (dwt_starttx(DWT_START_TX_IMMEDIATE)==DWT_SUCCESS) state.pending=true; else state.errors++;
      }
      snprintf(line,sizeof(line),"MULG TX src=86 seq=%" PRIu32 " attempt=%" PRIu32,message.sequence,state.attempts);
      usb_console_write(line);
    }
  } else {
    if (state.pending && !(status&RX_EVENTS) && (uint32_t)(now-state.lastTxMs)>=50) rearm();
    if (status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
      state.errors++; lastErrorStatus=status;
      if (status&DWT_INT_RXFCE_BIT_MASK) crcErrors++;
      if (status&(DWT_INT_RXPHE_BIT_MASK|DWT_INT_RXFSL_BIT_MASK)) phrErrors++;
      rearm();
    } else if (status&DWT_INT_RXFCG_BIT_MASK) {
      mulCoreDiag_t message; uint8_t stamp[5]; bool valid=false;
      unsigned length=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
      if (length==MUL_CORE_DIAG_LENGTH+2) {
        dwt_readrxdata(state.frame,MUL_CORE_DIAG_LENGTH,0); dwt_readrxtimestamp(stamp,DWT_COMPAT_NONE);
        valid=mulCoreDecodeDiag(state.frame,MUL_CORE_DIAG_LENGTH,&message)==mulCoreOk && message.source[0]==0x84 && message.radio==MUL_CORE_DW1000;
      }
      rearm();
      if (valid) {
        mulCoreObserveSequence(&state.rx,message.sequence);
        snprintf(line,sizeof(line),"MULG RX src=84 seq=%" PRIu32 " valid=1 rx=%02X%02X%02X%02X%02X gaps=%" PRIu32 " prev_valid=%u prev_seq=%" PRIu32 " prev_tx=%02X%08" PRIX32 " bits=40 unit=DTU",
          message.sequence,stamp[4],stamp[3],stamp[2],stamp[1],stamp[0],state.rx.gaps,
          message.markerValid,message.markerSequence,(unsigned)(message.txMarker>>32),(uint32_t)message.txMarker);
        usb_console_write(line);
      } else state.invalid++;
    } else if (status&SYS_STATUS_ALL_RX_TO) { state.timeouts++; rearm(); }
  }
  if ((uint32_t)(now-state.lastReportMs)>=1000) {
    state.lastReportMs=now;
    snprintf(line,sizeof(line),"MULG STAT tx=%" PRIu32 " done=%" PRIu32 " valid=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32,
      state.attempts,state.done,state.rx.valid,state.rx.gaps,state.rx.duplicates,state.rx.old,state.invalid,state.errors,state.timeouts);
    usb_console_write(line);
    if (state.errors!=reportedErrors) {
      reportedErrors=state.errors;
      snprintf(line,sizeof(line),"MULG ERR status=%08" PRIX32 " crc=%" PRIu32 " phr_rs=%" PRIu32,lastErrorStatus,crcErrors,phrErrors);
      usb_console_write(line);
    }
    mul_timing_report();
  }
  mul_timing_service(started);
}
#endif
