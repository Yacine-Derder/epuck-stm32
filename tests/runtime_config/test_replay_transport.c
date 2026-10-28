/* Real radio/runtime/queues, with bounded UART pending slot and 27ms wire time.
 * Run with MUL_REPLAY_TRACE; never accesses devices or wall-clock sockets. */
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
static bool connected,pending;
static char pendingLine[MUL_TLM_LINE];
static uint32_t busyUntil,firstSequence=UINT32_MAX,lastSequence;
static unsigned received,encodeErrors;
static uint8_t types[20000],seen[20000];
bool __real_mulTelemetryEncode(char *,size_t,const mulTelemetry_t *);
bool __wrap_mulTelemetryEncode(char *line,size_t capacity,const mulTelemetry_t *r) {
 assert(r->sequence<sizeof(types));types[r->sequence]=r->type;
 bool ok=__real_mulTelemetryEncode(line,capacity,r);if(!ok)encodeErrors++;
 return ok;
}
uint8_t esp_link_telemetry(const char *line) {
 if(!connected||pending)return 0;
 memcpy(pendingLine,line,sizeof pendingLine);pending=true;return 1;
}
static void uartService(void) {
 if(pending&&(int32_t)(tick-busyUntil)>=0) {
  uint32_t seq=0;for(unsigned i=0;i<4;i++){unsigned b;assert(sscanf(pendingLine+29+2*i,"%2x",&b)==1);seq|=b<<(8*i);}
  assert(seq<sizeof(seen));seen[seq]=1;if(firstSequence==UINT32_MAX)firstSequence=seq;lastSequence=seq;received++;
  if(pendingLine[11]=='0'&&pendingLine[12]=='F') {
   uint32_t cycle=0;for(unsigned i=0;i<4;i++){unsigned b;assert(sscanf(pendingLine+69+2*i,"%2x",&b)==1);cycle|=b<<(8*i);}
   assert((uint32_t)(tick-(10000+(cycle-1)*1000))<1300); // No disconnected-era backlog.
  }
  if(trace)fprintf(trace,"%s\n",pendingLine);
  pending=false;busyUntil=tick+27;
 }
}
int main(void) {
 const char *path=getenv("MUL_REPLAY_TRACE");if(path){trace=fopen(path,"w");assert(trace);}
 memset(nv,255,sizeof nv);uwb_runtime_init();command("addr 141");command("mode hybrid_static");command("off");
 command("plan init");command("plan alrs 132 133");command("plan rlrs 141");command("plan set process_us 5000");command("plan set lead_us 1000");command("plan set revision 23");command("plan set aa_millihz 1000");command("plan set ar_millihz 50");command("plan set aa on");command("plan set ar on");command("plan use on");command("cfg set samples on");command("cfg set telemetry on");command("cfg set wifi on");command("on");
 assert(mulRangingPlanFingerprint(&active.plan)==0x826414ccu);
 activePlan=&active.plan;unsigned transmissions=starts;
 const char *phase=getenv("MUL_REPLAY_PHASE_MS");if(phase)hybridLastReport=9000+(unsigned)atoi(phase);
 for(unsigned step=0;step<200;step++) {
  uint32_t cycle=step+1,base=10000+step*1000;unsigned aa=99;mulRangingOperation_t op;
  for(unsigned i=0;i<mulRangingPlanCount(&active.plan,cycle);i++)if(mulRangingPlanOperation(&active.plan,cycle,i,&op)&&op.kind==MUL_PLAN_AA){aa=i;break;}
  for(unsigned ms=0;ms<1000;ms++) {
   tick=base+ms;status=0;connected=step>=60;
   if(ms==0||ms==10) {
    unsigned slot=ms==10;uint64_t rx;size_t n=packet(&multiReceiver.schedule,slot,step,cycle,true,input,&rx);length=n+2;rxMarker=rx;status=DWT_INT_RXFCG_BIT_MASK;
   }else if(aa!=99&&ms>=op.pollUs/1000&&(ms-op.pollUs/1000)%20==0&&(ms-op.pollUs/1000)/20<5) {
    unsigned stage=(ms-op.pollUs/1000)/20;
    mulTwrPacket_t p={.type=mulTwrPoll+stage,.id=cycle,.txTimestamp=1000000+stage*20*MS};
    unsigned used=stage==0?1:stage==1?3:stage==2?5:6;
    for(unsigned j=0;j<used;j++)p.stamps[j]=1000000+(j/2)*20*MS+j;
    assert(mulPlanTwrEncode(input,&active.plan,aa,&p));length=92;rxMarker=mulTimestampMask(multiReceiver.sources[0].source.rxTimestamp+(int64_t)ms*MS);status=DWT_INT_RXFCG_BIT_MASK;
   }
   uwb_runtime_task();uartService();
  }
 }
 for(unsigned i=0;i<400;i++){tick++;status=0;uwb_runtime_task();uartService();}
 assert(starts==transmissions);assert(!encodeErrors);
 unsigned missing=0,missingByType[16]={0};
 for(unsigned i=firstSequence;i<=lastSequence;i++)if(!seen[i]){missing++;missingByType[types[i]]++;}
 printf("paced UART: received=%u gaps=%u wifi_queue_drops=%lu usb_queue_drops=%lu encode_errors=%u\n",received,missing,(unsigned long)wifiDrops,(unsigned long)dropped,encodeErrors);
 for(unsigned i=0;i<16;i++)if(missingByType[i])printf("missing type=%u count=%u\n",i,missingByType[i]);
 assert(!missingByType[0]&&!missingByType[15]&&!missingByType[mulTlmPlanVector]);
 if(trace)fclose(trace);
}
