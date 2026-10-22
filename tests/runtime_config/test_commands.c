/* Canonical commands through the real runtime, radio adapter and flash journal.
 * Existing USB/safety dispatcher tests independently exercise the line boundary. */
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static void runtime_step(void) {tick++;uwb_runtime_task();}
static void frame(unsigned i,bool follower) {
 const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc},fol[8]={0x85,0,0,0,0,0,0xcf,0xbc};
 uint64_t ms=63897600,tx=mulTimestampMask(MUL_TIMESTAMP_MASK-2000*ms+i*1000*ms);
 uint64_t origin=500000+i*1000*ms,rx=mulTimestampMask(MUL_TIMESTAMP_MASK-100000+i*1000*ms);
 tick=10000+i*1000+(follower?MUL_FOLLOWER_SLOT_MS:0);head=tail=count=0;
 if(!follower){mulEncodeTestPacket(input,ref,i,tx);length=36;rxMarker=rx;}
 else {
  mulTestPacket_t p={.sequence=500+i,.txTimestamp=mulTimestampMask(origin+MUL_FOLLOWER_SLOT_DTU)};memcpy(p.source,fol,8);
  mulRelation_t r={.version=2,.available=true,.epoch=7,.referenceSequence=i,.twrSequence=i,.referenceOrigin=tx,.followerOrigin=origin,
   .originAgeMs=MUL_FOLLOWER_SLOT_MS,.delayAgeMs=MUL_FOLLOWER_SLOT_MS,.delayDtu=120,.maxAgeMs=6000,.delayState=mulPropagationFresh};
  mulRelationEncode(input,&p,&r);length=76;rxMarker=mulTimestampMask(rx+MUL_FOLLOWER_SLOT_DTU+1000);
 }
 status=DWT_INT_RXFCG_BIT_MASK;runtime_step();
 /* The same service call can enqueue a periodic summary after the RX record.
  * Inspect the RX record itself rather than assuming the last queue entry. */
 bool found=false;
 for(unsigned n=0;n<count;n++) {
  mulTelemetry_t decoded;assert(mulTelemetryDecode(telemetry[(tail+n)%TLM_CAPACITY],&decoded));
  assert(decoded.role==2); /* ADS role stays initiator; wire stays passive. */
  if(decoded.type==mulTlmHybridRx) {
   assert(decoded.transaction==(follower?500+i:i));found=true;
  }
 }
 assert(found);
}
static void legacy_v3_boot(void) {
 uwb_config_t expected=active;uint32_t before=nvWrites;
 uint8_t record[128];uwb_config_encode(record,&expected,77);record[4]=3;record[19]=0;
 uint32_t crc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(crc>>(8*i));
 memset(nv,255,sizeof(nv));memcpy(nv,record,sizeof(record));uwb_runtime_init();
 assert(nvWrites==before&&generation==77&&loadedAtBoot&&uwb_config_equal(&active,&expected));
 assert(started&&active.rf_enabled&&active.protocol_role==UWB_PROTOCOL_RLR);
}
int main(void) {
 memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();
 unsigned writes=nvWrites,tx=starts;flashSafe=false;
 const char *readonly[]={"show","show saved","status","cal show","peer list","diag","diag wrong","hybrid wrong","sync wrong","on extra","off extra","role ref","role follower","role nonsense","addr","mode nonsense","save extra"};
 for(unsigned i=0;i<sizeof(readonly)/sizeof(*readonly);i++){captured[0]=0;command(readonly[i]);drain();assert(!strstr(captured,"ERR UWB-only")&&nvWrites==writes&&starts==tx);}
 flashSafe=true;command("on");assert(!started&&active.mode==0&&nvWrites==writes);
 command("addr 0x8d");command("mode twr");assert(active.mode==1&&!active.rf_enabled&&!started);
 command("cfg set role initiator");command("cfg set identity alr");command("cal on");command("cfg set correction_mm -1234");
 command("wifi on");command("tlm on");assert(wifiEnabled&&active.telemetry&&active.telemetry_ms==1000);
 command("peer add 0x84");command("peer add 0x85");command("peer add 0x42");command("peer del 0x42");
 assert(active.calibration&&active.correction_mm==-1234&&active.identity==1);writes=nvWrites;
 command("save");assert(nvWrites==writes);command("on");assert(started&&active.mode==1&&active.rf_enabled);
 tick+=2000;runtime_step();assert(starts>tx&&state.stage!=adsIdle);unsigned sent=starts;
 command("diag reset");assert(state.stage!=adsIdle); /* Busy exchange is preserved. */
 /* A valid off request stops RF even if flash is unsafe. Do not claim persistence. */
 flashSafe=false;writes=nvWrites;captured[0]=0;command("off");assert(!started&&!mockRadioActive&&!applied&&nvWrites==writes&&starts==sent);
 assert(active.mode==1&&!active.rf_enabled&&!current.rf_enabled&&saved.rf_enabled&&strstr(captured,"UWB RF OFF; persistence not confirmed"));
 command("status");assert(!strstr(captured,"ERR UWB-only"));flashSafe=true;command("off");assert(!started&&!active.rf_enabled&&active.mode==1);persisted();
 writes=nvWrites;uwb_runtime_init();assert(!started&&!active.rf_enabled&&active.mode==1&&nvWrites==writes);
 /* Failure after preflight must not resume the old transmitter either. No
  * new radio session means rollback did not initialize/rearm the old RF mode. */
 command("on");uint64_t sessionBefore=nonce;writeBudget=0;command("off");writeBudget=-1;
 assert(!started&&!mockRadioActive&&!active.rf_enabled&&!current.rf_enabled&&!applied&&saved.rf_enabled&&nonce==sessionBefore);
 command("off");persisted();
 command("on");tick+=2000;runtime_step();assert(started&&state.stage!=adsIdle);sent=starts;
 /* Accepted RLR conversion from an actual transmitter stops it and preserves calibration. */
 command("role rlr");assert(!started&&!mockRadioActive&&active.mode==4&&!active.rf_enabled&&active.protocol_role==UWB_PROTOCOL_RLR);
 assert(active.role==1&&active.identity==1&&active.calibration&&active.correction_mm==-1234&&starts==sent);
 command("mode twr");command("mode beacon");assert(active.mode==4&&!started&&active.protocol_role==UWB_PROTOCOL_RLR);
 command("on");assert(started&&mockRadioActive&&active.mode==4&&active.protocol_role==UWB_PROTOCOL_RLR);sent=starts;
 for(unsigned i=0;i<11;i++){frame(i,false);frame(i,true);}
 /* Only the first cycle lacks the prior per-source cadence needed to pair.
  * Rate warm-up does not gate association (or admit moving metric TDoA). */
 assert(hybridLastStatus==mulDownlinkNoRate&&hybridReceiver.pairs==10&&starts==sent);
 legacy_v3_boot();assert(active.reference==0x84&&active.follower==0x85&&active.calibration&&active.correction_mm==-1234);
 command("off");assert(!started&&!active.rf_enabled&&active.mode==4);uint64_t beforeEpoch=telemetryEpoch;writes=nvWrites;command("off");assert(nvWrites==writes&&telemetryEpoch==beforeEpoch);
 status=DWT_INT_TXFRS_BIT_MASK|DWT_INT_RXFCG_BIT_MASK;tick+=2000;runtime_step();assert(starts==sent&&!started&&!mockRadioActive); /* Actual runtime ignores late events while off. */
 command("diag reset");assert(!hybridReceiver.received&&!hybridReceiver.pairs&&!hybridReceiver.twrActive);
 command("peer disable 0x84");writes=nvWrites;command("on");assert(!started&&!active.rf_enabled&&nvWrites==writes);
 command("peer enable 0x84");command("on");assert(started&&hybridReceiver.pairs==0&&hybridReceiver.rate.status!=mulClockValid);
 frame(0,false);frame(0,true);assert(hybridReceiver.pairs==0&&starts==sent); /* Fresh cadence required after resume. */
 command("mode hybrid_static");assert(started&&active.rf_enabled&&active.mode==5&&active.protocol_role==UWB_PROTOCOL_RLR);
 for(unsigned i=0;i<11;i++){frame(i,false);frame(i,true);}
 assert(hybridLastStatus==mulDownlinkAvailable&&hybridReceiver.pairs==10&&starts==sent);
 command("off");assert(active.mode==5&&!started);persisted();command("on");assert(started&&active.mode==5&&hybridReceiver.pairs==0);
 command("mode hybrid");assert(started&&active.mode==4);
 for(unsigned i=0;i<11;i++){frame(i,false);frame(i,true);}
 assert(hybridLastStatus==mulDownlinkNoRate&&hybridReceiver.pairs==10&&starts==sent);
 command("off");command("role standalone");assert(active.protocol_role==0&&active.mode==0&&!started&&active.calibration);
 command("mode beacon");assert(active.mode==2&&!started);command("on");tick+=2000;runtime_step();assert(beaconPending);sent=starts;
 command("off");assert(!beaconPending&&!started&&active.mode==2);command("diag reset");assert(!beaconAttempts);
 status=DWT_INT_TXFRS_BIT_MASK;tick+=1000;runtime_step();assert(starts==sent&&!started&&!mockRadioActive);
 /* Legacy mode alias still chooses the original role/RF behavior. */
 command("cfg set mode twr");assert(started&&active.mode==1&&active.rf_enabled&&active.protocol_role==0);
 command("cfg set mode off");assert(!started&&active.mode==0&&!active.rf_enabled);persisted();
 puts("Canonical UWB aliases: protocol/ADS separation, persistent RF/mode, v3/v4 boot, fail-safe off, RLR-from-TX no-TX, NO_RATE/fresh resume, calibration/peers, busy reset and legacy mode compatibility passed");return 0;
}
