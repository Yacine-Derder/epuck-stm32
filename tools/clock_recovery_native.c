/* Host comparison: identical captured observations, original follower relation fixed. */
#include "mul_replay.h"
#include <stdio.h>
#include <stdlib.h>
#include "mul_multi_receiver.c"
int main(int argc,char **argv){
 mulClock_t clock;mulClockReset(&clock);mulClockFit_t fit;mulClockFitReset(&fit);
 uint8_t blob[MUL_REPLAY_BYTES];
 while(fread(blob,1,sizeof(blob),stdin)==sizeof(blob)){
  mulMultiReceiver_t s;mulRangingPlan_t plan;unsigned slot;uint32_t now,status[2];
  if(!mulReplayUnpack(blob,&s,&plan,&slot,&now,status))return 2;
  mulClockFitConfig_t c=s.clockConfig;c.estimator=argc>1?atoi(argv[1]):2;
  mulObservation_t *o=&s.sources[0];
  if((s.seenMask&1)&&s.captureAge[0]<=15&&o->valid&&o->source.sequence==s.cycle&&
     (!clock.hasBaseline||o->source.sequence!=clock.sequence)){
   if(clock.hasBaseline&&(uint32_t)(o->acceptedMs-clock.observedMs)>=MUL_SOURCE_TIMEOUT_MS
#ifdef RECOVERY_BUILD
      &&!mulClockFitHoldover(&clock,&fit,&c,o->acceptedMs)
#endif
      )mulClockReset(&clock);
   mulTestPacket_t p={.sequence=o->source.sequence,.txTimestamp=o->source.txTimestamp};
   memcpy(p.source,o->sourceClock,8);
   mulNetwork_t network;mulSchedulePair(&s.schedule,slot,&network);
   mulClockObserveFitted(&network,&clock,&fit,&c,&p,o->source.rxTimestamp,o->acceptedMs,true,NULL);
  }
  s.rate=clock;evaluate(&s,slot,now);mulMultiValue_t v=s.values[slot-1];
  printf("%u %u %d %u %u %u %d %u\n",s.cycle,v.status,v.rangeDifferenceMm,fit.resets,fit.applied,fit.fallback,clock.skewPpb,clock.status);
 }
 return ferror(stdin)?2:0;
}
