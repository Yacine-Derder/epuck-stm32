#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
#define main portable_receiver_tests
#include "../../ThirdParty/mul_core/tests/test_multi_receiver.c"
#undef main
int main(void) {
 FILE *trace=NULL;const char *path=getenv("MUL_PLAN_TRACE");if(path){trace=fopen(path,"w");assert(trace);}
 const unsigned counts[]={2,4,8},periods[]={100,200,1000};
 for(unsigned trial=0;trial<3;trial++)for(unsigned policy=0;policy<4;policy++) {
  unsigned period=policy==3?200:periods[trial],slotMs=policy==3?5:3;
  memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 141");command(policy?"mode hybrid_static":"mode hybrid");command("off");
  command("plan init");command("plan set lead_us 1000");command("plan set process_us 500");command(policy==3?"plan set slot_ms 5":"plan set slot_ms 3");command("plan set reply_ms 5");
  const char *rosters[]={"plan alrs 32 33","plan alrs 32 33 34 35","plan alrs 32 33 34 35 36 37 38 39"};command(rosters[trial]);command("plan rlrs 141 142");
  char cmd[64];snprintf(cmd,sizeof(cmd),"plan set cycle_ms %u",period);command(cmd);command("plan set aa on");
  if(policy==2)command("plan set aa_start_delay_us 1000");
  if(policy&&policy!=3){command("plan set policy freshness");command("plan set aa_priority 7");command("plan set ar_priority 1");}
  if(policy==3)command("plan mode tdma");
  command("cfg set clock_estimator kalman");command("cfg set telemetry on");command("cfg set wifi on");command("cfg set samples on");command("plan use on");persisted();
  assert(current.plan.aaStartDelayUs==(policy==2?1000u:0u));
  assert(mulRangingPlanVersion(&current.plan)==(policy==3?5:policy==2?4:policy?3:2));
  unsigned writes=nvWrites;uwb_runtime_init();assert(current.plan_selected&&!current.rf_enabled&&nvWrites==writes);command("on");
  writes=nvWrites;uwb_runtime_init();assert(started&&current.rf_enabled&&current.plan_selected&&nvWrites==writes);
  assert(started&&current.rf_enabled&&multiReceiver.plan==&hybridConfig.plan&&multiReceiver.schedule.count==counts[trial]);
  assert(multiReceiver.clockConfig.periodMs==period&&!current.schedule.profile&&current.slot_ms==10);
  unsigned transmissions=starts;uint32_t hash=mulRangingPlanFingerprint(&active.plan);
  writes=nvWrites;command("plan set reply_ms 10");assert(nvWrites==writes);
  for(unsigned step=0;step<20;step++) {
   uint32_t cycle=UINT32_MAX-8+step,base=UINT32_MAX-4000+step*period;int64_t t=(int64_t)step*period*MS;
   uint64_t reference=mulTimestampMask(MUL_TIMESTAMP_MASK-200*MS+t);
   for(unsigned slot=0;slot<counts[trial];slot++) {
    if(step==12&&slot==1)continue;
    int64_t offset=(int64_t)slot*slotMs*MS,origin=500000+scale(t,35000);
    mulRelation_t relation={.version=2,.epoch=slot+1,.referenceSequence=cycle,.twrSequence=cycle-1,
     .referenceOrigin=reference,.followerOrigin=mulTimestampMask(origin),.skewPpb=35000,.originAgeMs=slot*slotMs+1,
     .delayAgeMs=period+slot*slotMs,.delayDtu=120,.maxAgeMs=6000,.delayState=mulPropagationFresh,
     .available=true,.period10ms=period/10};
    if(policy){relation.version=3;relation.externalPose=1;relation.delayState=mulPropagationReused;}
    uint64_t tx=slot?mulTimestampMask(origin+scale(offset+120,35000)):reference;
    rxMarker=mulTimestampMask(800000+scale(t+(slot?offset+120+(slot%2?477:-477):0)+4000,15000));
    size_t n=mulPlanBroadcastEncode(input,sizeof(input),&active.plan,slot,cycle,tx,slot?&relation:NULL);assert(n);
    /* Change live levels in flight, preserving the complete production receiver,
     * immutable adapter config, transport queues/sequence and radio ownership. */
    mulMultiReceiver_t beforeReceiver=multiReceiver;uwb_config_t beforeConfig=hybridConfig;
    unsigned beforeWrites=nvWrites,beforeAccepted=accepted;uint64_t beforeEpoch=telemetryEpoch;
    char beforeUsb[sizeof(telemetry)],beforeWifi[sizeof(wifiQueue)];
    memcpy(beforeUsb,telemetry,sizeof(beforeUsb));memcpy(beforeWifi,wifiQueue,sizeof(beforeWifi));
    const char *levels[]={"cfg set verbosity quiet","cfg set verbosity normal","cfg set verbosity debug"};
    command(levels[(step+slot)%3]);assert(active.verbosity==(step+slot)%3);
    assert(!memcmp(&beforeReceiver,&multiReceiver,sizeof(beforeReceiver))&&!memcmp(&beforeConfig,&hybridConfig,sizeof(beforeConfig)));
    assert(nvWrites==beforeWrites&&accepted==beforeAccepted&&telemetryEpoch==beforeEpoch&&starts==transmissions);
    assert(!memcmp(beforeUsb,telemetry,sizeof(beforeUsb))&&!memcmp(beforeWifi,wifiQueue,sizeof(beforeWifi)));
    // A full-plan mismatch cannot pass solely because its broadcast schedule agrees.
    if(step==10&&slot==1)input[36]^=1;
    length=n+2;tick=base+slot*slotMs;status=DWT_INT_RXFCG_BIT_MASK;head=tail=count=wifiHead=wifiTail=wifiCount=0;
    captureServiceDtu=step==11&&!slot?16*MS:638976;mul_hybrid_radio_process();captureServiceDtu=638976;
    unsigned queued=count,wifiQueued=wifiCount;size_t logBefore=strlen(captured);
    if(policy==3)humanLastSummary=tick-5000; // Force summary eligibility inside broadcast phase.
    mul_hybrid_radio_log_poll();assert(count==queued&&wifiCount==wifiQueued);
    if(policy==3)assert(strlen(captured)==logBefore);
    bool rawFound=false;
    for(unsigned i=0;i<count;i++) {
     const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));
     if(trace)fprintf(trace,"%s\n",line);
     if(r.type==mulTlmClock)assert(r.transaction==period);
     else if(r.type==mulTlmPlanRx){rawFound=true;assert(r.quality==4&&r.scheduleFingerprint==hash&&r.legacyLength==n);assert(!memcmp(r.legacyFrame,input,n));}
     else assert(r.type==mulTlmSummary||(policy==3&&r.type==mulTlmPlanQuality));
    }
    assert(rawFound!= (step==10&&slot==1));
   }
   // The planned result must displace optional traces on both saturated queues.
   mulTelemetry_t filler={.type=mulTlmSummary};while(count<TLM_CAPACITY)uwb_runtime_publish(&filler);
   tick=base+(counts[trial]-1)*slotMs+20;status=0;mul_hybrid_radio_process();bool found=false;
   for(unsigned i=0;i<count;i++) {
    const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));if(r.type!=mulTlmPlanVector)continue;
    found=true;if(policy&&r.vector.sourceVersion)for(unsigned j=1;j<counts[trial];j++)assert(r.vector.values[j-1].source==((r.vector.seenMask&(1u<<j))?2:0));
    if(trace)fprintf(trace,"%s\n",line);
    assert(r.quality==4&&r.uptimeMs==base&&r.vector.fingerprint==hash&&r.vector.periodMs==period&&r.vector.count==counts[trial]);
    bool wifi=false;for(unsigned j=0;j<wifiCount;j++)if(!strcmp(line,wifiQueue[(wifiTail+j)%TLM_CAPACITY]))wifi=true;assert(wifi);
    if(step==10)assert(r.vector.values[0].reason==mulMultiScheduleMismatch);
    else if(step==11)assert(!r.vector.validMask);
    else if(step==12)assert(r.vector.values[0].reason==mulMultiMissing);
    else if((step==8||step==9||step>=17)&&policy)for(unsigned j=0;j+1<counts[trial];j++)assert(r.vector.values[j].status==4&&abs(r.vector.values[j].tdoaDtu-((j+1)%2?477:-477))<=3);
    else if(!policy)assert(!r.vector.validMask);
   }
   assert(found&&starts==transmissions);
   if(policy==3){
    // Finalizing a vector clears active; summaries must still run in idle slack.
    assert(!multiReceiver.active&&multiReceiver.hasCycle);
    mulRangingOperation_t op;assert(mulRangingPlanOperation(&active.plan,cycle,0,&op));
    tick=base+op.endUs/1000+2;humanLastSummary=tick-5000;
    command("cfg set verbosity normal");unsigned q=count,wq=wifiCount;size_t at=strlen(captured);
    mul_hybrid_radio_log_poll();assert(strstr(captured+at,"MULTI passive="));assert(count==q&&wifiCount==wq);
   }
  }
  command("off");command("plan use off");assert(!current.plan_selected&&!current.schedule.profile&&current.slot_ms==10);persisted();
 }
 if(trace)fclose(trace);
 puts("Planned RF5 codec2/3/4/5 actual DW3000 adapter: N2/4/8,100/200/1000ms, signed geometry/skews, partial/mismatch/capture age, wrap, full hash, essential queues, persisted selection and zero TX passed");return 0;
}
