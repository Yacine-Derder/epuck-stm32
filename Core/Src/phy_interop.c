/* SPDX-License-Identifier: MIT */
#include "phy_interop.h"
#if PHY_INTEROP_ROLE
#include "qorvo_platform.h"
#include "deca_interface.h"
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
  if (dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS) state.errors++;
}
int32_t phy_interop_init(void) {
  memset(&state,0,sizeof(state));
  crcErrors=phrErrors=lastErrorStatus=reportedErrors=0;
  if (qorvo_probe_and_init()!=DWT_SUCCESS) return DWT_ERROR;
  dwt_config_t config={0};
  config.chan=DWT_CH5; config.txPreambLength=DWT_PLEN_128; config.rxPAC=DWT_PAC8;
  config.txCode=config.rxCode=9; config.sfdType=DWT_SFD_IEEE_4A;
  config.dataRate=DWT_BR_6M8; config.phrMode=DWT_PHRMODE_STD; config.phrRate=DWT_PHRRATE_STD;
  config.sfdTO=129; config.stsMode=DWT_STS_MODE_OFF; config.stsLength=DWT_STS_LEN_64; config.pdoaMode=DWT_PDOA_M0;
  if (dwt_configure(&config)!=DWT_SUCCESS) return DWT_ERROR;
  qorvo_spi_set_fastrate();
  dwt_forcetrxoff(); dwt_setinterrupt(0,0,DWT_ENABLE_INT_ONLY);
  dwt_configureframefilter(0,0); dwt_setrxtimeout(50000); dwt_setpreambledetecttimeout(0);
  dwt_writesysstatuslo(RX_EVENTS | DWT_INT_TXFRS_BIT_MASK);
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
    if (status&DWT_INT_RXFCG_BIT_MASK) {
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
    } else if (status&(SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
      if (status&SYS_STATUS_ALL_RX_TO) state.timeouts++;
      if (status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
        state.errors++; lastErrorStatus=status;
        if (status&DWT_INT_RXFCE_BIT_MASK) crcErrors++;
        if (status&(DWT_INT_RXPHE_BIT_MASK|DWT_INT_RXFSL_BIT_MASK)) phrErrors++;
      }
      rearm();
    }
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
