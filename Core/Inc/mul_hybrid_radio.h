/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_HYBRID_RADIO_H
#define MUL_HYBRID_RADIO_H
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
void mul_hybrid_radio_configure(const uwb_config_t *config);
void mul_hybrid_radio_process(void);
void mul_hybrid_radio_status(char *line,size_t capacity);
void mul_hybrid_radio_diagnostics(unsigned page,char *line,size_t capacity);
void mul_hybrid_radio_frame(unsigned index,char *line,size_t capacity);
bool mul_hybrid_radio_reset_stats(void);
#endif
#endif
