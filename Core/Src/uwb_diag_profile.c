/* SPDX-License-Identifier: MIT */
#include "uwb_diag_profile.h"
#include "mul_diag_config.h"
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE
#include "qorvo_platform.h"
#include "dw3000_deca_regs.h"
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXOVRR_BIT_MASK)
int32_t uwb_diag_configure(void) {
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
  return DWT_SUCCESS;
}
#endif
