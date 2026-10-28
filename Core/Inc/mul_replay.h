/* SPDX-License-Identifier: MIT */
#ifndef MUL_REPLAY_H
#define MUL_REPLAY_H
#include "mul_multi_receiver.h"
#ifndef MUL_TDOA_REPLAY
#define MUL_TDOA_REPLAY 0
#endif
#define MUL_REPLAY_BYTES 384u
#define MUL_REPLAY_RESULT_AT 368u
#define MUL_REPLAY_PART_BYTES 100u
#define MUL_REPLAY_PARTS 4u
#define MUL_REPLAY_QUEUE 4u
/* Diagnostic-only schema. No native struct layout is transported. */
bool mulReplayPack(uint8_t out[MUL_REPLAY_BYTES], const mulMultiReceiver_t *s,
                   unsigned slot, const uint32_t status[MUL_MAX_ALRS]);
bool mulReplayUnpack(const uint8_t in[MUL_REPLAY_BYTES], mulMultiReceiver_t *s,
                     mulRangingPlan_t *plan, unsigned *slot, uint32_t *evalNow,
                     uint32_t status[2]);
uint32_t mulReplayDigest(const uint8_t *p, unsigned n);
#if MUL_TDOA_REPLAY
void mulReplayReset(void);
void mulReplayPrepare(const mulMultiReceiver_t *s);
void mulReplayReceived(const mulMultiReceiver_t *s, unsigned slot,
                       uint32_t status);
void mulReplayComplete(const mulMultiCycle_t *cycle);
void mulReplayService(void);
/* Owner-loop sink, shares the normal envelope/session/sequence and queues. */
bool uwb_runtime_replay_emit(const uint8_t blob[MUL_REPLAY_BYTES], uint32_t id,
                             uint32_t dropped, unsigned part);
#else
#define mulReplayReset() ((void)0)
#define mulReplayPrepare(s) ((void)0)
#define mulReplayReceived(s, i, v) ((void)0)
#define mulReplayComplete(c) ((void)0)
#define mulReplayService() ((void)0)
#endif
#endif
