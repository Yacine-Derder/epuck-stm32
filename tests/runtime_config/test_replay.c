#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
#define main portable_receiver_tests
#include "../../ThirdParty/mul_core/tests/test_multi_receiver.c"
#undef main
#define invalid replay_invalid
#include "../../ThirdParty/mul_core/mul_multi_receiver.c"
#undef invalid
static FILE *trace;
static unsigned snapshots;
static void drain_replay(void) {
 for(unsigned step=0;step<32;step++) {
  head=tail=count=wifiHead=wifiTail=wifiCount=0;
  mulReplayService(tick);
  for(unsigned i=0;i<wifiCount;i++)if(trace)fprintf(trace,"%s\n",wifiQueue[(wifiTail+i)%TLM_CAPACITY]);
 }
}
static size_t tdmaPacket(const mulSchedule_t *s,unsigned slot,unsigned step,uint32_t cycle,bool available,uint8_t *frame,uint64_t *rx) {
 int32_t folPpb=slot%2?35000:-45000,receiverPpb=15000;
 int64_t at=(int64_t)step*s->periodMs*MS,offset=slot*s->slotMs*MS;
 uint64_t reference=mulTimestampMask(MUL_TIMESTAMP_MASK-2000*MS+at);
 uint64_t followerOrigin=mulTimestampMask(500000+scale(at,folPpb));
 int32_t path=4000+(slot?differences[slot-1]:0);
 *rx=mulTimestampMask(MUL_TIMESTAMP_MASK-100000+scale(at+(slot?offset+120+path:path),receiverPpb));
 mulRelation_t relation={.version=2,.epoch=slot+10,.available=available,.referenceSequence=cycle,.twrSequence=cycle-3,
  .referenceOrigin=reference,.followerOrigin=followerOrigin,.skewPpb=folPpb,.originAgeMs=slot*s->slotMs+1,
  .delayAgeMs=3*s->periodMs+slot*s->slotMs,.delayDtu=120,.delayState=mulPropagationReused,.maxAgeMs=6000,.period10ms=s->periodMs/10};
 uint64_t tx=slot?mulTimestampMask(followerOrigin+scale(offset+120,folPpb)):reference;
 return mulPlanBroadcastEncode(frame,90,activePlan,slot,cycle,tx,slot?&relation:NULL);
}
int main(void) {
 bool tdma=getenv("MUL_REPLAY_TDMA")!=NULL;
 const char *path=getenv("MUL_REPLAY_TRACE");if(path){trace=fopen(path,"w");assert(trace);}
 memset(nv,255,sizeof nv);uwb_runtime_init();command("addr 141");command("mode hybrid_static");command("off");
 command("plan init");command("plan alrs 132 133");command("plan rlrs 141");command("plan set process_us 5000");command("plan use on");command("cfg set samples on");command("cfg set telemetry on");command("cfg set wifi on");if(getenv("MUL_REPLAY_KALMAN"))command("cfg set clock_estimator kalman");if(tdma){command("plan set lead_us 1000");command("plan set process_us 2000");command("plan set slot_ms 5");command("plan alrs 132 134 133");command("plan mode tdma");command("plan set twr_window_ms 90");command("plan set cycle_ms 200");}
 command("on");
 const char *verbosity=getenv("MUL_REPLAY_VERBOSITY");
 if(verbosity){assert(strlen(verbosity)==1&&verbosity[0]>='0'&&verbosity[0]<='2');
  char commandLine[32];snprintf(commandLine,sizeof(commandLine),"cfg set verbosity %s",verbosity);command(commandLine);
  assert(active.verbosity==(unsigned)(verbosity[0]-'0'));
 }
 assert(!tdma||(current.plan.policy==MUL_PLAN_TDMA&&current.plan.broadcast.count==3&&current.plan.broadcast.periodMs==200));
 activePlan=&active.plan;unsigned transmissions=starts;
 for(unsigned step=0;step<45;step++) {
  uint32_t base=UINT32_MAX-10000+step*multiReceiver.schedule.periodMs,cycle=UINT32_MAX-10+step;
  for(unsigned slot=0;slot<multiReceiver.schedule.count;slot++) {
   if((step==12&&slot)||(step==13&&!slot))continue;
   uint64_t rx;size_t n=(tdma?tdmaPacket:packet)(&multiReceiver.schedule,slot,step,cycle,step!=14,input,&rx);
   length=n+2;rxMarker=rx;tick=base+slot*multiReceiver.schedule.slotMs;status=DWT_INT_RXFCG_BIT_MASK;
   if(step==16&&!slot)mulClockReset(&multiReceiver.rate);
   head=tail=count=wifiHead=wifiTail=wifiCount=0;
   if(step==30&&slot)multiReceiver.rate.skewPpb=-15000;
   mul_hybrid_radio_process();
   for(unsigned i=0;i<wifiCount;i++)if(trace)fprintf(trace,"%s\n",wifiQueue[(wifiTail+i)%TLM_CAPACITY]);
  }
  uint8_t saved[MUL_REPLAY_BYTES];assert(mulReplayPack(saved,&multiReceiver,1,NULL));
  mulMultiReceiver_t decoded;mulRangingPlan_t plan;unsigned slot;uint32_t now,st[2];assert(mulReplayUnpack(saved,&decoded,&plan,&slot,&now,st));
  mulMultiValue_t expected=multiReceiver.values[0];evaluate(&decoded,1,now);
  assert(decoded.values[0].tdoaDtu==expected.tdoaDtu&&decoded.values[0].reason==expected.reason);
  tick=base+31;status=0;head=tail=count=wifiHead=wifiTail=wifiCount=0;mul_hybrid_radio_process();
  for(unsigned i=0;i<wifiCount;i++)if(trace)fprintf(trace,"%s\n",wifiQueue[(wifiTail+i)%TLM_CAPACITY]);
  snapshots++;drain_replay();
  if(tdma){tick=base+150;mul_hybrid_radio_log_poll();}
 }
 drain_replay();assert(starts==transmissions);command("off");

#if MUL_TDOA_REPLAY
 /* Snapshot pressure cannot block the radio owner or consume essential slots. */
 for(unsigned i=0;i<8;i++){multiReceiver.active=true;mulReplayPrepare(&multiReceiver);mulMultiCycle_t c;finish(&multiReceiver,&c);mulReplayComplete(&c,tick);}
 head=tail=count=wifiHead=wifiTail=wifiCount=0;mulReplayService(tick);assert(wifiCount==1);mulReplayService(tick);assert(wifiCount==2);mulReplayService(tick);assert(wifiCount==2);
 /* Expiry across uint32 uptime wrap cannot publish stale tail fragments, and
  * stale queued blobs cannot prevent admission of the next current cycle. */
 mulReplayReset();tick=UINT32_MAX-20;multiReceiver.active=true;mulReplayPrepare(&multiReceiver);
 mulMultiCycle_t wrapped;finish(&multiReceiver,&wrapped);mulReplayComplete(&wrapped,tick);
 head=tail=count=wifiHead=wifiTail=wifiCount=0;mulReplayService(tick+999);assert(wifiCount==1);
 mulReplayService(tick+1000);assert(wifiCount==1);
 multiReceiver.active=true;mulReplayPrepare(&multiReceiver);finish(&multiReceiver,&wrapped);mulReplayComplete(&wrapped,tick+1001);
 mulReplayService(tick+1001);assert(wifiCount==2);
#endif
 if(trace)fclose(trace);
 printf("Replay: %u radio-driven cycles, exact state roundtrip, partial observations, relation/rate unavailable, resets, signed rate, wrap, bounded queue and zero additional TX passed\n",snapshots);
}
