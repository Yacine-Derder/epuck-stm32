/* Real radio adapter + durable console + shared core: deterministic fault campaign. */
#include <stdlib.h>
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
static uint64_t base;
static unsigned successes[3];
static void complete(bool reachable) {
  mulAdsPacket_t p=sentPacket();assert(p.type==mulAdsPoll);
  uint32_t transaction=p.transaction;uint8_t peer=p.destination;
  if(!reachable)return;
  base=(base+10000000)&MUL_TIMESTAMP_MASK;
  txMarker=base;status=DWT_INT_TXFRS_BIT_MASK;step();
  p=(mulAdsPacket_t){.type=mulAdsResponse,.source=peer,.destination=(uint8_t)active.node,.transaction=transaction,.session=state.session};
  mulAdsPacket_t wrong=p;wrong.source=peer==2?4:2;
  unsigned rejects=state.rejected;receive(&wrong,(base+1000200)&MUL_TIMESTAMP_MASK);
  assert(state.rejected==rejects+1&&state.stage==adsResponseRx);
  receive(&p,(base+1000200)&MUL_TIMESTAMP_MASK);assert(sentPacket().type==mulAdsFinal);
  txMarker=(base+2000200)&MUL_TIMESTAMP_MASK;status=DWT_INT_TXFRS_BIT_MASK;step();
  p.type=mulAdsReport;p.stamps[0]=(base+5000100)&MUL_TIMESTAMP_MASK;
  p.stamps[1]=(base+6000100)&MUL_TIMESTAMP_MASK;p.stamps[2]=(base+7000300)&MUL_TIMESTAMP_MASK;
  unsigned done=state.completed;receive(&p,(base+3000000)&MUL_TIMESTAMP_MASK);
  assert(state.stage==adsIdle&&state.completed==done+1&&state.rangeValid);
  assert(fabs(state.tof-100)<0.0001);successes[peer-2]++;
  receive(&p,(base+3000001)&MUL_TIMESTAMP_MASK);assert(state.completed==done+1); /* duplicate */
}
int main(void) {
  memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();
  command("CFG SET address 0x10");command("CFG SET peer 2");
  command("PEER ADD 3");command("PEER ADD 4");command("CFG SET role initiator");
  command("CFG SET interval_ms 250");command("CFG SET verbosity quiet");command("CFG SET wifi on");command("CFG SET telemetry on");command("CFG SET samples on");command("CFG SET mode twr");
  assert(config.peer_count==3);persisted();base=MUL_TIMESTAMP_MASK-10000000;
  for(unsigned ms=0;ms<120000;ms++) {
    captured[0]=0;step();
    if(state.stage==adsPollTx&&state.pending)complete(state.peer!=3||ms>60000);
  }
  assert(count<=TLM_CAPACITY&&wifiCount<=TLM_CAPACITY&&dropped>0&&wifiDrops>0); /* disconnected collector cannot block ranging */
  assert(successes[0]>200&&successes[2]>200&&successes[1]>100);
  assert(peerStates[1].aborted>100&&peerTimeouts[1]>100);
  assert(abs((int)successes[0]-(int)successes[2])<=2); /* unavailable peer cannot starve healthy nodes */
  command("CFG SET mode off");command("PEER DISABLE 3");command("PEER INTERVAL 4 500");persisted();
  unsigned writes=nvWrites;command("PEER ADD 4");command("PEER ADD 0x10");command("PEER ADD 256");assert(nvWrites==writes);
  for(unsigned i=5;i<=9;i++){char cmd[32];snprintf(cmd,sizeof(cmd),"PEER ADD %u",i);command(cmd);}
  assert(current.peer_count==8);writes=nvWrites;command("PEER ADD 10");assert(nvWrites==writes);
  uwb_config_t before=current;uwb_runtime_init();assert(uwb_config_equal(&before,&active));
  command("PEER REMOVE 2");assert(active.peer==3&&active.calibration==0);persisted();
  /* Responder idle routing, independent per-peer anti-duplicate/session state. */
  command("CFG SET role responder");command("PEER ENABLE 3");command("CFG SET mode twr");
  mulAdsPacket_t p={.source=3,.destination=0x10,.type=mulAdsPoll,.session=900,.transaction=1};receive(&p,100);
  assert(state.peer==3&&state.pending);p.source=4;receive(&p,200);assert(state.peer==3&&state.rejected);
  tick+=101;step();receive(&p,300);assert(state.peer==4&&state.pending);
  txMarker=400;status=DWT_INT_TXFRS_BIT_MASK;step();p.type=mulAdsFinal;receive(&p,500);status=DWT_INT_TXFRS_BIT_MASK;step();
  unsigned countBefore=state.completed;p.type=mulAdsPoll;receive(&p,600);assert(state.stage==adsIdle&&state.completed==countBefore);
  tick=UINT32_MAX-200;config.role=1;assert(mul_ads_radio_init()==DWT_SUCCESS);
  tick+=251;step();assert(state.pending&&state.stage==adsPollTx); /* host tick wrap, too */
  puts("multi-peer real adapter: 120s loss/rejoin, fairness, wrap, duplicate/cross-peer isolation, durable commands and responder routing passed");return 0;
}
