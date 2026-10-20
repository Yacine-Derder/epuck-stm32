/* SPDX-License-Identifier: MIT */
#include "phy_interop.h"
#include "uwb_diag_profile.h"
#if PHY_INTEROP_ROLE
#include "qorvo_platform.h"
#include "usb_console.h"
#include "dw3000_deca_regs.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static phyDiagState_t state;
static uint32_t crcErrors,phrErrors,lastErrorStatus,reportedErrors;
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXOVRR_BIT_MASK)
static void rearm(void) {
  dwt_forcetrxoff();
  dwt_writesysstatuslo(RX_EVENTS);
  state.lastTxMs=HAL_GetTick(); // RX role reuses this as its bounded retry timer.
  state.pending=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;
  if (state.pending) state.errors++;
}
int32_t phy_interop_init(void) {
  memset(&state,0,sizeof(state));
  crcErrors=phrErrors=lastErrorStatus=reportedErrors=0;
  if (uwb_diag_configure()!=DWT_SUCCESS) return DWT_ERROR;
  state.lastTxMs=state.lastReportMs=HAL_GetTick();
  usb_console_write(PHY_INTEROP_ROLE==1 ?
    "PHY INIT radio=DWM3000 role=TX src=86 ch=5 prf=64 pre=128 code=9 sfd=4A rate=6800 phr=STD sts=OFF bytes=34" :
    "PHY INIT radio=DWM3000 role=RX src=86 ch=5 prf=64 pre=128 code=9 sfd=4A rate=6800 phr=STD sts=OFF bytes=34");
  if (PHY_INTEROP_ROLE==2) rearm();
  return DWT_SUCCESS;
}
void phy_interop_process(void) {
  uint32_t now=HAL_GetTick();
  uint32_t status=dwt_readsysstatuslo();
  char line[192]; // Existing USB queue copies this bounded task-local line.
  if (PHY_INTEROP_ROLE==1) {
    if ((status&DWT_INT_TXFRS_BIT_MASK) && state.pending) {
      dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK); state.done++; state.pending=false;
      snprintf(line,sizeof(line),"PHY DONE src=86 seq=%" PRIu32 " done=%" PRIu32,state.sequence-1,state.done);
      usb_console_write(line);
    }
    if (state.pending && (uint32_t)(now-state.lastTxMs)>=100) {
      dwt_forcetrxoff(); state.errors++; state.pending=false;
    }
    if (!state.pending && (uint32_t)(now-state.lastTxMs)>=1000) {
      uint32_t sequence=state.sequence++; state.attempts++; state.lastTxMs=now;
      phyDiagEncode(state.frame,PHY_DIAG_DWM3000,sequence);
      dwt_forcetrxoff(); dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
      if (dwt_writetxdata(PHY_DIAG_LENGTH,state.frame,0)!=DWT_SUCCESS) state.errors++;
      else {
        dwt_writetxfctrl(PHY_DIAG_LENGTH+2,0,0);
        if (dwt_starttx(DWT_START_TX_IMMEDIATE)==DWT_SUCCESS) state.pending=true; else state.errors++;
      }
      snprintf(line,sizeof(line),"PHY TX src=86 seq=%" PRIu32 " attempt=%" PRIu32,sequence,state.attempts);
      usb_console_write(line);
    }
  } else {
    if (state.pending && !(status&RX_EVENTS) && (uint32_t)(now-state.lastTxMs)>=50) rearm();
    if (status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
      state.errors++; lastErrorStatus=status;
      if (status&DWT_INT_RXFCE_BIT_MASK) crcErrors++;
      if (status&(DWT_INT_RXPHE_BIT_MASK|DWT_INT_RXFSL_BIT_MASK)) phrErrors++;
      rearm(); // Error wins over a simultaneous CRC-good/overrun indication.
    } else if (status&DWT_INT_RXFCG_BIT_MASK) {
      uint32_t sequence; uint8_t source,stamp[5]; bool valid=false;
      unsigned int length=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
      if (length==PHY_DIAG_LENGTH+2) {
        dwt_readrxdata(state.frame,PHY_DIAG_LENGTH,0); dwt_readrxtimestamp(stamp,DWT_COMPAT_NONE);
        valid=phyDiagDecode(state.frame,PHY_DIAG_LENGTH,&source,&sequence) && source==PHY_DIAG_DW1000;
      }
      rearm();
      if (valid) {
        phyDiagObserve(&state.rx,sequence);
        snprintf(line,sizeof(line),"PHY RX src=%02X seq=%" PRIu32 " valid=1 rx=%02X%02X%02X%02X%02X gaps=%" PRIu32,
          source,sequence,stamp[4],stamp[3],stamp[2],stamp[1],stamp[0],state.rx.gaps);
        usb_console_write(line);
      } else state.invalid++;
    } else if (status&SYS_STATUS_ALL_RX_TO) { state.timeouts++; rearm(); }
  }
  if ((uint32_t)(now-state.lastReportMs)>=1000) {
    state.lastReportMs=now;
    snprintf(line,sizeof(line),"PHY STAT tx=%" PRIu32 " done=%" PRIu32 " valid=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32,
      state.attempts,state.done,state.rx.valid,state.rx.gaps,state.rx.duplicates,state.rx.old,state.invalid,state.errors,state.timeouts);
    usb_console_write(line);
    if (state.errors!=reportedErrors) {
      reportedErrors=state.errors;
      snprintf(line,sizeof(line),"PHY ERR status=%08" PRIX32 " crc=%" PRIu32 " phr_rs=%" PRIu32,lastErrorStatus,crcErrors,phrErrors);
      usb_console_write(line);
    }
  }
}
#endif
