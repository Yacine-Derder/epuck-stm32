/* Host-only baseline oracle: compile the SAME receiver and conversion sources.
 * No copied estimator or integer arithmetic. Input: schema-1 384-byte snapshots. */
#include "mul_replay.h"
#include "mul_downlink.h"
#include <stdio.h>
#include <inttypes.h>
#include "../ThirdParty/mul_core/mul_multi_receiver.c"
int main(void) {
 uint8_t b[MUL_REPLAY_BYTES]; size_t n;
 while((n=fread(b,1,sizeof b,stdin))) {
  mulMultiReceiver_t s;mulRangingPlan_t plan;unsigned slot;uint32_t now,status[2];
  if(n!=sizeof b||!mulReplayUnpack(b,&s,&plan,&slot,&now,status)){puts("{\"error\":\"invalid_snapshot\"}");continue;}
  mulMultiValue_t expected=s.values[slot-1];
  /* Decoder rejection is an admission decision, not a downlink calculation. */
  if(s.mismatches&&!(s.seenMask&(1u<<slot)))s.values[slot-1]=invalid(mulMultiScheduleMismatch);
  else if((s.seenMask&(1u<<slot))&&!s.sources[slot].valid&&s.sources[slot].generation==UINT32_MAX)s.values[slot-1]=invalid(mulMultiSequence);
  else evaluate(&s,slot,now);
  mulMultiValue_t actual=s.values[slot-1];
  actual.source=(s.seenMask&(1u<<slot))&&s.relations[slot-1].version==3?1+mulRelationSource(&s.relations[slot-1]):0;
  bool match=actual.tdoaDtu==expected.tdoaDtu&&actual.rangeDifferenceMm==expected.rangeDifferenceMm&&actual.status==expected.status&&actual.clockValid==expected.clockValid&&actual.rateState==expected.rateState&&actual.reason==expected.reason&&actual.source==expected.source;
  printf("{\"cycle\":%"PRIu32",\"slot\":%u,\"match\":%s,\"dtu\":%"PRId32",\"mm\":%"PRId32",\"status\":%u,\"reason\":%u,\"dtu_match\":%s,\"mm_match\":%s,\"status_match\":%s",s.cycle,slot,match?"true":"false",actual.tdoaDtu,actual.rangeDifferenceMm,actual.status,actual.reason,actual.tdoaDtu==expected.tdoaDtu?"true":"false",actual.rangeDifferenceMm==expected.rangeDifferenceMm?"true":"false",actual.status==expected.status&&actual.reason==expected.reason?"true":"false");
  if((s.seenMask&((1u<<slot)|1u))==((1u<<slot)|1u)) {
   mulNetwork_t network;mulSchedulePair(&s.schedule,slot,&network);
   mulPair_t pair={.reference=s.sources[0],.follower=s.sources[slot],.rawArrivalDifferenceDtu=mulTimestampDifference(s.sources[slot].source.rxTimestamp,s.sources[0].source.rxTimestamp),.physicalBoundDtu=UINT64_MAX};
   mulDownlink_t d=mulDownlinkComputeAtSlot(&network,mulScheduleOffsetMs(&s.schedule,slot),&s.relations[slot-1],&pair,&s.rate,now,s.captureAge[slot],s.stationary);
   printf(",\"raw_arrival_local_dtu\":%"PRId64",\"tx_separation_ref_dtu\":%"PRId64",\"arrival_ref_dtu\":%"PRId64",\"corrected_arrival_ref_dtu\":%"PRId64",\"intermediate_status\":%u",pair.rawArrivalDifferenceDtu,d.txSeparationRef,d.rawRxSeparationRef,d.correctedRxSeparationRef,d.status);
  }
  puts("}");
 }
 return ferror(stdin)?2:0;
}
