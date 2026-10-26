#include <math.h>
#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static mulRangingPlan_t plan;
static mulClock_t receiverClock;
static unsigned operation;
static uint8_t responderReplyMs=5;
static const int64_t msDtu=63897600,flightDtu=2131;
static int32_t receiverPpb,initiatorPpb;
static bool deliveryMode,saturateResult,sawRange;
static unsigned rawCount,rawTxCount;
static mulTelemetry_t deliveredRange;
static FILE *trace;
static bool service(void) {
 bool handled=true;
 if(deliveryMode) {
  if(saturateResult&&(status&DWT_INT_RXFCG_BIT_MASK)&&input[24]==0x1c) {
   mulTelemetry_t filler={.type=mulTlmSummary};while(count<TLM_CAPACITY)uwb_runtime_publish(&filler);
  }
  mul_hybrid_radio_process();
 }else handled=mul_plan_radio_process(tick,status);
 if(deliveryMode) {
  for(unsigned i=0;i<count;i++) {
   const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));
   if(trace)fprintf(trace,"%s\n",line);
   if(r.type==mulTlmPlanRange){sawRange=true;deliveredRange=r;}
   if(r.type==mulTlmPlanRx){rawCount++;rawTxCount+=!!(r.flags&mulTlmTx);}
  }
  head=tail=count=wifiHead=wifiTail=wifiCount=0;
 }else {
  mulPlanRawRecord_t raw;while(mul_plan_radio_take_raw(&raw)){rawCount++;rawTxCount+=raw.tx;}
 }
 return handled;
}
static uint64_t stamp(int which,int64_t physical) {
 int32_t ppb=which?receiverPpb:initiatorPpb;
 uint64_t origin=which?MUL_TIMESTAMP_MASK-3*msDtu:UINT64_C(0x723456789a);
 return mulTimestampMask(origin+llroundl((long double)physical*(1.0L+ppb/1e9L)));
}
static int64_t physicalAt(int which,uint64_t time) {
 int32_t ppb=which?receiverPpb:initiatorPpb;
 return llroundl(mulTimestampDifference(time,stamp(which,0))/(1.0L+ppb/1e9L));
}
static void hardware(int64_t physical){planHardwareTime=stamp(1,physical);tick=(uint32_t)(5000+physical/msDtu);}
static void deliver(mulTwrPacket_t *p,int64_t physical) {
 assert(mulPlanTwrEncode(input,&plan,operation,p));length=92;rxMarker=stamp(1,physical);
 hardware(physical+msDtu/10);status|=DWT_INT_RXFCG_BIT_MASK;assert(service());
}
static mulTwrPacket_t transmitted(void){mulTwrPacket_t p;assert(mulPlanTwrDecode(&plan,5,operation,output,90,&p));return p;}
static mulTwrPacket_t prepareRange(int32_t ppb,int32_t senderPpb) {
 rawCount=rawTxCount=0;sawRange=false;
 mul_plan_radio_suspend();dwt_forcetrxoff();planClockOverride=false;
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 141");command("mode hybrid_static");command("off");
 receiverPpb=ppb;initiatorPpb=senderPpb;plan=(mulRangingPlan_t){.enabled=MUL_PLAN_AR,.replyMs=responderReplyMs,.maxUtilization=70,.arMilliHz=1000,.rlrCount=2,.rlrs={141,142}};
 mulScheduleLegacy(&plan.broadcast,32,33,10);mulScheduleExperimental(&plan.broadcast);plan.broadcast.rangingFlags=0;
 plan.broadcast.txLeadUs=500;plan.broadcast.rxBudgetUs=2000;plan.broadcast.airtimeUs=400;
 if(deliveryMode){plan.enabled|=MUL_PLAN_AA;plan.aaMilliHz=1000;}
 assert(!mulRangingPlanValidate(&plan,NULL));mulClockReset(&receiverClock);
 for(unsigned i=0;i<6;i++) {
  int64_t physical=((int64_t)i-5)*1000*msDtu;mulTestPacket_t ref={.sequence=i,.txTimestamp=mulTimestampMask(900000+physical)};
  const uint8_t address[8]={32,0,0,0,0,0,0xcf,0xbc};memcpy(ref.source,address,8);
  mulNetwork_t n;assert(mulNetworkInit(&n,32,33));
  mulClockObserveNetwork(&n,&receiverClock,&ref,stamp(1,physical+flightDtu),i*1000,true);
 }
 assert(receiverClock.status==mulClockValid);mul_plan_radio_configure(&plan,141,&receiverClock,true);mul_plan_radio_reference(5,1);
 operation=99;mulRangingOperation_t op;
 for(unsigned i=0;i<mulRangingPlanCount(&plan,5);i++){assert(mulRangingPlanOperation(&plan,5,i,&op));if(op.initiator==33&&op.responder==141)operation=i;}
 assert(operation!=99&&mulRangingPlanOperation(&plan,5,operation,&op));
 dwt_settxantennadelay((uint16_t)current.tx_ant_dtu);dwt_setrxantennadelay((uint16_t)current.rx_ant_dtu);
 planClockOverride=true;planSetupLag=0;startResult=rxResult=writeResult=DWT_SUCCESS;badAnt=false;status=0;outputLength=0;
 if(deliveryMode) {
  uwb_config_t candidate=current;
  assert(!uwb_config_plan_set(&candidate,&plan,true));
  candidate.telemetry=candidate.wifi=candidate.samples=candidate.rf_enabled=1;
  assert(update(&candidate)&&started);persisted();
  unsigned writes=nvWrites;uwb_runtime_init();assert(started&&nvWrites==writes&&current.plan_selected);
  multiReceiver.rate=receiverClock;mul_plan_radio_reference(5,1);
  hybridLastReport=tick; // Keep health publication separate from measurement checks.
  head=tail=count=wifiHead=wifiTail=wifiCount=0;
 }
 int64_t poll=(int64_t)op.pollUs*msDtu/1000;
 mulTwrPacket_t p={.type=mulTwrPoll,.id=5,.txTimestamp=stamp(0,poll)};p.stamps[0]=p.txTimestamp;return p;
}
static void completion(bool coalesced,int32_t rlrRate,int32_t alrRate,uint8_t reply) {
 responderReplyMs=reply;mulTwrPacket_t p=prepareRange(rlrRate,alrRate);unsigned sent=starts;
 int64_t poll=physicalAt(0,p.stamps[0]);deliver(&p,poll+flightDtu);assert(mul_plan_radio_busy()&&starts==sent+1);
 assert(!mul_plan_radio_reset_stats());
 p=transmitted();assert(p.type==mulTwrResponse&&p.power[0].status==mulPowerUnsupported);
 int64_t response=physicalAt(1,p.txTimestamp);uint64_t responseStamp=p.txTimestamp;
 assert((delayedRegister&1)==0&&mulTimestampDifference(responseStamp,((uint64_t)delayedRegister<<8)+txAnt)==0);
 if(!coalesced){hardware(response+msDtu/10);txMarker=responseStamp;status=DWT_INT_TXFRS_BIT_MASK;assert(service());}
 p.type=mulTwrFinal;p.stamps[3]=stamp(0,response+flightDtu);p.stamps[4]=mulTimestampMask(p.stamps[3]+plan.replyMs*msDtu);p.txTimestamp=p.stamps[4];
 if(coalesced){txMarker=responseStamp;status=DWT_INT_TXFRS_BIT_MASK;}
 deliver(&p,physicalAt(0,p.txTimestamp)+flightDtu);assert(starts==sent+2&&mul_plan_radio_busy());
 p=transmitted();assert(p.type==mulTwrReport&&p.power[2].status==mulPowerUnsupported);int64_t report=physicalAt(1,p.txTimestamp);
 hardware(report+msDtu/10);txMarker=p.txTimestamp;status=DWT_INT_TXFRS_BIT_MASK;assert(service());
 p.type=mulTwrResult;assert(mulTwrCalculateReply(p.stamps,&p.tof,plan.replyMs));p.txTimestamp=mulTimestampMask(stamp(0,report+flightDtu)+plan.replyMs*msDtu);
 deliver(&p,physicalAt(0,p.txTimestamp)+flightDtu);assert(!mul_plan_radio_busy());
 mul_plan_radio_reference(6,1); // A new reference must not re-label a committed result.
 if(deliveryMode) {
  assert(sawRange&&deliveredRange.type==mulTlmPlanRange&&deliveredRange.node==141&&deliveredRange.peer==33);
  assert(deliveredRange.planRange.cycle==5&&deliveredRange.planRange.rawTof>=flightDtu-1&&deliveredRange.planRange.rawTof<=flightDtu+1);
  assert(deliveredRange.uptimeMs<deliveredRange.planRange.reportedMs-15&&deliveredRange.planRange.unknownComponents==255);
  assert(rawTxCount==2&&rawCount==(saturateResult?4u:5u));
  mulTelemetry_t q;assert(mul_plan_radio_quality(&q));uwb_runtime_publish(&q);mulTelemetry_t decoded;assert(mulTelemetryDecode(telemetry[tail],&decoded)&&decoded.type==mulTlmPlanQuality&&decoded.completed==1);
  if(trace)fprintf(trace,"%s\n",telemetry[tail]);
  planClockOverride=false;return;
 }
 assert(rawCount==5&&rawTxCount==2);
 mulPlanRangeResult_t result;assert(mul_plan_radio_take_result(&result)&&!mul_plan_radio_take_result(&result));
 assert(result.cycle==5&&result.operation==operation&&result.initiator==33&&result.responder==141&&result.fingerprint==mulRangingPlanFingerprint(&plan));
 assert(abs((int)result.rawTof-(int)flightDtu)<=1&&result.tof==result.rawTof&&!memcmp(result.stamps,p.stamps,sizeof(p.stamps)));
 assert(result.measuredMs<tick-15&&starts==sent+2);planClockOverride=false;
}
int main(void) {
 const char *path=getenv("MUL_AR_TRACE");if(path){trace=fopen(path,"w");assert(trace);}
 const uint8_t replies[]={5,10,20};
 for(unsigned d=0;d<3;d++) {
  completion(false,-150000,35000,replies[d]);completion(true,-150000,35000,replies[d]);
  completion(false,150000,-45000,replies[d]);completion(true,150000,-45000,replies[d]);
 }
 deliveryMode=true;completion(false,150000,-45000,5);completion(true,-150000,35000,20);
 saturateResult=true;completion(true,150000,-45000,10);saturateResult=false;deliveryMode=false;
 responderReplyMs=5;
 const int32_t rates[]={150000,-150000};
 for(unsigned i=0;i<2;i++) {
  mulTwrPacket_t p=prepareRange(rates[i],-rates[i]);int64_t poll=physicalAt(0,p.stamps[0]);unsigned sent=starts;
  planSetupLag=5*msDtu;deliver(&p,poll+flightDtu);assert(!mul_plan_radio_busy()&&starts==sent);planSetupLag=0;
  assert(mul_plan_radio_reset_stats());mulTelemetry_t quality;assert(mul_plan_radio_quality(&quality)&&quality.planQuality.reset==1);
  deliver(&p,poll+flightDtu);assert(starts==sent); // Counter reset cannot revive a retired operation.
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);sent=starts;deliver(&p,poll+flightDtu);assert(starts==sent+1);
  mulPlanRangeResult_t result;txMarker=mulTimestampMask(transmitted().txTimestamp+1);hardware(poll+6*msDtu);status=DWT_INT_TXFRS_BIT_MASK;
  assert(service()&&!mul_plan_radio_busy()&&!mul_plan_radio_take_result(&result));
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);deliver(&p,poll+flightDtu);
  hardware(poll+15*msDtu);status=0;assert(service()&&!mul_plan_radio_busy()); // Lost TX IRQ/deadline.
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);deliver(&p,poll+flightDtu);
  txMarker=transmitted().txTimestamp;hardware(physicalAt(1,txMarker)+msDtu/10);status=DWT_INT_TXFRS_BIT_MASK|DWT_INT_RXFCG_BIT_MASK;length=42;
  assert(!service()&&!mul_plan_radio_busy()&&(status&DWT_INT_RXFCG_BIT_MASK)); // Broadcast frozen for owner.
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);deliver(&p,poll+flightDtu);
  hardware(poll+50*msDtu);status=DWT_INT_RXFCG_BIT_MASK;length=42;
  assert(!service()&&!mul_plan_radio_busy()&&(status&DWT_INT_RXFCG_BIT_MASK)); // Expired range cannot discard the next broadcast.
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);sent=starts;
  assert(mulPlanTwrEncode(input,&plan,operation,&p));input[86]^=1;length=92;rxMarker=stamp(1,poll+flightDtu);hardware(poll+msDtu/10);status=DWT_INT_RXFCG_BIT_MASK;
  assert(service()&&!mul_plan_radio_busy()&&starts==sent);
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);sent=starts;
  unsigned own=operation;operation=own+1;deliver(&p,poll+flightDtu);operation=own;
  assert(starts==sent&&!mul_plan_radio_busy());deliver(&p,poll+flightDtu);assert(starts==sent+1); // A different RLR cannot consume this assignment.
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);sent=starts;
  hardware(poll);status=DWT_INT_TXFRS_BIT_MASK;assert(service()&&!mul_plan_radio_busy()&&!(status&DWT_INT_TXFRS_BIT_MASK)&&starts==sent);
  p=prepareRange(rates[i],-rates[i]);poll=physicalAt(0,p.stamps[0]);rxResult=DWT_ERROR;startResult=DWT_ERROR;deliver(&p,poll+flightDtu);
  assert(!mul_plan_radio_busy()&&!mockRadioActive);rxResult=startResult=DWT_SUCCESS;status=0;service();assert(mockRadioActive);
 }
 planClockOverride=false;if(trace)fclose(trace);
 puts("DW3000 planned responder: five messages, independent initiator/reference/RLR clocks, raw ToF, delayed512/antenna markers, coalesced IRQ, commit identity, no-retry, late SPI/stamp/deadline/hash/broadcast and rearm recovery passed");return 0;
}
