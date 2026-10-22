#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static void hybrid_frame(const uint8_t *frame,unsigned n,uint64_t rx) {
 memcpy(input,frame,n);length=n+2;rxMarker=mulTimestampMask(rx);status=DWT_INT_RXFCG_BIT_MASK;step();
}
int main(void) {
 memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();command("CFG SET address 0x21");
 unsigned writes=nvWrites;command("CFG SET mode hybrid");assert(nvWrites==writes&&active.mode==0);
 command("CFG SET correction_mm -99");command("CFG SET calibration on");
 command("PEER ADD 0x84");command("PEER ADD 0x85");command("CFG SET verbosity quiet");assert(active.peer==1&&active.calibration==1);
 command("CFG SET wifi on");command("CFG SET telemetry on");command("CFG SET mode hybrid");assert(active.mode==4&&config.calibration==1);persisted();
 unsigned transmitted=starts;uint8_t frame[84];
 const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc},fol[8]={0x85,0,0,0,0,0,0xcf,0xbc};
 for(unsigned policy=0;policy<2;policy++) {
   if(policy)command("CFG SET mode hybrid_static");
   for(unsigned i=0;i<10;i++) {
     tick=20000+policy*20000+i*1000;head=tail=count=0;
     uint64_t tx=mulTimestampMask(MUL_TIMESTAMP_MASK-2*1000*UINT64_C(63897600)+i*1000*UINT64_C(63897600));
     uint64_t rx=mulTimestampMask(100000+i*1000*UINT64_C(63897600));
     mulEncodeTestPacket(frame,ref,i,tx);hybrid_frame(frame,34,rx);
     tick+=MUL_FOLLOWER_SLOT_MS;
     mulTestPacket_t p={.sequence=i+500,.txTimestamp=mulTimestampMask(500000+i*1000*UINT64_C(63897600)+MUL_FOLLOWER_SLOT_DTU)};memcpy(p.source,fol,8);
     mulRelation_t r={.available=true,.epoch=1,.version=1,.referenceSequence=i,.twrSequence=i,.referenceOrigin=tx,
     .followerOrigin=mulTimestampMask(500000+i*1000*UINT64_C(63897600)),.originAgeMs=MUL_FOLLOWER_SLOT_MS,.delayAgeMs=MUL_FOLLOWER_SLOT_MS,.remainingMs=100};
     mulRelationEncode(frame,&p,&r);hybrid_frame(frame,74,rx+MUL_FOLLOWER_SLOT_DTU+1000);
     assert(count>=2);mulTelemetry_t decoded;assert(mulTelemetryDecode(telemetry[(head+TLM_CAPACITY-1)%TLM_CAPACITY],&decoded));
     assert(decoded.type==mulTlmHybridRx&&decoded.legacyLength==74&&decoded.legacyRx==mulTimestampMask(rx+MUL_FOLLOWER_SLOT_DTU+1000)&&!memcmp(decoded.legacyFrame,frame,74));
   }
   assert(hybridReceiver.pairs>0&&hybridLastStatus==(policy?mulDownlinkAvailable:mulDownlinkNoRate));
 }
 assert(starts==transmitted); /* No POLL, RESPONSE or delayed TX in either passive mode. */
 unsigned received=hybridReceiver.received;status=DWT_INT_RXFCG_BIT_MASK|DWT_INT_RXFCE_BIT_MASK;step();assert(hybridReceiver.received==received&&hybridErrors==1);
 tick+=3001;step();assert(!hybridReceiver.rate.hasBaseline&&!hybridReceiver.association.pending);
 writes=nvWrites;command("PEER DISABLE 0x84");assert(nvWrites==writes);command("CFG SET role initiator");assert(nvWrites==writes);
 captured[0]=0;command("HYBRID STATUS");assert(strstr(captured,"passive=1")&&strstr(captured,"last_status=")&&strstr(captured,"ref_fresh=0")&&strstr(captured,"fol_fresh=0"));
 writes=nvWrites;
 char diagnostic[384];
 for(unsigned page=0;page<3;page++) {
   mul_hybrid_radio_diagnostics(page,diagnostic,sizeof(diagnostic));assert(strlen(diagnostic)<317);
   mul_hybrid_radio_frame(page,diagnostic,sizeof(diagnostic));assert(strlen(diagnostic)<317);
 }
 /* All new diagnostic lines must fit the existing USB queue even when
  * counters have grown to their full width; no queue-size increase. */
 mulHybridDiagnostics_t before=hybridReceiver.diagnostics;
 for(unsigned i=0;i<mulHybridReasonCount;i++)hybridReceiver.diagnostics.reason[i]=UINT32_MAX;
 for(unsigned i=0;i<5;i++)hybridReceiver.diagnostics.twrFrames[i]=UINT32_MAX;
 hybridReceiver.diagnostics.relations=hybridReceiver.diagnostics.availableRelations=UINT32_MAX;
 hybridReceiver.diagnostics.lastId=UINT32_MAX;
 for(unsigned page=0;page<3;page++){mul_hybrid_radio_diagnostics(page,diagnostic,sizeof(diagnostic));assert(strlen(diagnostic)<317);}
 hybridReceiver.diagnostics=before;
 captured[0]=0;command("hybrid diag");assert(strstr(captured,"MULH DIAG")&&strstr(captured,"MULH REJECT")&&strstr(captured,"MULH MODEL"));
 captured[0]=0;command("hybrid frames");assert(strstr(captured,"slot=0")&&strstr(captured,"slot=1")&&strstr(captured,"frame="));
 assert(starts==transmitted&&nvWrites==writes);
 command("DIAG RESET");assert(!hybridErrors&&active.calibration==1);uwb_runtime_init();assert(active.mode==5&&active.correction_mm==-99&&active.calibration==1);persisted();
 command("CFG SET mode off");assert(!started&&!mockRadioActive);
 puts("DWM3000 hybrid adapter: strict legacy frames/full RX, no-TX guarantee, original rate policy, CRC precedence, loss, durable configuration/calibration and bounded telemetry passed");return 0;
}
