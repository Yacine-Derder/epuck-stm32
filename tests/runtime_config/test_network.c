#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static void receive_network(const uint8_t *frame,unsigned bytes,uint64_t rx) {
 memcpy(input,frame,bytes);length=bytes+2;rxMarker=mulTimestampMask(rx);status=DWT_INT_RXFCG_BIT_MASK;
 head=tail=count=0;step();
}
static void exercise(uint8_t reference,uint8_t follower) {
 char cmd[64];command("cfg set mode off");
 snprintf(cmd,sizeof(cmd),"cfg set reference 0x%02X",reference);command(cmd);
 snprintf(cmd,sizeof(cmd),"cfg set follower 0x%02X",follower);command(cmd);
 command("cfg set mode hybrid");assert(active.mode==4&&active.reference==reference&&active.follower==follower);
 unsigned priorWrites=nvWrites;uwb_runtime_init();assert(nvWrites==priorWrites&&active.reference==reference&&active.follower==follower);
 assert(active.calibration&&active.correction_mm==-1234&&active.tx_ant_dtu==16380&&active.rx_ant_dtu==16380);
 mulNetwork_t network;assert(mulNetworkInit(&network,reference,follower));network.slotMs=(uint16_t)active.slot_ms;assert(!memcmp(&hybridReceiver.network,&network,sizeof(network)));
 uint8_t frame[84];uint64_t refTx=0,refRx=0;uint32_t base=UINT32_MAX-6000;
 for(unsigned i=0;i<11;i++) {
  refTx=mulTimestampMask(MUL_TIMESTAMP_MASK-2*1000*INT64_C(63897600)+i*1000*INT64_C(63897600));
  refRx=mulTimestampMask(MUL_TIMESTAMP_MASK-100000+i*1000*INT64_C(63897600));tick=base+i*1000;
  assert(mulEncodeScheduledPacket(frame,network.addresses[0],i,refTx,(uint16_t)active.slot_ms));receive_network(frame,34,refRx);
  mulTestPacket_t p={.sequence=500+i,.txTimestamp=500000+i*1000*INT64_C(63897600)+(active.slot_ms*UINT64_C(63897600))};memcpy(p.source,network.addresses[1],8);
  mulRelation_t r={.version=2,.available=true,.epoch=7,.referenceSequence=i,.twrSequence=i,
   .referenceOrigin=refTx,.followerOrigin=500000+i*1000*INT64_C(63897600),.originAgeMs=active.slot_ms,
   .delayAgeMs=active.slot_ms,.delayDtu=120,.delayState=mulPropagationFresh,.maxAgeMs=6000};
  p.slotMs=(uint16_t)active.slot_ms;mulRelationEncode(frame,&p,&r);tick+=active.slot_ms;receive_network(frame,74,refRx+(active.slot_ms*UINT64_C(63897600))+1000);
  if(i>=5){assert(hybridLastStatus==mulDownlinkNoRate&&hybridReceiver.pairs>0);}
 }
 assert(hybridReceiver.rate.status==mulClockValid&&hybridReceiver.rate.skewPpb==0);
 uint64_t stamps[6]={mulTimestampMask(refTx+650*INT64_C(63897600)),900000000000ULL,0,0,0,0};
 stamps[2]=mulTimestampMask(stamps[1]+20*INT64_C(63897600));stamps[3]=mulTimestampMask(stamps[0]+20*INT64_C(63897600)+200);
 stamps[4]=mulTimestampMask(stamps[3]+20*INT64_C(63897600));stamps[5]=mulTimestampMask(stamps[2]+20*INT64_C(63897600)+200);
 for(unsigned stage=0;stage<5;stage++) {
  mulTwrPacket_t p={.type=mulTwrPoll+stage,.id=10,.tof=stage==4?100:0};unsigned used=stage==0?1:stage==1?3:stage==2?5:6;
  memcpy(p.stamps,stamps,used*sizeof(uint64_t));p.txTimestamp=stage<3?stamps[used-1]:stage==3?mulTimestampMask(stamps[5]+20*INT64_C(63897600)):mulTimestampMask(stamps[4]+40*INT64_C(63897600)+200);
  assert(mulTwrEncodeNetwork(&network,frame,&p));tick=base+10650+stage*20;receive_network(frame,84,refRx+(650+stage*20)*INT64_C(63897600));
 }
 assert(hybridReceiver.twrCompleted==1&&hybridReceiver.twrRejected==0);
 /* A different advertised delay cannot reuse an acquired rate or pair. */
 unsigned rejected=hybridReceiver.rejected;
 assert(mulEncodeScheduledPacket(frame,network.addresses[0],11,refTx+INT64_C(63897600000),active.slot_ms==10?50:10));
 tick=base+11000;receive_network(frame,34,refRx+INT64_C(63897600000));
 assert(hybridReceiver.rejected==rejected+1&&hybridReceiver.diagnostics.reason[mulHybridScheduleMismatch]);
 assert(!hybridReceiver.rate.hasBaseline&&!hybridReceiver.association.pending);
 unsigned writes=nvWrites;command("cfg set reference 0x8d");command("cfg set follower 255");command("role ref");command("role follower");
 assert(writes==nvWrites&&active.reference==reference&&active.follower==follower&&active.role==2);
 command("cfg show");drain();assert(strstr(captured,"Legacy TDMA roster:"));persisted();
}
int main(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("cfg set address 0x8d");
 command("cfg set correction_mm -1234");command("cfg set calibration on");command("cfg set verbosity quiet");
 command("peer add 0x42");command("peer add 0x23");command("peer add 0x84");command("peer add 0x85");
 unsigned transmitted=starts;
 const unsigned slots[]={10,50,500};
 for(unsigned i=0;i<3;i++) {
  command("off");char cmd[40];snprintf(cmd,sizeof(cmd),"cfg set slot_ms %u",slots[i]);command(cmd);
  assert(active.slot_ms==slots[i]);exercise(0x42,0x23);exercise(0x85,0x84);
 }
 assert(starts==transmitted);
 puts("Persistent custom/swapped roster: legacy/current boot, calibration/peers retained, real RX/SYS_TIME/association/passive TWR, native40/uptime32 wrap, NO_RATE and no TX passed");
 return 0;
}
