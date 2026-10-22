/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_hybrid_radio.h"
#if MUL_UNIVERSAL
#include "mul_hybrid.h"
#include "qorvo_platform.h"
#include "dw3000_deca_regs.h"
#include "mul_timing.h"
#include "usb_console.h"
#include <stdio.h>
#include <string.h>
#define HYBRID_RX_EVENTS (SYS_STATUS_ALL_RX_GOOD|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)
static mulHybrid_t hybridReceiver;
static uwb_config_t hybridConfig;
static uint32_t hybridErrors,hybridTimeouts,hybridLastReport;
static bool hybridRetry;
static uint8_t hybridLastStatus;
static void hybridRearm(void) {
 dwt_forcetrxoff();dwt_writesysstatuslo(HYBRID_RX_EVENTS|DWT_INT_TXFRS_BIT_MASK);
 hybridRetry=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;if(hybridRetry)hybridErrors++;
}
void mul_hybrid_radio_configure(const uwb_config_t *c) {
 hybridConfig=*c;uint8_t receiver[8]={(uint8_t)c->node,0,0,0,0,0,0xcf,0xbc};
 mulHybridInit(&hybridReceiver,receiver,c->mode==5);hybridErrors=hybridTimeouts=0;
 hybridLastReport=HAL_GetTick();hybridRetry=false;hybridLastStatus=mulDownlinkNoRelation;
}
void mul_hybrid_radio_status(char *line,size_t capacity) {
 snprintf(line,capacity,"HYBRID passive=1 slot_ms=%u rate=%s rx=%lu reject=%lu pairs=%lu status=%u twr_inter_alr=%lu twr_reject=%lu twr_timeout=%lu phy=%lu timeout=%lu",
 MUL_FOLLOWER_SLOT_MS,hybridConfig.mode==5?"STATIONARY_OPT_IN":"UNKNOWN",(unsigned long)hybridReceiver.received,(unsigned long)hybridReceiver.rejected,
 (unsigned long)hybridReceiver.pairs,hybridLastStatus,(unsigned long)hybridReceiver.twrCompleted,(unsigned long)hybridReceiver.twrRejected,(unsigned long)hybridReceiver.twrTimeouts,(unsigned long)hybridErrors,(unsigned long)hybridTimeouts);
}
bool mul_hybrid_radio_reset_stats(void) {
 if(hybridReceiver.twrActive)return false;
 hybridReceiver.received=hybridReceiver.rejected=hybridReceiver.pairs=hybridReceiver.twrCompleted=hybridReceiver.twrRejected=hybridReceiver.twrTimeouts=0;
 hybridErrors=hybridTimeouts=0;mul_timing_reset_stats();return true;
}
void mul_hybrid_radio_process(void) {
 uint32_t started=mul_timing_now(),now=HAL_GetTick();mulHybridExpire(&hybridReceiver,now);
 uint32_t status=dwt_readsysstatuslo();
 if(status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {hybridErrors++;hybridRearm();}
 else if(status&DWT_INT_RXFCG_BIT_MASK) {
   uint8_t frame[84],raw[5],hardware[5];unsigned bytes=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
   bool shape=bytes==36||bytes==76||bytes==86;uint64_t rx=0;uint32_t age=UINT16_MAX;
   if(shape) {
     bytes-=2;dwt_readrxdata(frame,(uint16_t)bytes,0);dwt_readrxtimestamp(raw,DWT_COMPAT_NONE);dwt_readsystime(hardware);
     rx=mulDecodeTimestamp(raw);int64_t elapsed=mulTimestampDifference(mulDecodeTimestamp(hardware),rx);
     if(elapsed>=0&&elapsed<=15*INT64_C(63897600))age=(uint32_t)((elapsed+63897599)/63897600);
   }
   hybridRearm(); /* Freeze all diagnostics before RX; shared math/IO follow rearm. */
   bool enabled=false;if(shape)for(unsigned i=0;i<hybridConfig.peer_count;i++)if(hybridConfig.peers[i].enabled&&hybridConfig.peers[i].address==frame[13])enabled=true;
   mulHybridOutput_t out;
   if(shape&&enabled&&mulHybridReceive(&hybridReceiver,frame,bytes,rx,age,now,&out)) {
     if(out.associated)hybridLastStatus=out.observation.status;
     mulTelemetry_t record={.type=mulTlmHybridRx,.peer=frame[13],.remoteRadio=1,.transaction=out.packet.sequence,
       .exchange=out.broadcast?out.relation.epoch:0,.quality=out.associated?out.observation.status:255,.flags=mulTlmStampValid,
       .legacyLength=(uint8_t)bytes,.legacyRx=rx,.captureAgeMs=(uint16_t)age,.legacySlotMs=MUL_FOLLOWER_SLOT_MS,
       .referenceGeneration=out.referenceGeneration,.followerGeneration=out.followerGeneration,.ratePolicy=hybridConfig.mode==5,
       .radioErrors=hybridErrors};
     memcpy(record.legacyFrame,frame,bytes);uwb_runtime_publish(&record);
     if(out.associated&&hybridConfig.verbosity) {
       char line[192];snprintf(line,sizeof(line),"MULH OBS ref=%lu fol=%lu status=%u clock=%u rate=%u tdoa_dtu=%ld range_diff_mm=%ld power=UNSUPPORTED uncertainty=UNKNOWN",
       (unsigned long)out.observation.referenceSequence,(unsigned long)out.observation.followerSequence,out.observation.status,out.observation.clockValid,out.observation.rateState,(long)out.observation.tdoaDtu,(long)out.observation.rangeDifferenceMm);usb_console_write(line);
     }
   } else if(!shape||!enabled)hybridReceiver.rejected++;
 } else if(status&SYS_STATUS_ALL_RX_TO){hybridTimeouts++;hybridRearm();}
 else if(hybridRetry)hybridRearm();
 if((uint32_t)(now-hybridLastReport)>=hybridConfig.telemetry_ms) {
   hybridLastReport=now;uint32_t t[4];mul_timing_snapshot(t);
   mulTelemetry_t r={.type=mulTlmSummary,.peer=0xff,.attempts=hybridReceiver.received,.completed=hybridReceiver.pairs,.rejected=hybridReceiver.rejected,
   .radioErrors=hybridErrors,.timeouts=hybridTimeouts,.spiCycles=t[0],.serviceCycles=t[1],.gapCycles=t[2],.clockHz=t[3]};uwb_runtime_publish(&r);
   if(hybridConfig.verbosity){char line[320];mul_hybrid_radio_status(line,sizeof(line));usb_console_write(line);}
 }
 mul_timing_service(started);
}
#endif
