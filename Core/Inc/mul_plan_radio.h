/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_PLAN_RADIO_H
#define MUL_PLAN_RADIO_H
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
typedef struct {
 uint64_t stamps[6]; // P1/P4/P5 initiator; P2/P3/P6 local responder clock.
 mulPower_t power[3];
 uint32_t cycle,measuredMs,fingerprint;
 uint16_t rawTof,tof; // Initiator DTU; not calibrated SI distance.
 uint8_t operation,initiator,responder;
} mulPlanRangeResult_t;
bool mul_plan_radio_take_result(mulPlanRangeResult_t *out);
/* One DW3000 owner; immutable config/clock belong to hybridConfig/multiReceiver. */
void mul_plan_radio_configure(const mulRangingPlan_t *plan,uint8_t local,const mulClock_t *clock);
void mul_plan_radio_reference(uint32_t cycle,uint16_t captureAgeMs);
void mul_plan_radio_suspend(void);
/* true means the radio event was consumed; broadcasts remain for the TDMA path. */
bool mul_plan_radio_process(uint32_t nowMs,uint32_t status);
bool mul_plan_radio_busy(void);
#endif
#endif
