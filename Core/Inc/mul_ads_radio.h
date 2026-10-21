/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ADS_RADIO_H
#define MUL_ADS_RADIO_H
#include "mul_diag_config.h"
#include <stdint.h>
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
void mul_ads_radio_configure(const uwb_config_t *config,uint64_t session);
void mul_ads_radio_stop(void);
#endif
int32_t mul_ads_radio_init(void);
void mul_ads_radio_process(void);
#endif
