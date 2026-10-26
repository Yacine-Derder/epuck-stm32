#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
/* Reuse deterministic clock/geometry stimulus; production radio, reconstruction,
 * runtime queues and telemetry codecs remain the system under test. */
#define main shared_multi_core_tests_not_invoked
#include "../../ThirdParty/mul_core/tests/test_multi_receiver.c"
#undef main
static void fitted_clock_runtime(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 0x8d");command("peer add 0x84");command("peer add 0x85");
 command("mode hybrid_static");command("off");command("cfg roster set 0x84 0x85 0x86 0x87");
 command("cfg set clock_estimator regression");command("cfg set clock_window 30");command("cfg set samples on");command("cfg set telemetry on");persisted();
 assert(current.clock.estimator==1&&current.clock.window==30);uwb_runtime_init();assert(active.clock.window==30&&active.telemetry&&active.samples);
 command("on");unsigned transmissions=starts,writes=nvWrites;
 command("cfg set clock_estimator median");assert(nvWrites==writes&&current.clock.estimator==1);
 for(unsigned cycle=0;cycle<45;cycle++) {
  for(unsigned slot=0;slot<4;slot++) {
   uint8_t frame[90];uint64_t rx;size_t n=packet(&active.schedule,slot,cycle,cycle,true,frame,&rx);
   head=tail=count=wifiHead=wifiTail=wifiCount=0;memcpy(input,frame,n);length=n+2;rxMarker=rx;
   tick=10000+cycle*1000+slot*10;status=DWT_INT_RXFCG_BIT_MASK;mul_hybrid_radio_process();
   if(!slot) {
    bool diagnostic=false;
    for(unsigned i=0;i<count;i++){mulTelemetry_t value;assert(mulTelemetryDecode(telemetry[(tail+i)%TLM_CAPACITY],&value));if(value.type==mulTlmClock){diagnostic=true;assert(value.clock.version==2&&value.captureAgeMs==1&&value.transaction==1000);assert(value.clock.method==1&&value.clock.referenceSequence==cycle&&value.clock.referenceTx==mulDecodeTimestamp(frame+29)&&value.clock.rawRx==rx);}}
    assert(diagnostic);
   }
  }
  tick=10000+cycle*1000+60;status=0;mul_hybrid_radio_process();
 }
 assert(multiReceiver.fit.ready&&multiReceiver.fit.count==30&&abs(multiReceiver.rate.skewPpb-15000)<=1&&starts==transmissions);
 command("clock show");drain();assert(strstr(captured,"CLOCK method=1 window=30"));
 command("off");command("cfg roster set 0x84 0x85");persisted();uwb_runtime_init();
 assert(!active.schedule.profile&&active.clock.estimator==1&&active.clock.window==30);
 command("on");assert(hybridReceiver.clockConfig.estimator==1&&hybridReceiver.clockConfig.window==30);
 command("off");command("cfg set clock_estimator median");persisted();
}
static void experimental_runtime(FILE *trace) {
 const unsigned periods[]={100,200,500,1000,2000},spacing[]={2,3,5,7,17};
 for(unsigned trial=0;trial<5;trial++) {
  memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 0x8d");command("mode hybrid_static");command("off");
  command("cfg roster set 0x84 0x85 0x86 0x87");command("cfg set profile experimental");
  command("cfg set tx_lead_us 500");command("cfg set rx_budget_us 500");command("cfg set airtime_us 400");
  command("cfg set alr_alr_twr off");char cmd[64];snprintf(cmd,sizeof(cmd),"cfg set slot_ms %u",spacing[trial]);command(cmd);
  snprintf(cmd,sizeof(cmd),"cfg set cycle_ms %u",periods[trial]);command(cmd);
  command("cfg set clock_estimator regression");command("cfg set samples on");command("cfg set telemetry on");persisted();
  uwb_runtime_init();assert(active.schedule.profile==3&&active.slot_ms==spacing[trial]);command("on");
  assert(multiReceiver.clockConfig.periodMs==periods[trial]);unsigned tx=starts;
  const char *blocked[]={"tx_lead_us 1000","rx_budget_us 1000","airtime_us 1000","guard_us 200","alr_alr_twr on","slot_ms 10","cycle_ms 1000","profile multi"};
  unsigned writes=nvWrites;for(unsigned i=0;i<8;i++){snprintf(cmd,sizeof(cmd),"cfg set %s",blocked[i]);command(cmd);assert(nvWrites==writes);}
  for(unsigned step=0;step<35;step++) {
   uint32_t cycle=100+step,base=10000+step*periods[trial];int64_t t=(int64_t)step*periods[trial]*MS;
   uint64_t reference=mulTimestampMask(MUL_TIMESTAMP_MASK-200*MS+t);
   for(unsigned slot=0;slot<4;slot++) {
    int64_t offset=(int64_t)slot*spacing[trial]*MS,origin=500000+scale(t,35000);
    unsigned ageCycles=2000/periods[trial];
    mulRelation_t relation={.version=2,.epoch=slot+1,.referenceSequence=cycle,.twrSequence=cycle-ageCycles,
     .referenceOrigin=reference,.followerOrigin=mulTimestampMask(origin),.skewPpb=35000,
     .originAgeMs=slot*spacing[trial]+1,.delayAgeMs=ageCycles*periods[trial]+slot*spacing[trial],.delayDtu=120,.maxAgeMs=6000,
     .delayState=ageCycles<=1?mulPropagationFresh:mulPropagationReused,.available=true,.period10ms=periods[trial]/10};
    uint64_t timestamp=slot?mulTimestampMask(origin+scale(offset+120,35000)):reference;
    rxMarker=mulTimestampMask(800000+scale(t+(slot?offset+120+(slot%2?477:-477):0)+4000,15000));
    size_t n=mulMultiBroadcastEncode(input,sizeof(input),&active.schedule,slot,cycle,timestamp,slot?&relation:NULL);assert(n);
    length=n+2;tick=base+slot*spacing[trial];status=DWT_INT_RXFCG_BIT_MASK;
    head=tail=count=wifiHead=wifiTail=wifiCount=0;
    captureServiceDtu=step==10&&!slot?16*MS:638976;mul_hybrid_radio_process();captureServiceDtu=638976;
    for(unsigned i=0;i<count;i++){const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));
     if(trace)fprintf(trace,"%s\n",line);
     if(r.type==mulTlmClock)assert(r.transaction==periods[trial]&&r.captureAgeMs==(step==10?UINT16_MAX:1));
     if(r.type==mulTlmMultiRx){assert(r.quality==3&&r.legacySlotMs==spacing[trial]);if(slot)assert(!memcmp(hybridFrames[1].frame,input,n));}
    }
   }
   head=tail=count=wifiHead=wifiTail=wifiCount=0;tick=base+3*spacing[trial]+20;status=0;mul_hybrid_radio_process();
   bool vector=false;
   for(unsigned i=0;i<count;i++){const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));
    if(trace)fprintf(trace,"%s\n",line);
    if(r.type==mulTlmVector){vector=true;assert(r.quality==3&&r.vector.periodMs==periods[trial]);
     if(step==10)assert(!r.vector.validMask);
     else if(step==9||step>=16)for(unsigned j=0;j<3;j++)assert(r.vector.values[j].status==mulDownlinkAvailable&&abs(r.vector.values[j].tdoaDtu-((j+1)%2?477:-477))<=3);
    }
   }
   assert(vector);
  }
  assert(starts==tx);command("off");
 }
}
int main(void) {
 FILE *trace=NULL;const char *tracePath=getenv("MUL_MULTI_TRACE");if(tracePath){trace=fopen(tracePath,"w");assert(trace);}
 const unsigned counts[]={2,3,4,8};
 for(unsigned c=0;c<4;c++)for(unsigned stationary=0;stationary<2;stationary++) {
  memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 0x8d");
  command("peer add 0x84");command("peer add 0x85");command("cfg set mode hybrid_static");command("off");
  const char *rosters[]={"cfg roster set 0x84 0x85","cfg roster set 0x84 0x85 0x86","cfg roster set 0x84 0x85 0x86 0x87","cfg roster set 0x84 0x85 0x86 0x87 0x88 0x89 0x8a 0x8b"};
  command(rosters[c]);command("cfg set profile multi");
  if(!stationary)command("cfg set mode hybrid");
  command("cfg set telemetry on");command("cfg set wifi on");command("on");
  assert(started&&active.schedule.profile==2&&multiReceiver.schedule.count==counts[c]);
  unsigned transmissions=starts;
  for(unsigned cycle=0;cycle<10;cycle++) {
   uint32_t base=UINT32_MAX-5000+cycle*1000;
   for(unsigned slot=0;slot<counts[c];slot++) {
    if(cycle==6&&slot==2)continue;
    uint8_t frame[90];uint64_t rx;size_t n=packet(&active.schedule,slot,cycle,UINT32_MAX-3+cycle,!(cycle==6&&slot==3),frame,&rx);
    assert(n<=sizeof(input));head=tail=count=0;wifiHead=wifiTail=wifiCount=0;
    memcpy(input,frame,n);length=n+2;rxMarker=rx;tick=base+slot*10;status=DWT_INT_RXFCG_BIT_MASK;
    mul_hybrid_radio_process();assert(starts==transmissions&&mockRadioActive);
   }
   // Saturate raw trace queues: the cycle result must displace optional evidence.
   mulTelemetry_t raw={.type=mulTlmSummary};while(count<TLM_CAPACITY)uwb_runtime_publish(&raw);
   tick=base+(counts[c]-1)*10+21;status=0;mul_hybrid_radio_process();
   bool found=false;
   for(unsigned i=0;i<count;i++) {
    mulTelemetry_t result;assert(mulTelemetryDecode(telemetry[(tail+i)%TLM_CAPACITY],&result));
    if(result.type!=mulTlmVector)continue;
    found=true;
    if(trace)fprintf(trace,"%s\n",telemetry[(tail+i)%TLM_CAPACITY]);
    assert(result.uptimeMs==base&&result.vector.cycle==UINT32_MAX-3+cycle&&result.vector.count==counts[c]);
    assert(result.vector.fingerprint==mulScheduleFingerprint(&active.schedule));
    bool wifi=false;for(unsigned j=0;j<wifiCount;j++)if(!strcmp(telemetry[(tail+i)%TLM_CAPACITY],wifiQueue[(wifiTail+j)%TLM_CAPACITY]))wifi=true;
    assert(wifi);
    for(unsigned slot=1;slot<counts[c];slot++) {
     const mulMultiValue_t *v=&result.vector.values[slot-1];
     if(cycle==6&&slot==2){assert(v->reason==mulMultiMissing);continue;}
     if(cycle==6&&slot==3){assert(v->status==mulDownlinkNoRelation);continue;}
     if(cycle>=5&&stationary)assert(v->status==4&&abs(v->tdoaDtu-differences[slot-1])<=3);
     if(!stationary)assert(v->status==mulDownlinkNoRate&&v->rangeDifferenceMm==INT32_MIN);
    }
   }
   assert(found&&starts==transmissions);
  }
  command("off");assert(!started&&!mockRadioActive);persisted();
 }
 fitted_clock_runtime();
 FILE *experimental=NULL;const char *experimentalPath=getenv("MUL_TIMING_TRACE");if(experimentalPath){experimental=fopen(experimentalPath,"w");assert(experimental);}
 experimental_runtime(experimental);if(experimental)fclose(experimental);
 if(trace)fclose(trace);
 puts("Multi RLR actual radio/queues/telemetry: N2/3/4/8, signed geometry/skews, partial vectors, native40/cycle32/uptime32 wrap, no-rate, Wi-Fi priority and zero TX passed");return 0;
}
