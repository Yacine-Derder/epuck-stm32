#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static void beacon_rx(mulBeacon_t *p,uint64_t timestamp) {
 assert(mulBeaconEncode(input,sizeof(input),p));length=58;rxMarker=timestamp;status=DWT_INT_RXFCG_BIT_MASK;step();
}
int main(void) {
 memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();command("CFG SET verbosity quiet");
 command("CFG SET telemetry on");command("CFG SET samples on");command("CFG SET mode beacon");
 tick+=1000;step();mulBeacon_t p;assert(mulBeaconDecode(output,56,&p)&&!p.markerValid&&beaconPending);
 uint32_t first=p.sequence;txMarker=MUL_TIMESTAMP_MASK-10;status=DWT_INT_TXFRS_BIT_MASK;step();
 tick+=1000;step();assert(mulBeaconDecode(output,56,&p)&&p.markerValid&&p.previousTx==txMarker&&p.previousSequence==first);
 uint32_t uncertain=p.sequence;tick+=100;step();assert(!beaconPending);tick+=1000;step();
 assert(mulBeaconDecode(output,56,&p)&&p.sequence>uncertain&&p.previousSequence==first); /* no predicted TX or uncertain reuse */
 command("CFG SET mode listen");
 uint64_t tx=MUL_TIMESTAMP_MASK-100000000000ULL,previous=0;uint32_t previousSeq=0;
 p=(mulBeacon_t){.source=0x22,.radio=1,.epoch=987,.sequence=1};
 for(unsigned i=0;i<20;i++) {
   captured[0]=0;head=tail=count=0;tick+=1000;
   tx=(tx+MUL_TIME_HZ)&MUL_TIMESTAMP_MASK;
   p.sequence=i+1;p.markerValid=i>0;p.previousTx=previous;p.previousSequence=previousSeq;
   uint64_t rx=(UINT64_C(50000000000)+(uint64_t)((i+1)*(double)MUL_TIME_HZ*1.000020))&MUL_TIMESTAMP_MASK;
   beacon_rx(&p,rx);
   if(i){bool found=false;for(unsigned j=0;j<count;j++){mulTelemetry_t r;assert(mulTelemetryDecode(telemetry[(tail+j)%TLM_CAPACITY],&r));if(r.type==mulTlmObservation){assert(r.transaction==i&&r.stamps[1]==previous&&r.remoteEpoch==987&&r.remoteRadio==1);found=true;}}assert(found);}
   previous=tx;previousSeq=p.sequence;
 }
 assert(beacons[0].model.valid&&fabs(beacons[0].model.rate-1.000020)<1e-10);
 /* Fresh packets without usable metadata must not keep an old model VALID. */
 for(unsigned i=0;i<6;i++){p.sequence++;p.markerValid=false;p.previousSequence=0;p.previousTx=0;tick+=1000;beacon_rx(&p,123+i);}
 assert(!beacons[0].model.valid);
 unsigned rejected=invalid;beacon_rx(&p,999);assert(invalid==rejected+1); /* duplicate */
 unsigned received=beaconRx;status=DWT_INT_RXFCE_BIT_MASK|DWT_INT_RXFCG_BIT_MASK;step();assert(beaconRx==received&&errors);
 p.epoch=988;p.sequence=1;p.markerValid=false;p.previousSequence=0;p.previousTx=0;
 beacon_rx(&p,123);assert(!beacons[0].model.valid&&beacons[0].epoch==988);
 p.sequence=2;p.markerValid=true;p.previousSequence=1;p.previousTx=100;tick+=6000;beacon_rx(&p,200);
 assert(!beacons[0].model.valid&&!beacons[0].model.seen); /* stale cached RX cannot train model */
 char line[320];assert(mul_ads_radio_sync_status(0,line,sizeof(line))&&strstr(line,"UNKNOWN")&&strstr(line,"offset_includes_propagation=1"));
 unsigned sequence=beaconSequence;command("DIAG RESET");assert(beaconSequence==sequence&&beaconRx==0&&errors==0);
 command("CFG SET mode off");assert(!started&&!mockRadioActive);
 puts("beacon/listen actual adapter: completed TX metadata, uncertain TX, drift/wrap, paired observation, CRC precedence, epoch/stale reset and safe OFF passed");return 0;
}
