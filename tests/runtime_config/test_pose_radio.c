#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static FILE *trace;
static unsigned rawCount,externalAvailable,externalMissing,noSource;
static void records(void) {
 for(unsigned i=0;i<count;i++) {
  const char *line=telemetry[(tail+i)%TLM_CAPACITY];mulTelemetry_t r;assert(mulTelemetryDecode(line,&r));
  if(trace)fprintf(trace,"%s\n",line);
  if(r.type==mulTlmMultiRx)rawCount++;
  if(r.type==mulTlmVector&&r.vector.sourceVersion) {
   const mulMultiValue_t *v=&r.vector.values[0];
   if(r.vector.cycle==6){assert(v->source==2&&v->status==4&&abs(v->tdoaDtu+2131)<=3);externalAvailable++;}
   else if(r.vector.cycle==7){assert(v->source==3&&v->status==0&&v->tdoaDtu==INT32_MIN);noSource++;}
   else {assert(r.vector.cycle==8&&v->source==2&&v->status==0&&v->tdoaDtu==INT32_MIN);externalMissing++;}
  }
 }
 head=tail=count=wifiHead=wifiTail=wifiCount=0;
}
int main(void) {
 const char *path=getenv("MUL_POSE_FRAMES");assert(path);FILE *frames=fopen(path,"r");assert(frames);
 path=getenv("MUL_POSE_TLM_TRACE");if(path){trace=fopen(path,"w");assert(trace);}
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 141");command("mode hybrid_static");command("off");
 command("cfg roster set 132 135");command("cfg set profile multi");command("cfg set schedule_revision 9");
 command("cfg set telemetry on");command("cfg set wifi on");command("cfg set samples on");command("on");persisted();
 assert(started&&active.schedule.profile==MUL_PROFILE_MULTI&&multiReceiver.schedule.count==2);
 unsigned transmissions=starts,rows=0,n;unsigned long observed;unsigned long long marker;char hex[181];
 while(fscanf(frames,"%u %lu %llx %180s",&n,&observed,&marker,hex)==4) {
  assert((n==40||n==80)&&strlen(hex)==2*n&&observed<=UINT32_MAX);
  for(unsigned i=0;i<n;i++){unsigned byte;assert(sscanf(hex+2*i,"%2x",&byte)==1);input[i]=(uint8_t)byte;}
  length=n+2;rxMarker=marker;tick=(uint32_t)observed;status=DWT_INT_RXFCG_BIT_MASK;mul_hybrid_radio_process();records();rows++;
  if(n==80){tick+=40;status=0;mul_hybrid_radio_process();records();}
 }
 assert(feof(frames)&&rows==11&&rawCount==11&&externalAvailable==1&&noSource==1&&externalMissing==1&&starts==transmissions);
 fclose(frames);if(trace)fclose(trace);
 puts("Actual LPS external/none/stale pose radio frames through persisted DW3000 receiver, independent RLR clock, geometric TDoA, explicit provenance, raw bytes and zero TX passed");return 0;
}
