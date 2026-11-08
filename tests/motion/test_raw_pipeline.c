/* Real radio/runtime/queues. 921600-baud UART, no devices or network endpoints. */
#define MUL_REPLAY_REUSE
#include "../runtime_config/test_replay.c"
static FILE *wire,*expected;
static char pendingLine[MUL_TLM_LINE];
static unsigned wireRemaining,credit,baud=921600,delivered[16],encoded[16],encodeErrors;
bool __real_mulTelemetryEncode(char *,size_t,const mulTelemetry_t *);
bool __wrap_mulTelemetryEncode(char *line,size_t capacity,const mulTelemetry_t *r) {
 bool ok=__real_mulTelemetryEncode(line,capacity,r);if(!ok)encodeErrors++;else encoded[r->type]++;
 return ok;
}
uint8_t esp_link_telemetry(const char *line) {
 if(wireRemaining)return 0;
 memcpy(pendingLine,line,sizeof pendingLine);wireRemaining=(unsigned)strlen(line)+1;
 return 1;
}
static void uart(void) {
 credit+=baud;
 unsigned bytes=credit/10000;credit%=10000;
 if(wireRemaining) {
  if(bytes>=wireRemaining) {
   mulTelemetry_t record;
   if(mulTelemetryDecode(pendingLine,&record))delivered[record.type]++;
   else {assert(!strncmp(pendingLine,"UWB3 ",5));delivered[15]++;}
   if(wire)fprintf(wire,"%lu\t%s\n",(unsigned long)tick,pendingLine);
   wireRemaining=0;
  }else wireRemaining-=bytes;
 }
}

static uint64_t receiverStamp(int64_t physical) {
 return mulTimestampMask(MUL_TIMESTAMP_MASK-100000+scale(physical,15000));
}
static int64_t physicalStamp(uint64_t local,int64_t base) {
 return base+llroundl(mulTimestampDifference(local,receiverStamp(base))/(1.0L+15000/1e9L));
}
int main(void) {
 const char *path=getenv("MUL_RAW_UART");if(path){wire=fopen(path,"w");assert(wire);}
 path=getenv("MUL_RAW_EXPECTED");if(path){expected=fopen(path,"w");assert(expected);}
 path=getenv("MUL_RAW_BAUD");if(path)baud=(unsigned)strtoul(path,NULL,10);
 memset(nv,255,sizeof nv);uwb_runtime_init();command("addr 141");command("mode hybrid_static");command("off");
 command("plan init tdma");command("plan set lead_us 1000");command("plan set process_us 2000");command("plan set slot_ms 5");
 command("plan set reply_ms 5");command("plan set twr_window_ms 30");command("plan set cycle_ms 100");
 command("plan alrs 132 134 133 135");command("plan rlrs 141");command("plan set aa on");command("plan set ar on");command("plan set ff on");command("plan use on");
 command("cfg set samples on");command("cfg set telemetry on");command("cfg set wifi on");command("cfg set clock_estimator kalman");command("on");
 assert(mulRangingPlanFingerprint(&active.plan)==UINT32_C(3962260266));activePlan=&active.plan;
 unsigned tx=starts,receptions=0,corrected=0,missing=0,cycles=120,arCompleted=0,arRx=0;
 for(unsigned step=0;step<cycles;step++) {
  uint32_t cycle=UINT32_MAX-10+step,base=10000+step*100;
  mulRangingOperation_t operation;bool haveAr=mulRangingPlanOperation(activePlan,cycle,0,&operation)&&operation.kind==MUL_PLAN_AR;
  mulTwrPacket_t arPacket={0};unsigned arStage=0;int64_t nextRx=0;uint64_t pendingTx=0;
  for(unsigned ms=0;ms<100;ms++) {
   int64_t physical=(int64_t)step*100*MS+ms*MS;
   tick=base+ms;status=0;planClockOverride=true;planHardwareTime=receiverStamp(physical+50000);
   if(haveAr&&step>=6) {
    if(!arStage&&ms==(operation.pollUs+999)/1000) {
     arPacket=(mulTwrPacket_t){.type=mulTwrPoll,.id=cycle,.txTimestamp=mulTimestampMask(1000000+physical),.stamps={mulTimestampMask(1000000+physical)}};
     nextRx=physical+2131;arStage=1;
    }
    if(pendingTx&&mulTimestampDifference(planHardwareTime,pendingTx)>=0) {
     txMarker=pendingTx;pendingTx=0;status|=DWT_INT_TXFRS_BIT_MASK;
    }
    if(nextRx&&physical+50000>=nextRx) {
     assert(mulPlanTwrEncode(input,activePlan,0,&arPacket));length=92;rxMarker=receiverStamp(nextRx);
     status|=DWT_INT_RXFCG_BIT_MASK;nextRx=0;arRx++;
     if(arStage==5){arCompleted++;arStage=6;}
    }
   }
   if(ms<=15&&ms%5==0) {
    unsigned slot=ms/5;
    if(step==17&&slot==2)missing++;
    else {
     uint64_t rx;size_t n=tdmaPacket(&multiReceiver.schedule,slot,step,cycle,true,input,&rx);assert(n);
     length=n+2;rxMarker=rx;status=DWT_INT_RXFCG_BIT_MASK;receptions++;
     if(expected)fprintf(expected,"%lu %u %u %llu %llu\n",(unsigned long)cycle,slot,input[13],(unsigned long long)mulDecodeTimestamp(input+29),(unsigned long long)rx);
    }
   }
   unsigned previousStarts=starts;
   uwb_runtime_task();uart();
   if(starts!=previousStarts) {
    assert(haveAr&&starts==previousStarts+1);
    assert(mulPlanTwrDecode(activePlan,cycle,0,output,90,&arPacket));pendingTx=arPacket.txTimestamp;
    int64_t at=physicalStamp(arPacket.txTimestamp,(int64_t)step*100*MS);
    if(arPacket.type==mulTwrResponse) {
     arPacket.type=mulTwrFinal;
     arPacket.stamps[3]=mulTimestampMask(1000000+at+2131);
     arPacket.stamps[4]=mulTimestampMask(arPacket.stamps[3]+5*MS);arPacket.txTimestamp=arPacket.stamps[4];
     arPacket.power[1]=(mulPower_t){.powerCentiDbm=INT16_MIN,.status=mulPowerUnsupported};
     nextRx=at+2*2131+5*MS;arStage=3;
    }else {
     assert(arPacket.type==mulTwrReport);arPacket.type=mulTwrResult;
     assert(mulTwrCalculateReply(arPacket.stamps,&arPacket.tof,5));
     arPacket.txTimestamp=mulTimestampMask(1000000+at+2131+5*MS);nextRx=at+2*2131+5*MS;arStage=5;
    }
   }
   if(ms==36) {
    assert(multiReceiver.cycle==cycle&&!multiReceiver.active);
    for(unsigned k=0;k<3;k++) {
     corrected+=multiReceiver.values[k].status==mulDownlinkAvailable;
     if(MUL_RX_MOTION_GUARD)assert(multiReceiver.values[k].tdoaDtu==INT32_MIN);
    }
   }
  }
 }
 for(unsigned ms=0;ms<1000;ms++){tick++;status=0;uwb_runtime_task();uart();}
 assert(receptions==cycles*4-1&&multiReceiver.received==receptions&&starts==tx+2*arCompleted&&!encodeErrors);
 if(baud==921600){assert(delivered[mulTlmPlanRx]==receptions+5*arCompleted);assert(delivered[mulTlmPlanRange]==arCompleted);}
 assert(MUL_RX_MOTION_GUARD?!corrected:corrected>300);
 assert(arCompleted>30&&arRx==3*arCompleted);
 if(wire)fclose(wire);if(expected)fclose(expected);
 fprintf(stderr,"AR completed=%u received=%u raw_tx=%u ranges_delivered=%u\n",arCompleted,arRx,2*arCompleted,delivered[mulTlmPlanRange]);
 printf("{\"guard\":%u,\"cycles\":%u,\"rf_broadcast_accepted\":%u,\"rf_twr_received\":%u,\"raw_twr_tx\":%u,\"twr_completed\":%u,\"missing_rf\":%u,\"raw_encoded\":%u,\"raw_delivered\":%u,\"vectors_delivered\":%u,\"corrected_components\":%u,\"clock_delivered\":%u,\"wifi_drops\":%lu,\"summary_not_delivered\":%u,\"quality_not_delivered\":%u,\"usb_drops\":%lu,\"encode_errors\":%u,\"baud\":%u}\n",
 MUL_RX_MOTION_GUARD,cycles,receptions,arRx,2*arCompleted,arCompleted,missing,encoded[mulTlmPlanRx],delivered[mulTlmPlanRx],delivered[mulTlmPlanVector],corrected,delivered[mulTlmClock],(unsigned long)wifiDrops,encoded[1]-delivered[1],encoded[14]-delivered[14],(unsigned long)dropped,encodeErrors,baud);
}
