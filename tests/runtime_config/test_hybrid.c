#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
#include "hybrid_vectors.h"
static void hybrid_frame(const uint8_t *frame,unsigned n,uint64_t rx) {
 memcpy(input,frame,n);length=n+2;rxMarker=mulTimestampMask(rx);status=DWT_INT_RXFCG_BIT_MASK;step();
}
static void twr_exchange(const uint8_t ref[8],bool loseResponse) {
 mul_hybrid_radio_configure(&active);
 uint8_t frame[84];uint32_t base=UINT32_MAX-700;
 uint64_t tx=mulTimestampMask(MUL_TIMESTAMP_MASK-660*UINT64_C(63897600));
 uint64_t rx=mulTimestampMask(MUL_TIMESTAMP_MASK-680*UINT64_C(63897600));
 tick=base;mulEncodeTestPacket(frame,ref,10,tx);hybrid_frame(frame,34,rx);
 uint64_t ms=63897600,stamps[6];
 stamps[0]=mulTimestampMask(tx+650*ms);stamps[1]=900000000000ULL;
 stamps[2]=mulTimestampMask(stamps[1]+20*ms);stamps[3]=mulTimestampMask(stamps[0]+20*ms+200);
 stamps[4]=mulTimestampMask(stamps[3]+20*ms);stamps[5]=mulTimestampMask(stamps[2]+20*ms+200);
 for(unsigned stage=0;stage<5;stage++) {
   if(loseResponse&&stage==1)continue;
   mulTwrPacket_t p={.type=mulTwrPoll+stage,.id=10};unsigned used=stage==0?1:stage==1?3:stage==2?5:6;
   memcpy(p.stamps,stamps,used*sizeof(uint64_t));
   p.txTimestamp=stage<3?p.stamps[used-1]:stage==3?mulTimestampMask(stamps[5]+20*ms):mulTimestampMask(stamps[4]+40*ms+200);
   if(stage==4)p.tof=100;
   mulTwrEncode(frame,&p);tick=base+650+stage*20;
   hybrid_frame(frame,84,mulTimestampMask(rx+(650+stage*20)*ms));
 }
 assert(hybridReceiver.twrCompleted==(loseResponse?0u:1u));
 if(loseResponse){assert(hybridReceiver.diagnostics.reason[mulHybridMissingTwr]);tick=base+900;step();assert(hybridReceiver.twrTimeouts==1);}
 else assert(!hybridReceiver.twrRejected&&!hybridReceiver.twrActive);
}
int main(void) {
 hardware_vectors();
 memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();command("CFG SET address 0x21");
 unsigned writes=nvWrites;command("CFG SET mode hybrid");assert(nvWrites==writes&&active.mode==0);
 command("CFG SET correction_mm -99");command("CFG SET calibration on");
 command("PEER ADD 0x84");command("PEER ADD 0x85");command("CFG SET verbosity summary");assert(active.peer==1&&active.calibration==1);
 command("CFG SET wifi on");command("CFG SET telemetry on");command("CFG SET mode hybrid");assert(active.mode==4&&config.calibration==1);persisted();
 unsigned transmitted=starts;uint8_t frame[84];mulTelemetry_t lastTdoa={0};
 const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc},fol[8]={0x85,0,0,0,0,0,0xcf,0xbc};
 for(unsigned policy=0;policy<2;policy++) {
   if(policy)command("CFG SET mode hybrid_static");
   for(unsigned i=0;i<10;i++) {
     tick=20000+policy*20000+i*1000;head=tail=count=0;wifiHead=wifiTail=wifiCount=0;
     uint64_t tx=mulTimestampMask(MUL_TIMESTAMP_MASK-2*1000*UINT64_C(63897600)+i*1000*UINT64_C(63897600));
     uint64_t rx=mulTimestampMask(100000+i*1000*UINT64_C(63897600));
     mulEncodeTestPacket(frame,ref,i,tx);hybrid_frame(frame,34,rx);
     tick+=MUL_FOLLOWER_SLOT_MS;
     mulTestPacket_t p={.sequence=i+500,.txTimestamp=mulTimestampMask(500000+i*1000*UINT64_C(63897600)+MUL_FOLLOWER_SLOT_DTU)};memcpy(p.source,fol,8);
     mulRelation_t r={.available=true,.epoch=1,.version=1,.referenceSequence=i,.twrSequence=i,.referenceOrigin=tx,
     .followerOrigin=mulTimestampMask(500000+i*1000*UINT64_C(63897600)),.originAgeMs=MUL_FOLLOWER_SLOT_MS,.delayAgeMs=MUL_FOLLOWER_SLOT_MS,.remainingMs=100};
     mulRelationEncode(frame,&p,&r);hybrid_frame(frame,74,rx+MUL_FOLLOWER_SLOT_DTU+1000);
     assert(count>=2);mulTelemetry_t decoded;assert(mulTelemetryDecode(telemetry[(head+TLM_CAPACITY-1)%TLM_CAPACITY],&decoded));
     assert(decoded.captureAgeMs==1);
     assert(decoded.type==mulTlmHybridRx&&decoded.legacyLength==74&&decoded.legacyRx==mulTimestampMask(rx+MUL_FOLLOWER_SLOT_DTU+1000)&&!memcmp(decoded.legacyFrame,frame,74));
     bool tdoaFound=false;
     for(unsigned n=0;n<count;n++) {
       mulTelemetry_t t;assert(mulTelemetryDecode(telemetry[(tail+n)%TLM_CAPACITY],&t));
       if(t.type!=mulTlmTdoa)continue;
       lastTdoa=t;tdoaFound=true;assert(t.tdoa.referenceSequence==i&&t.tdoa.followerSequence==i+500&&t.tdoa.reference==0x84&&t.tdoa.follower==0x85);
       assert(t.uptimeMs==tick&&t.tdoa.rx==mulTimestampMask(rx+MUL_FOLLOWER_SLOT_DTU+1000)&&t.ratePolicy==policy);
       const char *log=NULL,*next=captured;while((next=strstr(next,"MULH OBS ref="))){log=next;next++;}assert(log);
       unsigned rs,fs,st,clock,rate;int dt,mm;
       assert(sscanf(log,"MULH OBS ref=%u fol=%u status=%u clock=%u rate=%u tdoa_dtu=%d range_diff_mm=%d",&rs,&fs,&st,&clock,&rate,&dt,&mm)==7);
       assert(t.tdoa.referenceSequence==rs&&t.tdoa.followerSequence==fs&&t.tdoa.status==st&&t.tdoa.clockValid==clock&&t.tdoa.rateState==rate&&t.tdoa.tdoaDtu==dt&&t.tdoa.rangeDifferenceMm==mm);
       bool wifi=false;for(unsigned j=0;j<wifiCount;j++)if(!strcmp(telemetry[(tail+n)%TLM_CAPACITY],wifiQueue[(wifiTail+j)%TLM_CAPACITY]))wifi=true;
       assert(wifi); /* The actual separate Wi-Fi queue holds identical bytes. */
     }
     assert(tdoaFound==(i>0));
   }
   assert(hybridReceiver.pairs>0&&hybridLastStatus==(policy?mulDownlinkAvailable:mulDownlinkNoRate));
 }
 /* Every associated result gets a publish attempt; bounded queues account for
  * loss rather than waiting in the radio path. Capture uptime is not rewritten. */
 head=tail=count=0;wifiHead=wifiTail=wifiCount=0;lastTdoa.uptimeMs=17;
 uwb_runtime_publish(&lastTdoa);mulTelemetry_t queued;assert(mulTelemetryDecode(telemetry[tail],&queued)&&queued.uptimeMs==17);
 for(unsigned i=1;i<TLM_CAPACITY;i++)uwb_runtime_publish(&lastTdoa);
 uint32_t usbBefore=dropped,wifiBefore=wifiDrops;uwb_runtime_publish(&lastTdoa);
 assert(count==TLM_CAPACITY&&wifiCount==TLM_CAPACITY&&dropped==usbBefore+1&&wifiDrops==wifiBefore+1&&starts==transmitted);
 head=tail=count=0;wifiHead=wifiTail=wifiCount=0;
 /* The reported v2 state is usable with a valid local capture age. It is
  * an inter-ALR propagation measurement age, not RX service/queue latency. */
 command("CFG SET mode hybrid");
 for(unsigned i=0;i<3;i++) {
   uint64_t ms=63897600,tx=100000+i*1000*ms,rx=200000+i*1000*ms;
   tick=100000+i*1000;mulEncodeTestPacket(frame,ref,2910+i,tx);hybrid_frame(frame,34,rx);
   mulTestPacket_t p={.sequence=151+i,.txTimestamp=mulTimestampMask(500000+i*1000*ms+MUL_FOLLOWER_SLOT_DTU)};memcpy(p.source,fol,8);
   mulRelation_t r={.version=2,.available=true,.epoch=7,.referenceSequence=2910+i,.twrSequence=2905+i,
   .referenceOrigin=tx,.followerOrigin=500000+i*1000*ms,.originAgeMs=502,.delayAgeMs=4879,
   .maxAgeMs=6000,.delayDtu=120,.delayState=mulPropagationReused};
   mulRelationEncode(frame,&p,&r);tick+=MUL_FOLLOWER_SLOT_MS;hybrid_frame(frame,74,rx+MUL_FOLLOWER_SLOT_DTU+1000);
 }
 assert(hybridLastStatus==mulDownlinkNoRate&&hybridReceiver.relation.remainingMs==1120);
 assert(!hybridReceiver.diagnostics.reason[mulHybridStaleRelation]);
 /* Real service delay is still rejected, not concealed by a larger timeout. */
 captureServiceDtu=20*UINT64_C(63897600);tick+=1000;
 mulTwrPacket_t late={.type=mulTwrPoll,.id=2910,.txTimestamp=1000,.stamps={1000}};
 mulTwrEncode(frame,&late);hybrid_frame(frame,84,1000);
 assert(hybridReceiver.diagnostics.lastReason==mulHybridCaptureAge&&hybridFrames[2].age==UINT16_MAX);
 captureServiceDtu=638976;
 twr_exchange(ref,false);twr_exchange(ref,true);
 assert(starts==transmitted); /* No POLL, RESPONSE or delayed TX in either passive mode. */
 unsigned received=hybridReceiver.received;status=DWT_INT_RXFCG_BIT_MASK|DWT_INT_RXFCE_BIT_MASK;step();assert(hybridReceiver.received==received&&hybridErrors==1);
 tick+=3001;step();assert(!hybridReceiver.rate.hasBaseline&&!hybridReceiver.association.pending);
 writes=nvWrites;command("PEER DISABLE 0x84");assert(nvWrites==writes);
 command("CFG SET role initiator");assert(active.role==1&&active.protocol_role==UWB_PROTOCOL_RLR&&starts==transmitted);
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
 captured[0]=0;command("hybrid frames");assert(strstr(captured,"slot=0")&&strstr(captured,"slot=1")&&strstr(captured,"absent"));
 assert(starts==transmitted&&nvWrites==writes);
 command("DIAG RESET");assert(!hybridErrors&&active.calibration==1);uwb_runtime_init();assert(active.mode==4&&active.correction_mm==-99&&active.calibration==1);persisted();
 command("CFG SET mode off");assert(!started&&!mockRadioActive);
 puts("DWM3000 hybrid adapter: strict legacy frames/full RX, no-TX guarantee, original rate policy, CRC precedence, loss, durable configuration/calibration and bounded telemetry passed");return 0;
}
