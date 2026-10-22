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
#include <math.h>
#include <string.h>
#ifndef MUL_ADS_DWM_ANT_DTU
#define MUL_ADS_DWM_ANT_DTU 16380u /* Existing platform nominal, not calibrated */
#endif
#if MUL_ADS_DWM_ANT_DTU > 65535
#error "DWM antenna delay must fit the hardware register"
#endif
#define RX_EVENTS (SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXOVRR_BIT_MASK)
#if MUL_UNIVERSAL
static mulAds_t peerStates[UWB_MAX_PEERS];
static unsigned activePeer, nextPeer, telemetryPeer;
#define state (peerStates[activePeer])
static uint32_t peerStarted[UWB_MAX_PEERS],peerTimeouts[UWB_MAX_PEERS],peerSuccess[UWB_MAX_PEERS];
static int32_t peerRaw[UWB_MAX_PEERS],peerCal[UWB_MAX_PEERS];
static uint8_t peerFlags[UWB_MAX_PEERS];
static uint64_t peerRx[UWB_MAX_PEERS];
#else
static mulAds_t state;
#endif
static uint32_t lastPoll,lastStart,lastReport,errors,invalid,timeouts;
static bool retryRx;
#if MUL_UNIVERSAL
#include "mul_beacon.h"
#include "mul_hybrid_radio.h"
static void beacon_reset(void);
static bool beacon_reset_stats(void);
static void beacon_suspend(void);
static void beacon_process(uint32_t now);
static uwb_config_t config;
static uint64_t runtimeSession;
static bool configured;
static uint32_t lastTelemetry;
static uint64_t lastRemoteStamps[3];
static bool mm(double value,int32_t *out) {
  double x=value*1000.0;if(!isfinite(x)||x<INT32_MIN||x>INT32_MAX){*out=INT32_MIN;return false;}
  *out=(int32_t)llround(x);return true;
}
static mulTelemetry_t telemetry_record(uint8_t type) {
  uint32_t timing[4];mul_timing_snapshot(timing);
  mulTelemetry_t r={.type=type,.peer=state.peer,.exchange=state.session,.transaction=state.transaction,
   .attempts=state.attempts,.completed=state.completed,.aborted=state.aborted,.rejected=state.rejected,.invalid=state.invalidRanges,
   .radioErrors=errors,.timeouts=timeouts,.spiCycles=timing[0],.serviceCycles=timing[1],.gapCycles=timing[2],.clockHz=timing[3]};return r;
}
bool mul_ads_radio_peer_stats(unsigned i,mulTelemetry_t *r) {
  if(i>=config.peer_count)return false;
  mulAds_t *p=&peerStates[i];
  *r=(mulTelemetry_t){.type=mulTlmPeer,.peer=p->peer,.exchange=p->session,.transaction=p->transaction,
    .attempts=p->attempts,.completed=p->completed,.aborted=p->aborted,.rejected=p->rejected,.invalid=p->invalidRanges,
    .timeouts=peerTimeouts[i],.rawMm=peerRaw[i],.calMm=peerCal[i],.flags=peerFlags[i],.uptimeMs=peerSuccess[i]};
  r->stamps[0]=peerRx[i];return true;
}
bool mul_ads_radio_reset_stats(void) {
  if(config.mode>=4)return mul_hybrid_radio_reset_stats();
  if(config.mode>=2)return beacon_reset_stats();
  for(unsigned i=0;i<config.peer_count;i++)if(peerStates[i].stage!=adsIdle)return false;
  for(unsigned i=0;i<config.peer_count;i++) {
    mulAds_t *p=&peerStates[i];p->attempts=p->completed=p->aborted=p->rejected=p->invalidRanges=0;
    memset(p->failedStages,0,sizeof(p->failedStages));peerTimeouts[i]=0;
  }
  errors=invalid=timeouts=0;mul_timing_reset_stats();return true;
}
/* One coordinated initiator: >=100ms start guard, one exchange at a time,
 * 100ms per-stage deadlines. Unavailable peers consume one bounded attempt.
 * Due peers are visited cyclically, never restarted in a tight retry loop. */
static void schedule(uint32_t now) {
  if(config.role!=1||state.stage!=adsIdle||(uint32_t)(now-lastStart)<100U)return;
  for(unsigned n=0;n<config.peer_count;n++) {
    unsigned i=(nextPeer+n)%config.peer_count;
    if(!config.peers[i].enabled||(uint32_t)(now-peerStarted[i])<config.peers[i].interval_ms||peerStates[i].transaction==UINT32_MAX)continue;
    activePeer=i;nextPeer=(i+1)%config.peer_count;peerStarted[i]=lastStart=now;
    mulAdsBegin(&state,now);break;
  }
}
void mul_ads_radio_configure(const uwb_config_t *c,uint64_t session) {config=*c;runtimeSession=session;}
static bool initiator(void) {return config.role==1;}
static unsigned verbosity(void) {return uwb_runtime_help_active()?0:(unsigned)config.verbosity;}
#else
static bool initiator(void) {return MUL_TWR_ROLE==1;}
static unsigned verbosity(void) {return 2;}
#endif
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
#if MUL_UNIVERSAL
int32_t mul_ads_radio_resume(void) {
  if(!configured) return DWT_ERROR;
  lastPoll=lastStart=lastReport=lastTelemetry=HAL_GetTick();
  rearm();return retryRx?DWT_ERROR:DWT_SUCCESS;
}
void mul_ads_radio_stop(void) {
  if(!configured) return;
  if(state.stage!=adsIdle) {mulAdsAbort(&state);logAbort();}
  if(config.mode>=4)mul_hybrid_radio_suspend();
  else if(config.mode>=2)beacon_suspend();
  dwt_forcetrxoff();dwt_writesysstatuslo(RX_EVENTS|DWT_INT_TXFRS_BIT_MASK);
}
#endif
int32_t mul_ads_radio_init(void) {
  if(uwb_diag_configure()!=DWT_SUCCESS) return DWT_ERROR;
#if MUL_UNIVERSAL
  configured=true;
  dwt_settxantennadelay((uint16_t)config.tx_ant_dtu);dwt_setrxantennadelay((uint16_t)config.rx_ant_dtu);
  if(dwt_gettxantennadelay()!=config.tx_ant_dtu||dwt_getrxantennadelay()!=config.rx_ant_dtu) return DWT_ERROR;
  activePeer=nextPeer=telemetryPeer=0;
  memset(peerTimeouts,0,sizeof(peerTimeouts));memset(peerSuccess,0,sizeof(peerSuccess));
  memset(peerFlags,0,sizeof(peerFlags));memset(peerRx,0,sizeof(peerRx));
  for(unsigned i=0;i<config.peer_count;i++) {
    peerStarted[i]=HAL_GetTick();peerRaw[i]=peerCal[i]=INT32_MIN;
    if(!mulAdsInit(&peerStates[i],(uint8_t)config.node,config.peers[i].address,initiator(),runtimeSession))return DWT_ERROR;
  }
  lastTelemetry=HAL_GetTick();beacon_reset();
  if(config.mode>=4)mul_hybrid_radio_configure(&config);
#else
  dwt_settxantennadelay(MUL_ADS_DWM_ANT_DTU); dwt_setrxantennadelay(MUL_ADS_DWM_ANT_DTU);
  if(dwt_gettxantennadelay()!=MUL_ADS_DWM_ANT_DTU || dwt_getrxantennadelay()!=MUL_ADS_DWM_ANT_DTU) return DWT_ERROR;
  mulAdsInit(&state,0x86,0x84,MUL_TWR_ROLE==1,MUL_TWR_SESSION ? MUL_TWR_SESSION : 1);
#endif
  errors=invalid=timeouts=0; lastPoll=lastStart=lastReport=HAL_GetTick();
  char line[192];
  snprintf(line,sizeof(line),"MULH INIT radio=DWM3000 role=%u session=%08" PRIX32 "%08" PRIX32 " ant=%u ch=5 version=1 bytes=56 uwb_only=1 uncalibrated=1",(unsigned)(initiator()?1:2),(uint32_t)(state.session>>32),(uint32_t)state.session,
#if MUL_UNIVERSAL
    (unsigned)config.tx_ant_dtu
#else
    MUL_ADS_DWM_ANT_DTU
#endif
  );
  usb_console_write(line);
#if MUL_UNIVERSAL
  snprintf(line,sizeof(line),"MULH CONFIG node=0x%02X peer=0x%02X identity=%lu tx_ant_dtu=%lu rx_ant_dtu=%lu interval_ms=%lu calibration=%lu correction_mm=%ld",
    state.local,state.peer,(unsigned long)config.identity,(unsigned long)config.tx_ant_dtu,(unsigned long)config.rx_ant_dtu,(unsigned long)config.interval_ms,(unsigned long)config.calibration,(long)config.correction_mm);
  usb_console_write(line);
#endif
  mul_timing_init(); rearm();
#if MUL_UNIVERSAL
  if(retryRx) return DWT_ERROR;
#endif
  return DWT_SUCCESS;
}
void mul_ads_radio_process(void) {
  uint32_t now=HAL_GetTick();
#if MUL_UNIVERSAL
  if(config.mode>=4){mul_hybrid_radio_process();return;}
  if(config.mode>=2){beacon_process(now);return;}
#endif
  if(now==lastPoll) return;
  lastPoll=now;
#if MUL_UNIVERSAL
  schedule(now);
#endif
  uint32_t started=mul_timing_now(); uint32_t completed=state.completed;
  if(mulAdsTick(&state,now)) {
#if MUL_UNIVERSAL
    peerTimeouts[activePeer]++;
#endif
    rearm();logAbort();
  }
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
#if MUL_UNIVERSAL
          if(!initiator()&&state.stage==adsIdle&&p.type==mulAdsPoll&&p.destination==config.node) {
            for(unsigned i=0;i<config.peer_count;i++)if(config.peers[i].enabled&&config.peers[i].address==p.source){activePeer=i;break;}
          }
          if(!config.peers[activePeer].enabled){state.rejected++;rearm();mul_timing_service(started);return;}
#endif
          log=initiator() && state.stage==adsReportRx && !state.pending && p.type==mulAdsReport &&
            p.session==state.session && p.transaction==state.transaction && p.source==state.peer && p.destination==state.local;
#if MUL_UNIVERSAL
          if(log)memcpy(lastRemoteStamps,p.stamps,sizeof(lastRemoteStamps));
#endif
          mulAdsReceive(&state,&p,mulDecodeTimestamp(t),now);
        }
      } else invalid++;
      rearm();
      if(state.aborted!=before) logAbort();
      if(MUL_ADS_TRACE_TIMES && verbosity()>=2 && log) logTimes(&p);
    } else if(status&SYS_STATUS_ALL_RX_TO) {
      timeouts++;
#if MUL_UNIVERSAL
      if(state.stage!=adsIdle)peerTimeouts[activePeer]++;
#endif
      abortExchange(adsAbortRxTimeout);
    }
    else if(retryRx && now%50U==0) rearm();
  }
#if !MUL_UNIVERSAL
  if(initiator() && state.stage==adsIdle && (uint32_t)(now-lastStart)>=1000U) {
    lastStart=now; mulAdsBegin(&state,now);
  }
#endif
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
  if(initiator() && state.completed!=completed) {
    snprintf(line,sizeof(line),"MULH RESULT session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " tof_dtu=%.6f ratio=%.9f nominal_m=%.4f uncalibrated=1 range_valid=%u",(uint32_t)(state.session>>32),(uint32_t)state.session,state.transaction,state.tof,state.rateRatio,state.tof*(299792458.0/63897600000.0),(unsigned)state.rangeValid);
    if(verbosity()) usb_console_write(line);
#if MUL_UNIVERSAL
    double raw=state.tof*(299792458.0/63897600000.0),calibrated;
    uwb_config_t pairConfig=config;if(activePeer)pairConfig.calibration=0;
    bool valid=uwb_config_range(&pairConfig,raw,state.rangeValid,&calibrated);
    int32_t rawMm,calMm;bool rawFits=mm(raw,&rawMm),calFits=mm(calibrated,&calMm);
    uint8_t flags=(state.rangeValid&&rawFits?mulTlmRawValid:0)|(pairConfig.calibration?mulTlmCalibration:0)|(valid&&calFits?mulTlmCalValid:0)|mulTlmStampValid;
    if(state.rangeValid&&rawFits) {
      peerRaw[activePeer]=rawMm;peerCal[activePeer]=calMm;peerFlags[activePeer]=flags;
      peerSuccess[activePeer]=now;peerRx[activePeer]=state.stamps[1];
    }
    snprintf(line,sizeof(line),"MULH CAL session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " raw_m=%.4f calibrated_m=%.4f calibration=%lu range_valid=%u",
      (uint32_t)(state.session>>32),(uint32_t)state.session,state.transaction,raw,calibrated,(unsigned long)pairConfig.calibration,(unsigned)valid);
    if(verbosity())usb_console_write(line);
    if(config.samples) {
      mulTelemetry_t r=telemetry_record(mulTlmRange);
      r.rawMm=rawMm;r.calMm=calMm;r.flags=flags;
      r.quality=(!rawFits||!calFits)?1:0;
      r.stamps[0]=state.stamps[0];r.stamps[1]=lastRemoteStamps[0];r.stamps[2]=lastRemoteStamps[1];
      r.stamps[3]=state.stamps[1];r.stamps[4]=state.stamps[2];r.stamps[5]=lastRemoteStamps[2];
      uwb_runtime_publish(&r);
    }
#endif
  }
  if((uint32_t)(now-lastReport)>=1000U) {
    lastReport=now;
    snprintf(line,sizeof(line),"MULH STAT attempt=%" PRIu32 " done=%" PRIu32 " abort=%" PRIu32 " reject=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32 " stage=%u session=%08" PRIX32 "%08" PRIX32 " bad_range=%" PRIu32,state.attempts,state.completed,state.aborted,state.rejected,invalid,errors,timeouts,state.stage,(uint32_t)(state.session>>32),(uint32_t)state.session,state.invalidRanges);
    if(verbosity()) {usb_console_write(line);mul_timing_report();}
  }
#if MUL_UNIVERSAL
  if(config.telemetry && (uint32_t)(now-lastTelemetry)>=config.telemetry_ms) {
    lastTelemetry=now;
    mulTelemetry_t r;mul_ads_radio_peer_stats(telemetryPeer,&r);uwb_runtime_publish(&r);
    telemetryPeer=(telemetryPeer+1)%config.peer_count;
  }
#endif
  mul_timing_service(started);
}
#if MUL_UNIVERSAL
/* Same radio/profile, mutually exclusive modes. These are arrival-clock models,
 * whose offset includes unknown propagation; no global-time claim is made. */
typedef struct {
  mulArrivalModel_t model;
  uint64_t epoch,rx;uint32_t sequence,lastMs,modelMs,gaps;
  uint8_t source,radio;bool occupied,cached;
} beacon_peer_t;
static beacon_peer_t beacons[UWB_MAX_PEERS];
static uint32_t beaconSequence,previousSequence,pendingSequence,beaconTxAt,beaconDone,beaconRx,beaconAttempts;
static uint64_t previousTx;static bool beaconPending,previousValid;
static void beacon_suspend(void) {beaconPending=false;}
static void beacon_reset(void) {
  memset(beacons,0,sizeof(beacons));beaconSequence=1;previousSequence=pendingSequence=0;
  beaconTxAt=HAL_GetTick();beaconDone=beaconRx=beaconAttempts=0;previousTx=0;beaconPending=previousValid=false;
}
static bool beacon_reset_stats(void) {
  if(beaconPending)return false;
  beaconDone=beaconRx=beaconAttempts=errors=invalid=timeouts=0;mul_timing_reset_stats();return true;
}
bool mul_ads_radio_sync_status(unsigned i,char *line,size_t capacity) {
  if(i>=UWB_MAX_PEERS)return false;
  beacon_peer_t *p=&beacons[i];
  if(!p->occupied){snprintf(line,capacity,"SYNC slot=%u empty",i);return true;}
  bool fresh=(uint32_t)(HAL_GetTick()-p->modelMs)<=5000U;
  snprintf(line,capacity,"SYNC peer=0x%02X radio=%u epoch=%08" PRIX32 "%08" PRIX32 " apparent=%s samples=%lu reject=%lu version=%lu ppm=%.4f residual_dtu=%.3f gaps=%lu offset_includes_propagation=1",
    p->source,p->radio,(uint32_t)(p->epoch>>32),(uint32_t)p->epoch,p->model.valid&&fresh?"VALID":"UNKNOWN",(unsigned long)p->model.samples,(unsigned long)p->model.rejected,
    (unsigned long)p->model.version,(p->model.rate-1)*1e6,p->model.residualDtu,(unsigned long)p->gaps);return true;
}
static mulTelemetry_t beacon_record(uint8_t type,uint8_t peer,uint32_t sequence) {
  uint32_t t[4];mul_timing_snapshot(t);
  mulTelemetry_t r={.type=type,.peer=peer,.transaction=sequence,.exchange=runtimeSession,
    .attempts=beaconAttempts,.completed=config.mode==3?beaconRx:beaconDone,.rejected=invalid,.radioErrors=errors,.timeouts=timeouts,
    .spiCycles=t[0],.serviceCycles=t[1],.gapCycles=t[2],.clockHz=t[3]};return r;
}
static void beacon_receive(const mulBeacon_t *packet,uint64_t rx,uint32_t now) {
  if(packet->source==config.node){invalid++;return;}
  beacon_peer_t *p=NULL;
  for(unsigned i=0;i<UWB_MAX_PEERS;i++)if(beacons[i].occupied&&beacons[i].source==packet->source&&beacons[i].radio==packet->radio){p=&beacons[i];break;}
  if(!p)for(unsigned i=0;i<UWB_MAX_PEERS;i++)if(!beacons[i].occupied){p=&beacons[i];break;}
  if(!p){invalid++;return;}
  if(!p->occupied||p->epoch!=packet->epoch) {
    uint32_t version=p->model.version+1;memset(p,0,sizeof(*p));p->model.version=version;p->model.rate=1;
    p->source=packet->source;p->radio=packet->radio;p->epoch=packet->epoch;p->occupied=true;
  } else if(p->cached) {
    uint32_t advance=packet->sequence-p->sequence;
    if(!advance||advance>=UINT32_C(0x80000000)){invalid++;return;}
    p->gaps+=advance-1;
    if((uint32_t)(now-p->lastMs)>5000U){p->cached=false;p->model.seen=p->model.valid=false;p->model.version++;}
  }
  if(p->model.seen&&(uint32_t)(now-p->modelMs)>5000U) {
    p->model.seen=p->model.valid=false;p->model.version++;
  }
  beaconRx++;
  if(config.samples) {
    mulTelemetry_t r=beacon_record(mulTlmEvent,p->source,packet->sequence);
    r.remoteRadio=p->radio;r.remoteEpoch=p->epoch;r.stamps[0]=rx;r.flags=mulTlmStampValid;uwb_runtime_publish(&r);
  }
  if(packet->markerValid&&p->cached&&packet->previousSequence==p->sequence) {
    mulTimeEvent_t tx={{p->source,p->radio,p->epoch},packet->previousTx,true};
    mulTimeEvent_t received={{(uint8_t)config.node,2,runtimeSession},p->rx,true};
    bool accepted=mulArrivalUpdate(&p->model,tx,received);
    if(accepted)p->modelMs=now;
    mulTelemetry_t r=beacon_record(mulTlmObservation,p->source,packet->previousSequence);
    r.remoteRadio=p->radio;r.remoteEpoch=p->epoch;r.stamps[0]=p->rx;r.stamps[1]=packet->previousTx;
    r.flags=mulTlmStampValid;r.modelVersion=p->model.version;r.quality=(accepted?0:1)|(p->model.valid?2:0);
    uwb_runtime_publish(&r);
  }
  p->rx=rx;p->sequence=packet->sequence;p->lastMs=now;p->cached=true;
}
static void beacon_process(uint32_t now) {
  if(now==lastPoll)return;
  lastPoll=now;uint32_t started=mul_timing_now(),status=dwt_readsysstatuslo();
  if(status&(SYS_STATUS_ALL_RX_ERR|DWT_INT_RXOVRR_BIT_MASK)) {
    errors++;dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);beaconPending=false;rearm();
  } else {
    if(status&DWT_INT_TXFRS_BIT_MASK) {
      uint8_t t[5];dwt_readtxtimestamp(t);dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
      if(beaconPending) {
        previousTx=mulDecodeTimestamp(t);previousSequence=pendingSequence;previousValid=true;beaconPending=false;beaconDone++;
        if(config.samples){mulTelemetry_t r=beacon_record(mulTlmEvent,0xff,previousSequence);r.flags=mulTlmStampValid|mulTlmTx;r.stamps[0]=previousTx;uwb_runtime_publish(&r);}
      }
      if(!(status&DWT_INT_RXFCG_BIT_MASK))rearm();
    }
    if(status&DWT_INT_RXFCG_BIT_MASK) {
      uint8_t frame[MUL_BEACON_LENGTH],t[5];mulBeacon_t p;bool valid=false;
      if((dwt_read_reg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK)==MUL_BEACON_LENGTH+2) {
        dwt_readrxdata(frame,sizeof(frame),0);dwt_readrxtimestamp(t,DWT_COMPAT_NONE);
        valid=mulBeaconDecode(frame,sizeof(frame),&p);
      }
      rearm(); /* Restore RX before model math or telemetry formatting. */
      if(valid&&config.mode==3)beacon_receive(&p,mulDecodeTimestamp(t),now);else if(!valid)invalid++;
    } else if(status&SYS_STATUS_ALL_RX_TO){timeouts++;rearm();}
    else if(retryRx&&now%50U==0)rearm();
  }
  if(beaconPending&&(uint32_t)(now-beaconTxAt)>=100U){timeouts++;beaconPending=false;rearm();}
  if(config.mode==2&&!beaconPending&&beaconSequence&&(uint32_t)(now-beaconTxAt)>=config.interval_ms) {
    beaconTxAt=now;beaconAttempts++;pendingSequence=beaconSequence++; /* Never reuse an uncertain TX sequence in this epoch. */
    mulBeacon_t p={.source=(uint8_t)config.node,.radio=2,.epoch=runtimeSession,.sequence=pendingSequence,
      .previousSequence=previousSequence,.previousTx=previousTx,.markerValid=previousValid};
    uint8_t frame[MUL_BEACON_LENGTH];dwt_forcetrxoff();dwt_writesysstatuslo(RX_EVENTS|DWT_INT_TXFRS_BIT_MASK);
    if(!mulBeaconEncode(frame,sizeof(frame),&p)||dwt_writetxdata(sizeof(frame),frame,0)!=DWT_SUCCESS){errors++;rearm();}
    else {
      dwt_writetxfctrl(sizeof(frame)+2,0,1);
      if(dwt_starttx(DWT_START_TX_IMMEDIATE)!=DWT_SUCCESS){errors++;rearm();}else{beaconPending=true;retryRx=false;}
    }
  }
  if((uint32_t)(now-lastTelemetry)>=config.telemetry_ms) {
    lastTelemetry=now;mulTelemetry_t r=beacon_record(mulTlmSummary,0xff,previousSequence);uwb_runtime_publish(&r);
    if(verbosity()){char line[192];snprintf(line,sizeof(line),"MULN STAT mode=%s tx=%lu rx=%lu err=%lu invalid=%lu timeout=%lu",config.mode==2?"BEACON":"LISTEN",(unsigned long)beaconDone,(unsigned long)beaconRx,(unsigned long)errors,(unsigned long)invalid,(unsigned long)timeouts);usb_console_write(line);}
  }
  mul_timing_service(started);
}
#endif
#endif
