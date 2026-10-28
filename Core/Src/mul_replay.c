/* SPDX-License-Identifier: MIT */
#include "mul_replay.h"
#include <string.h>
static void put(uint8_t **p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    *(*p)++ = (uint8_t)(v >> (8 * i));
}
static uint64_t get(const uint8_t **p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; i++)
    v |= (uint64_t)*(*p)++ << (8 * i);
  return v;
}
static int32_t signed32(uint32_t n) {
  return n <= INT32_MAX ? (int32_t)n
                        : (int32_t)((int64_t)n - INT64_C(4294967296));
}
uint32_t mulReplayDigest(const uint8_t *p, unsigned n) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < n; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}
static void observation(uint8_t **p, const mulObservation_t *o) {
  memcpy(*p, o->sourceClock, 8);
  *p += 8;
  memcpy(*p, o->receiverClock, 8);
  *p += 8;
  put(p, o->source.sequence, 4);
  put(p, o->source.txTimestamp, 5);
  put(p, o->source.rxTimestamp, 5);
  put(p, o->correctedRx, 5);
  put(p, o->generation, 4);
  put(p, o->acceptedMs, 4);
  put(p, (uint16_t)o->power.powerCentiDbm, 2);
  put(p, (uint8_t)o->power.deltaDtu, 1);
  put(p, o->power.status, 1);
  put(p, o->power.table, 1);
  put(p, o->valid | (o->remoteEpochKnown << 1) | (o->source.hasSequence << 2),
      1);
}
static void readObservation(const uint8_t **p, mulObservation_t *o) {
  memcpy(o->sourceClock, *p, 8);
  *p += 8;
  memcpy(o->receiverClock, *p, 8);
  *p += 8;
  o->source.sequence = get(p, 4);
  o->source.txTimestamp = get(p, 5);
  o->source.rxTimestamp = get(p, 5);
  o->correctedRx = get(p, 5);
  o->generation = get(p, 4);
  o->acceptedMs = get(p, 4);
  o->power.powerCentiDbm = (int16_t)get(p, 2);
  o->power.deltaDtu = (int8_t)get(p, 1);
  o->power.status = get(p, 1);
  o->power.table = get(p, 1);
  unsigned f = get(p, 1);
  o->valid = f & 1;
  o->remoteEpochKnown = (f >> 1) & 1;
  o->source.hasSequence = (f >> 2) & 1;
}
#if MUL_TDOA_REPLAY
/* Link-time observation of the production return value. No equations or
 * decisions are replaced; __real returns the result used by localization. */
#include "mul_downlink.h"
typedef struct {
  uint8_t peer;
  bool used;
  uint32_t refSequence, folSequence, refGeneration, folGeneration;
  uint64_t refRx, folRx;
  int64_t values[5];
} replayMath_t;
static replayMath_t mathAt[MUL_MAX_ALRS - 1];
mulDownlink_t __real_mulDownlinkComputeAtSlot(const mulNetwork_t *, uint16_t,
                                              const mulRelation_t *,
                                              mulPair_t *, const mulClock_t *,
                                              uint32_t, uint32_t, bool);
mulDownlink_t
__wrap_mulDownlinkComputeAtSlot(const mulNetwork_t *network, uint16_t offset,
                                const mulRelation_t *relation, mulPair_t *pair,
                                const mulClock_t *rate, uint32_t now,
                                uint32_t age, bool stationary) {
  mulDownlink_t d = __real_mulDownlinkComputeAtSlot(
      network, offset, relation, pair, rate, now, age, stationary);
  replayMath_t *entry = NULL;
  for (unsigned i = 0; i < MUL_MAX_ALRS - 1; i++)
    if (mathAt[i].used && mathAt[i].peer == pair->follower.sourceClock[0]) {
      entry = &mathAt[i];
      break;
    }
  if (!entry)
    for (unsigned i = 0; i < MUL_MAX_ALRS - 1; i++)
      if (!mathAt[i].used) {
        entry = &mathAt[i];
        break;
      }
  if (entry)
    *entry = (replayMath_t){.used = true,
                            .peer = pair->follower.sourceClock[0],
                            .refSequence = pair->reference.source.sequence,
                            .folSequence = pair->follower.source.sequence,
                            .refGeneration = pair->reference.generation,
                            .folGeneration = pair->follower.generation,
                            .refRx = pair->reference.source.rxTimestamp,
                            .folRx = pair->follower.source.rxTimestamp,
                            .values = {d.txSeparationRef, d.rawRxSeparationRef,
                                       d.correctedRxSeparationRef, d.rawTdoaRef,
                                       d.correctedTdoaRef}};
  return d;
}
#endif
bool mulReplayPack(uint8_t out[MUL_REPLAY_BYTES], const mulMultiReceiver_t *s,
                   unsigned slot, const uint32_t status[MUL_MAX_ALRS]) {
  if (!out || !s || slot == 0 || slot >= s->schedule.count ||
      mulScheduleValidate(&s->schedule))
    return false;
  memset(out, 0, MUL_REPLAY_BYTES);
  uint8_t *p = out;
  put(&p, 1, 1);
  put(&p, s->plan != NULL, 1);
  put(&p, slot, 1);
  put(&p, s->stationary, 1);
  put(&p, s->cycle, 4);
  put(&p, s->seenMask, 1);
  put(&p, s->schedule.count, 1);
  bool sourceVersion = false;
  for (unsigned i = 1; i < s->schedule.count; i++)
    if ((s->seenMask & (1u << i)) && s->relations[i - 1].version == 3)
      sourceVersion = true;
  put(&p, sourceVersion, 1);
  put(&p, 0, 1);
  put(&p, s->cycleStartMs, 4);
  if (s->plan) {
    if (!mulRangingPlanEncode(p, s->plan))
      return false;
  } else if (!mulScheduleEncode(p, &s->schedule))
    return false;
  p += 64;
  observation(&p, &s->sources[0]);
  observation(&p, &s->sources[slot]);
  const mulRelation_t *r = &s->relations[slot - 1];
  put(&p, r->referenceOrigin, 5);
  put(&p, r->followerOrigin, 5);
  put(&p, r->epoch, 8);
  put(&p, (uint32_t)r->skewPpb, 4);
  put(&p, r->referenceSequence, 4);
  put(&p, r->twrSequence, 4);
  put(&p, r->originAgeMs, 2);
  put(&p, r->delayAgeMs, 2);
  put(&p, r->remainingMs, 2);
  put(&p, r->delayDtu, 2);
  put(&p, r->maxAgeMs, 2);
  put(&p,
      r->available | (r->powerCorrectedTwr << 1) | (r->version << 2) |
          (r->delayState << 4) | (r->externalPose << 7),
      1);
  put(&p, r->period10ms, 1);
  const mulClock_t *c = &s->rate;
  put(&p, c->epoch, 8);
  put(&p, c->sequence, 4);
  put(&p, c->referenceTx, 5);
  put(&p, c->localRx, 5);
  put(&p, c->observedMs, 4);
  put(&p, (uint32_t)c->skewPpb, 4);
  put(&p, c->status, 1);
  put(&p, c->hasBaseline, 1);
  put(&p, c->count, 1);
  put(&p, c->next, 1);
  put(&p, c->periodMs, 2);
  put(&p, c->samples, 4);
  if (!mulClockFitConfigEncode(p, &s->clockConfig))
    return false;
  p += MUL_CLOCK_CONFIG_BYTES;
  const mulClockFit_t *f = &s->fit;
  put(&p, f->epoch, 8);
  put(&p, (uint32_t)f->skewPpb, 4);
  put(&p, f->rmsDtu, 4);
  put(&p, f->uncertaintyPpb, 4);
  put(&p, f->rejected, 4);
  put(&p, f->accepted, 4);
  put(&p, f->resets, 4);
  put(&p, f->count, 1);
  put(&p, f->inliers, 1);
  put(&p, f->ready, 1);
  put(&p, f->applied, 1);
  put(&p, f->fallback, 1);
  put(&p, f->compensated, 1);
  put(&p, s->captureAge[0], 2);
  put(&p, s->captureAge[slot], 2);
  put(&p, status ? status[0] : 0, 4);
  put(&p, status ? status[slot] : 0, 4);
  const mulObservation_t *a = &s->sources[0], *b = &s->sources[slot];
  uint32_t now = (int32_t)(a->acceptedMs - b->acceptedMs) > 0 ? a->acceptedMs
                                                              : b->acceptedMs;
  put(&p, now, 4);
  put(&p, s->values[slot - 1].reason == mulMultiScheduleMismatch,
      1); // Admission outcome, before mathematical evaluation.
  bool mathValid = false;
#if MUL_TDOA_REPLAY
  for (unsigned i = 0; i < MUL_MAX_ALRS - 1; i++) {
    const replayMath_t *m = &mathAt[i];
    if ((s->seenMask & ((1u << slot) | 1u)) == ((1u << slot) | 1u) && m->used &&
        m->peer == b->sourceClock[0] && m->refSequence == a->source.sequence &&
        m->folSequence == b->source.sequence &&
        m->refGeneration == a->generation &&
        m->folGeneration == b->generation &&
        m->refRx == a->source.rxTimestamp &&
        m->folRx == b->source.rxTimestamp) {
      for (unsigned j = 0; j < 5; j++)
        put(&p, (uint64_t)m->values[j], 8);
      mathValid = true;
      break;
    }
  }
#endif
  if (!mathValid)
    p += 40; /* zeroed, explicitly unavailable on admission rejection */
  put(&p, mathValid, 1);
  if ((unsigned)(p - out) != MUL_REPLAY_RESULT_AT)
    return false;
  p = out + MUL_REPLAY_RESULT_AT;
  const mulMultiValue_t *v = &s->values[slot - 1];
  put(&p, (uint32_t)v->tdoaDtu, 4);
  put(&p, (uint32_t)v->rangeDifferenceMm, 4);
  put(&p, v->status, 1);
  put(&p, v->clockValid, 1);
  put(&p, v->rateState, 1);
  put(&p, v->reason, 1);
  put(&p, v->source, 1);
  return true;
}
bool mulReplayUnpack(const uint8_t in[MUL_REPLAY_BYTES], mulMultiReceiver_t *s,
                     mulRangingPlan_t *plan, unsigned *slot, uint32_t *evalNow,
                     uint32_t status[2]) {
  if (!in || !s || !plan || !slot || !evalNow || !status || in[0] != 1 ||
      in[1] > 1 || in[3] > 1 || in[10] > 1 || in[11])
    return false;
  memset(s, 0, sizeof(*s));
  const uint8_t *p = in + 1;
  bool planned = get(&p, 1);
  *slot = get(&p, 1);
  s->stationary = get(&p, 1);
  s->cycle = get(&p, 4);
  s->seenMask = get(&p, 1);
  unsigned count = get(&p, 1);
  p += 2;
  s->cycleStartMs = get(&p, 4);
  if (planned) {
    if (!mulRangingPlanDecode(p, plan))
      return false;
    s->plan = plan;
    s->schedule = plan->broadcast;
  } else if (!mulScheduleDecode(p, &s->schedule))
    return false;
  p += 64;
  if (*slot == 0 || *slot >= s->schedule.count || count != s->schedule.count ||
      s->seenMask >> count)
    return false;
  readObservation(&p, &s->sources[0]);
  readObservation(&p, &s->sources[*slot]);
  mulRelation_t *r = &s->relations[*slot - 1];
  r->referenceOrigin = get(&p, 5);
  r->followerOrigin = get(&p, 5);
  r->epoch = get(&p, 8);
  r->skewPpb = signed32(get(&p, 4));
  r->referenceSequence = get(&p, 4);
  r->twrSequence = get(&p, 4);
  r->originAgeMs = get(&p, 2);
  r->delayAgeMs = get(&p, 2);
  r->remainingMs = get(&p, 2);
  r->delayDtu = get(&p, 2);
  r->maxAgeMs = get(&p, 2);
  unsigned flags = get(&p, 1);
  r->available = flags & 1;
  r->powerCorrectedTwr = (flags >> 1) & 1;
  r->version = (flags >> 2) & 3;
  r->delayState = (flags >> 4) & 7;
  r->externalPose = flags >> 7;
  r->period10ms = get(&p, 1);
  mulClock_t *c = &s->rate;
  c->epoch = get(&p, 8);
  c->sequence = get(&p, 4);
  c->referenceTx = get(&p, 5);
  c->localRx = get(&p, 5);
  c->observedMs = get(&p, 4);
  c->skewPpb = signed32(get(&p, 4));
  c->status = get(&p, 1);
  c->hasBaseline = get(&p, 1);
  c->count = get(&p, 1);
  c->next = get(&p, 1);
  c->periodMs = get(&p, 2);
  c->samples = get(&p, 4);
  if (!mulClockFitConfigDecode(p, &s->clockConfig))
    return false;
  p += MUL_CLOCK_CONFIG_BYTES;
  mulClockFit_t *f = &s->fit;
  f->epoch = get(&p, 8);
  f->skewPpb = signed32(get(&p, 4));
  f->rmsDtu = get(&p, 4);
  f->uncertaintyPpb = get(&p, 4);
  f->rejected = get(&p, 4);
  f->accepted = get(&p, 4);
  f->resets = get(&p, 4);
  f->count = get(&p, 1);
  f->inliers = get(&p, 1);
  f->ready = get(&p, 1);
  f->applied = get(&p, 1);
  f->fallback = get(&p, 1);
  f->compensated = get(&p, 1);
  s->captureAge[0] = get(&p, 2);
  s->captureAge[*slot] = get(&p, 2);
  status[0] = get(&p, 4);
  status[1] = get(&p, 4);
  *evalNow = get(&p, 4);
  unsigned mismatch = get(&p, 1);
  if (c->count > MUL_CLOCK_HISTORY || c->next >= MUL_CLOCK_HISTORY ||
      c->status > mulClockValid || mismatch > 1)
    return false;
  p += 40;
  if (get(&p, 1) > 1 || p != in + MUL_REPLAY_RESULT_AT)
    return false;
  mulMultiValue_t *v = &s->values[*slot - 1];
  v->tdoaDtu = signed32(get(&p, 4));
  v->rangeDifferenceMm = signed32(get(&p, 4));
  v->status = get(&p, 1);
  v->clockValid = get(&p, 1);
  v->rateState = get(&p, 1);
  v->reason = get(&p, 1);
  v->source = get(&p, 1);
  if (v->status > 4 || v->clockValid > 1 || v->rateState > 2 || v->reason > 9 ||
      v->source > 3)
    return false;
  while (p < in + MUL_REPLAY_BYTES)
    if (*p++)
      return false;
  // Reserve mismatch admission evidence independently of the expected result.
  s->mismatches = mismatch;
  s->hasCycle = s->active = true;
  return true;
}
#if MUL_TDOA_REPLAY
static uint8_t candidate[MUL_MAX_ALRS - 1][MUL_REPLAY_BYTES],
    queue[MUL_REPLAY_QUEUE][MUL_REPLAY_BYTES];
static uint32_t statusAt[MUL_MAX_ALRS], ids[MUL_REPLAY_QUEUE], sequence,
    dropped, observedCycle;
static unsigned prepared, head, tail, count, part;
void mulReplayReset(void) {
  dropped += count;
  head = tail = count = part = prepared = 0;
  memset(statusAt, 0, sizeof(statusAt));
  memset(mathAt, 0, sizeof(mathAt));
}
void mulReplayReceived(const mulMultiReceiver_t *s, unsigned slot,
                       uint32_t status) {
  if (observedCycle != s->cycle) {
    memset(statusAt, 0, sizeof(statusAt));
    observedCycle = s->cycle;
  }
  if (slot < MUL_MAX_ALRS)
    statusAt[slot] = status;
}
void mulReplayPrepare(const mulMultiReceiver_t *s) {
  prepared = 0;
  if (!s->active)
    return;
  for (unsigned i = 1; i < s->schedule.count; i++) {
    if (!mulReplayPack(candidate[i - 1], s, i, statusAt)) {
      prepared = 0;
      dropped++;
      return;
    }
    prepared++;
  }
}
void mulReplayComplete(const mulMultiCycle_t *c) {
  for (unsigned i = 0; i < prepared; i++) {
    uint32_t id = sequence++;
    if (candidate[i][4] != (uint8_t)c->cycle ||
        candidate[i][5] != (uint8_t)(c->cycle >> 8) ||
        candidate[i][6] != (uint8_t)(c->cycle >> 16) ||
        candidate[i][7] != (uint8_t)(c->cycle >> 24)) {
      dropped++;
      continue;
    }
    if (count == MUL_REPLAY_QUEUE) {
      dropped++;
      continue;
    }
    memcpy(queue[head], candidate[i], MUL_REPLAY_BYTES);
    uint8_t *p = queue[head] + MUL_REPLAY_RESULT_AT;
    const mulMultiValue_t *v = &c->values[i];
    put(&p, (uint32_t)v->tdoaDtu, 4);
    put(&p, (uint32_t)v->rangeDifferenceMm, 4);
    put(&p, v->status, 1);
    put(&p, v->clockValid, 1);
    put(&p, v->rateState, 1);
    put(&p, v->reason, 1);
    put(&p, v->source, 1);
    ids[head] = id;
    head = (head + 1) % MUL_REPLAY_QUEUE;
    count++;
  }
  prepared = 0;
}
void mulReplayService(void) {
  if (count && uwb_runtime_replay_emit(queue[tail], ids[tail], dropped, part)) {
    if (++part == MUL_REPLAY_PARTS) {
      part = 0;
      tail = (tail + 1) % MUL_REPLAY_QUEUE;
      count--;
    }
  }
}
#endif
