#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
/* Reuse deterministic clock/geometry stimulus; production radio, reconstruction,
 * runtime queues and telemetry codecs remain the system under test. */
#define main shared_multi_core_tests_not_invoked
#include "../../ThirdParty/mul_core/tests/test_multi_receiver.c"
#undef main
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
 if(trace)fclose(trace);
 puts("Multi RLR actual radio/queues/telemetry: N2/3/4/8, signed geometry/skews, partial vectors, native40/cycle32/uptime32 wrap, no-rate, Wi-Fi priority and zero TX passed");return 0;
}
