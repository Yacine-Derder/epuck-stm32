/* SPDX-License-Identifier: MIT */
#ifndef UWB_RUNTIME_H
#define UWB_RUNTIME_H
#ifndef MUL_UNIVERSAL
#define MUL_UNIVERSAL 0
#endif
#include "uwb_config.h"
#if MUL_UNIVERSAL
#include "mul_telemetry.h"
void uwb_runtime_init(void);
void uwb_runtime_task(void);
bool uwb_runtime_command(const char *line);
const uwb_config_t *uwb_runtime_active(void);
/* Main-loop only; bounded local queue, not an interrupt/UART transmit call. */
void uwb_runtime_telemetry(const char *line);
void uwb_runtime_publish(mulTelemetry_t *record);
bool uwb_runtime_session(uint64_t *session);
const uwb_config_store_t *uwb_runtime_store(void);
#endif
#endif
