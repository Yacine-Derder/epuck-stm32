/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_hybrid_radio.h"
#if MUL_UNIVERSAL
#include "mul_hybrid.h"
#include "mul_multi_receiver.h"
#include "qorvo_platform.h"
#include "dw3000_deca_regs.h"
#include "mul_timing.h"
#include "usb_console.h"
#include <stdio.h>
#include <string.h>
#define HYBRID_RX_EVENTS (SYS_STATUS_ALL_RX_GOOD|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)
static mulHybrid_t hybridReceiver;
static mulMultiReceiver_t multiReceiver;
static uwb_config_t hybridConfig;
static uint32_t hybridErrors,hybridTimeouts,hybridLastReport;
static bool hybridRetry;
static uint8_t hybridLastStatus;
/* Fixed-size, on-demand evidence only: no automatic packet trace or heap. */
static struct {uint8_t frame[84],length;uint64_t rx;uint32_t age,observedMs;
#if MUL_HYBRID_FREQ_DIAG
 int32_t ci;int16_t coe;uint32_t status;bool coePresent;
#endif
} hybridFrames[3];
static void hybridRearm(void) {
 dwt_forcetrxoff();dwt_writesysstatuslo(HYBRID_RX_EVENTS|DWT_INT_TXFRS_BIT_MASK);
 hybridRetry=dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS;if(hybridRetry)hybridErrors++;
}
void mul_hybrid_radio_configure(const uwb_config_t *c) {
 memset(hybridFrames,0,sizeof(hybridFrames));
#if MUL_HYBRID_FREQ_DIAG
 /* Called with RX stopped by the universal radio owner. Diagnostic logging
  * affects only this explicit experimental hybrid build, not the normal PHY. */
 dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
#endif
 hybridConfig=*c;uint8_t receiver[8]={(uint8_t)c->node,0,0,0,0,0,0xcf,0xbc};
 mulNetwork_t network={0};(void)mulNetworkInit(&network,(uint8_t)c->reference,(uint8_t)c->follower);
 network.slotMs=(uint16_t)c->slot_ms;
 (void)mulHybridInitNetwork(&hybridReceiver,&network,receiver,c->mode==5);hybridErrors=hybridTimeouts=0;
 hybridLastReport=HAL_GetTick();hybridRetry=false;hybridLastStatus=mulDownlinkNoRelation;
 memset(&multiReceiver,0,sizeof(multiReceiver));
 if(c->schedule.profile==MUL_PROFILE_MULTI)(void)mulMultiReceiverInit(&multiReceiver,&c->schedule,receiver,c->mode==5);
}
void mul_hybrid_radio_suspend(void) {
 multiReceiver.active=false;mulClockReset(&multiReceiver.rate);
 hybridReceiver.twrActive=false;hybridReceiver.association.pending=false;
 for(unsigned i=0;i<MUL_ALR_COUNT;i++)hybridReceiver.observations[i].valid=false;
 mulClockReset(&hybridReceiver.rate);hybridLastStatus=mulDownlinkNoRelation;
}
void mul_hybrid_radio_status(char *line,size_t capacity) {
 if(hybridConfig.schedule.profile==MUL_PROFILE_MULTI) {
  snprintf(line,capacity,"MULTI passive=1 count=%u slot_ms=%u cycle_ms=%u revision=%lu hash=%08lX cycle=%lu seen=0x%02X rx=%lu reject=%lu vectors=%lu mismatch=%lu duplicate=%lu late=%lu phy=%lu timeout=%lu",
   multiReceiver.schedule.count,multiReceiver.schedule.slotMs,multiReceiver.schedule.periodMs,
   (unsigned long)multiReceiver.schedule.revision,(unsigned long)mulScheduleFingerprint(&multiReceiver.schedule),
   (unsigned long)multiReceiver.cycle,multiReceiver.seenMask,(unsigned long)multiReceiver.received,(unsigned long)multiReceiver.rejected,
   (unsigned long)multiReceiver.cycles,(unsigned long)multiReceiver.mismatches,(unsigned long)multiReceiver.duplicates,
   (unsigned long)multiReceiver.late,(unsigned long)hybridErrors,(unsigned long)hybridTimeouts);return;
 }
 snprintf(line,capacity,"HYBRID passive=1 slot_ms=%u rate=%s rx=%lu reject=%lu pairs=%lu last_status=%u ref_fresh=%u fol_fresh=%u twr_inter_alr=%lu twr_reject=%lu twr_timeout=%lu phy=%lu timeout=%lu slot_mismatch=%lu",
 (unsigned)hybridConfig.slot_ms,hybridConfig.mode==5?"STATIONARY_OPT_IN":"UNKNOWN",(unsigned long)hybridReceiver.received,(unsigned long)hybridReceiver.rejected,
 (unsigned long)hybridReceiver.pairs,hybridLastStatus,mulObservationFresh(&hybridReceiver.observations[mulAlrReference],HAL_GetTick()),mulObservationFresh(&hybridReceiver.observations[mulAlrFollower],HAL_GetTick()),(unsigned long)hybridReceiver.twrCompleted,(unsigned long)hybridReceiver.twrRejected,(unsigned long)hybridReceiver.twrTimeouts,(unsigned long)hybridErrors,(unsigned long)hybridTimeouts,(unsigned long)hybridReceiver.diagnostics.reason[mulHybridScheduleMismatch]);
}
void mul_hybrid_radio_diagnostics(unsigned page,char *line,size_t capacity) {
 if(hybridConfig.schedule.profile==MUL_PROFILE_MULTI){mul_hybrid_radio_status(line,capacity);return;}
 const mulHybridDiagnostics_t *d=&hybridReceiver.diagnostics;
 if(page==0)snprintf(line,capacity,"MULH DIAG poll=%lu response=%lu final=%lu report=%lu result=%lu relations=%lu available=%lu last=%s type=0x%02X id=%lu stage=0x%02X",
 (unsigned long)d->twrFrames[0],(unsigned long)d->twrFrames[1],(unsigned long)d->twrFrames[2],(unsigned long)d->twrFrames[3],(unsigned long)d->twrFrames[4],
 (unsigned long)d->relations,(unsigned long)d->availableRelations,d->hasFailure?mulHybridReasonName(d->lastReason):"none",d->lastType,(unsigned long)d->lastId,d->lastStage);
 else if(page==1)snprintf(line,capacity,"MULH REJECT shape=%lu address=%lu decode=%lu no_reference=%lu sequence=%lu window=%lu missing_twr=%lu prefix=%lu timestamp=%lu arithmetic=%lu capture_age=%lu",
 (unsigned long)d->reason[0],(unsigned long)d->reason[1],(unsigned long)d->reason[2],(unsigned long)d->reason[3],(unsigned long)d->reason[4],(unsigned long)d->reason[5],(unsigned long)d->reason[6],(unsigned long)d->reason[7],(unsigned long)d->reason[8],(unsigned long)d->reason[9],(unsigned long)d->reason[10]);
 else snprintf(line,capacity,"MULH MODEL source_order=%lu epoch=%lu missing_relation=%lu stale_relation=%lu missing_propagation=%lu available=%u version=%u delay_state=%u ref_seq=%lu twr_seq=%lu origin_ms=%u delay_ms=%u remaining_ms=%u",
 (unsigned long)d->reason[11],(unsigned long)d->reason[12],(unsigned long)d->reason[13],(unsigned long)d->reason[14],(unsigned long)d->reason[15],
 hybridReceiver.relation.available,hybridReceiver.relation.version,hybridReceiver.relation.delayState,(unsigned long)hybridReceiver.relation.referenceSequence,(unsigned long)hybridReceiver.relation.twrSequence,hybridReceiver.relation.originAgeMs,hybridReceiver.relation.delayAgeMs,hybridReceiver.relation.remainingMs);
}
void mul_hybrid_radio_frame(unsigned index,char *line,size_t capacity) {
 if(index>=3||!hybridFrames[index].length){snprintf(line,capacity,"MULH FRAME slot=%u absent",index);return;}
 char hex[169];unsigned n=hybridFrames[index].length;
 for(unsigned i=0;i<n;i++)snprintf(hex+2*i,3,"%02X",hybridFrames[index].frame[i]);
 snprintf(line,capacity,"MULH FRAME slot=%u rx=%02lX%08lX age_ms=%lu observed_ms=%lu len=%u frame=%s",index,
 (unsigned long)(hybridFrames[index].rx>>32),(unsigned long)(uint32_t)hybridFrames[index].rx,(unsigned long)hybridFrames[index].age,(unsigned long)hybridFrames[index].observedMs,n,hex);
}
bool mul_hybrid_radio_frequency(unsigned index,char *line,size_t capacity) {
#if MUL_HYBRID_FREQ_DIAG
 if(index>=3)return false;
 if(!hybridFrames[index].length){snprintf(line,capacity,"MULH FREQ slot=%u absent",index);return true;}
 const uint8_t *f=hybridFrames[index].frame;
 uint32_t seq=(uint32_t)f[25]|(uint32_t)f[26]<<8|(uint32_t)f[27]<<16|(uint32_t)f[28]<<24;
 snprintf(line,capacity,"MULH FREQ slot=%u peer=0x%02X type=0x%02X seq=%lu rx=%02lX%08lX observed_ms=%lu age_ms=%lu ci_raw=%ld coe_raw=%d coe_present=%u status=0x%08lX metric_rate=UNKNOWN",
 index,f[13],f[24],(unsigned long)seq,(unsigned long)(hybridFrames[index].rx>>32),
 (unsigned long)(uint32_t)hybridFrames[index].rx,(unsigned long)hybridFrames[index].observedMs,
 (unsigned long)hybridFrames[index].age,(long)hybridFrames[index].ci,hybridFrames[index].coe,
 hybridFrames[index].coePresent,(unsigned long)hybridFrames[index].status);
 return true;
#else
 if(index)return false;
 snprintf(line,capacity,"MULH FREQ disabled; build with MUL_HYBRID_FREQ_DIAG=ON; metric_rate=UNKNOWN");return true;
#endif
}
bool mul_hybrid_radio_reset_stats(void) {
 if(hybridReceiver.twrActive)return false;
 multiReceiver.received=multiReceiver.rejected=multiReceiver.duplicates=multiReceiver.old=multiReceiver.late=multiReceiver.mismatches=multiReceiver.cycles=0;
 hybridReceiver.received=hybridReceiver.rejected=hybridReceiver.pairs=hybridReceiver.twrCompleted=hybridReceiver.twrRejected=hybridReceiver.twrTimeouts=0;
 memset(&hybridReceiver.diagnostics,0,sizeof(hybridReceiver.diagnostics));
 hybridErrors=hybridTimeouts=0;mul_timing_reset_stats();return true;
}
static void publishVector(const mulMultiCycle_t *cycle) {
 mulTelemetry_t record={.type=mulTlmVector,.uptimeMs=cycle->observedMs,.ratePolicy=hybridConfig.mode==5,.vector=*cycle};
 uwb_runtime_publish(&record);
}
static void multiProcess(void) {
 uint32_t started=mul_timing_now(),now=HAL_GetTick();mulMultiCycle_t completed;
 if(mulMultiReceiverPoll(&multiReceiver,now,&completed))publishVector(&completed);
 uint32_t status=dwt_readsysstatuslo();
 if(status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)){hybridErrors++;hybridRearm();}
 else if(status&DWT_INT_RXFCG_BIT_MASK) {
  uint8_t frame[MUL_MULTI_FOLLOWER_BYTES],raw[5],hardware[4];
  unsigned bytes=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
  bool shape=bytes==MUL_MULTI_REFERENCE_BYTES+2||bytes==MUL_MULTI_FOLLOWER_BYTES+2;
  uint64_t rx=0;uint32_t age=UINT16_MAX;
  if(shape) {
   bytes-=2;dwt_readrxdata(frame,(uint16_t)bytes,0);dwt_readrxtimestamp(raw,DWT_COMPAT_NONE);dwt_readsystime(hardware);
   uint64_t upper=255;for(unsigned i=0;i<4;i++)upper|=(uint64_t)hardware[i]<<(8*(i+1));
   rx=mulDecodeTimestamp(raw);int64_t elapsed=mulTimestampDifference(upper,rx);
   if(elapsed>=0&&elapsed<=15*INT64_C(63897600))age=(uint32_t)((elapsed+63897599)/63897600);
  }
  hybridRearm(); // The single radio owner re-arms before codecs, math and telemetry.
  mulMultiReceiveOutput_t out;
  if(shape&&mulMultiReceiverReceive(&multiReceiver,frame,bytes,rx,age,now,&out)) {
   if(out.cycleReady)publishVector(&out.completed);
   unsigned index=out.broadcast.slot?1:0;
   memcpy(hybridFrames[index].frame,frame,bytes);hybridFrames[index].length=(uint8_t)bytes;
   hybridFrames[index].rx=rx;hybridFrames[index].age=age;hybridFrames[index].observedMs=now;
   mulTelemetry_t record={.type=mulTlmMultiRx,.peer=frame[13],.remoteRadio=1,.flags=mulTlmStampValid,
    .transaction=out.broadcast.packet.sequence,.legacyLength=(uint8_t)bytes,.legacyRx=rx,
    .captureAgeMs=(uint16_t)age,.legacySlotMs=multiReceiver.schedule.slotMs,
    .scheduleFingerprint=mulScheduleFingerprint(&multiReceiver.schedule),.scheduleRevision=multiReceiver.schedule.revision,
    .ratePolicy=hybridConfig.mode==5};
   memcpy(record.legacyFrame,frame,bytes);uwb_runtime_publish(&record);
  }else if(!shape)multiReceiver.rejected++;
 }else if(status&SYS_STATUS_ALL_RX_TO){hybridTimeouts++;hybridRearm();}
 else if(hybridRetry)hybridRearm();
 if((uint32_t)(now-hybridLastReport)>=hybridConfig.telemetry_ms) {
  hybridLastReport=now;mulTelemetry_t record={.type=mulTlmSummary,.peer=255,
   .attempts=multiReceiver.received,.completed=multiReceiver.cycles,.rejected=multiReceiver.rejected,
   .radioErrors=hybridErrors,.timeouts=hybridTimeouts};uwb_runtime_publish(&record);
 }
 mul_timing_service(started);
}
void mul_hybrid_radio_process(void) {
 if(hybridConfig.schedule.profile==MUL_PROFILE_MULTI){multiProcess();return;}
 uint32_t started=mul_timing_now(),now=HAL_GetTick();mulHybridExpire(&hybridReceiver,now);
 uint32_t status=dwt_readsysstatuslo();
 if(status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {hybridErrors++;hybridRearm();}
 else if(status&DWT_INT_RXFCG_BIT_MASK) {
   uint8_t frame[84],raw[5],hardware[4];unsigned bytes=dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;
   bool shape=bytes==36||bytes==76||bytes==86;uint64_t rx=0;uint32_t age=UINT16_MAX;
#if MUL_HYBRID_FREQ_DIAG
   int32_t ci=0;int16_t coe=0;
#endif
   if(shape) {
     bytes-=2;dwt_readrxdata(frame,(uint16_t)bytes,0);dwt_readrxtimestamp(raw,DWT_COMPAT_NONE);
#if MUL_HYBRID_FREQ_DIAG
     /* Both belong to this CRC-good RX. Read before forcetrxoff/clear/rearm;
      * never poll/wait for CIA. Missing CIADONE means no usable COE sample. */
     ci=dwt_readcarrierintegrator();
     if(status&DWT_INT_CIADONE_BIT_MASK)coe=dwt_readclockoffset();
#endif
     dwt_readsystime(hardware);
     /* DW3000 SYS_TIME is bits39:8 in FOUR bytes (SYS_TIME_LEN), whereas
      * RX_STAMP is five full-resolution bytes. Reconstruct the local40-bit
      * coordinate and use the upper edge of its256-DTU quantization bin so
      * capture age is rounded outwards. Never decode uninitialized byte5. */
     uint64_t systemUpper=255;
     for(unsigned i=0;i<4;i++)systemUpper|=(uint64_t)hardware[i]<<(8*(i+1));
     rx=mulDecodeTimestamp(raw);int64_t elapsed=mulTimestampDifference(systemUpper,rx);
     if(elapsed>=0&&elapsed<=15*INT64_C(63897600))age=(uint32_t)((elapsed+63897599)/63897600);

   }
   hybridRearm(); /* Freeze all diagnostics before RX; shared math/IO follow rearm. */
   bool enabled=false;if(shape)for(unsigned i=0;i<hybridConfig.peer_count;i++)if(hybridConfig.peers[i].enabled&&hybridConfig.peers[i].address==frame[13])enabled=true;
   mulHybridOutput_t out;
   if(shape) {
     unsigned index=bytes==34?0:bytes==74?1:2;
     memcpy(hybridFrames[index].frame,frame,bytes);hybridFrames[index].length=(uint8_t)bytes;
     hybridFrames[index].rx=rx;hybridFrames[index].age=age;hybridFrames[index].observedMs=now;
#if MUL_HYBRID_FREQ_DIAG
     hybridFrames[index].ci=ci;hybridFrames[index].coe=coe;
     hybridFrames[index].coePresent=(status&DWT_INT_CIADONE_BIT_MASK)!=0;
     hybridFrames[index].status=status;
#endif
   }
   if(shape&&enabled&&mulHybridReceive(&hybridReceiver,frame,bytes,rx,age,now,&out)) {
     if(out.associated)hybridLastStatus=out.observation.status;
     /* Prioritize the associated onboard result over this packet's debug copy.
      * Both use the existing bounded, nonblocking telemetry queues. */
     mulTelemetry_t record={0};
     if(out.associated) {
       const mulLocalization_t *o=&out.observation;
       record=(mulTelemetry_t){.type=mulTlmTdoa,.peer=o->follower[0],.remoteRadio=1,
        .uptimeMs=o->observedMs,.exchange=o->followerEpoch,
        .referenceGeneration=o->referenceGeneration,.followerGeneration=o->followerGeneration,
        .captureAgeMs=(uint16_t)age,.legacySlotMs=o->slotMs,.ratePolicy=hybridConfig.mode==5,
        .radioErrors=hybridErrors,.timeouts=hybridTimeouts,
        .tdoa={.reference=o->reference[0],.follower=o->follower[0],
          .referenceSequence=o->referenceSequence,.followerSequence=o->followerSequence,
          .tdoaDtu=o->tdoaDtu,.rangeDifferenceMm=o->rangeDifferenceMm,.ratePpb=o->ratePpb,
          .status=o->status,.clockValid=o->clockValid,.rateState=o->rateState,.rx=o->receiverTimestamp,
          .delayAgeMs=o->delayAgeMs,.delayDtu=o->delayDtu,.clockAgeMs=o->clockAgeMs,
          .delayState=o->delayState,.powerFlags=o->powerFlags,.unknownComponents=o->unknownComponents}};
       uwb_runtime_publish(&record);
     }
     record=(mulTelemetry_t){.type=mulTlmHybridRx,.peer=frame[13],.remoteRadio=1,.transaction=out.packet.sequence,
       .exchange=out.broadcast?out.relation.epoch:0,.quality=out.associated?out.observation.status:255,.flags=mulTlmStampValid,
       .legacyLength=(uint8_t)bytes,.legacyRx=rx,.captureAgeMs=(uint16_t)age,.legacySlotMs=hybridConfig.slot_ms,
       .referenceGeneration=out.referenceGeneration,.followerGeneration=out.followerGeneration,.ratePolicy=hybridConfig.mode==5,
       .radioErrors=hybridErrors};
     memcpy(record.legacyFrame,frame,bytes);uwb_runtime_publish(&record);
     if(out.associated&&hybridConfig.verbosity&&!uwb_runtime_help_active()) {
       char line[192];snprintf(line,sizeof(line),"MULH OBS ref=%lu fol=%lu status=%u clock=%u rate=%u tdoa_dtu=%ld range_diff_mm=%ld power=UNSUPPORTED uncertainty=UNKNOWN",
       (unsigned long)out.observation.referenceSequence,(unsigned long)out.observation.followerSequence,out.observation.status,out.observation.clockValid,out.observation.rateState,(long)out.observation.tdoaDtu,(long)out.observation.rangeDifferenceMm);usb_console_write(line);
     }
   } else if(!shape||!enabled) {
     hybridReceiver.rejected++;
     mulHybridDiagnostic(&hybridReceiver.diagnostics,!shape?mulHybridShape:mulHybridAddress,shape?frame[24]:0,0,0);
   }
 } else if(status&SYS_STATUS_ALL_RX_TO){hybridTimeouts++;hybridRearm();}
 else if(hybridRetry)hybridRearm();
 if((uint32_t)(now-hybridLastReport)>=hybridConfig.telemetry_ms) {
   hybridLastReport=now;uint32_t t[4];mul_timing_snapshot(t);
   mulTelemetry_t r={.type=mulTlmSummary,.peer=0xff,.attempts=hybridReceiver.received,.completed=hybridReceiver.pairs,.rejected=hybridReceiver.rejected,
   .radioErrors=hybridErrors,.timeouts=hybridTimeouts,.spiCycles=t[0],.serviceCycles=t[1],.gapCycles=t[2],.clockHz=t[3]};uwb_runtime_publish(&r);
   if(hybridConfig.verbosity&&!uwb_runtime_help_active()){char line[320];mul_hybrid_radio_status(line,sizeof(line));usb_console_write(line);}
 }
 mul_timing_service(started);
}
#endif
