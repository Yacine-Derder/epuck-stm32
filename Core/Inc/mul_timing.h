/* SPDX-License-Identifier: MIT */
#ifndef MUL_TIMING_H
#define MUL_TIMING_H
#include <stdint.h>
#ifndef MUL_TIMING_TRACE
#define MUL_TIMING_TRACE 0
#endif
#if MUL_TIMING_TRACE
#include <stdint.h>
void mul_timing_init(void);
void mul_timing_spi_begin(void);
void mul_timing_spi_end(void);
uint32_t mul_timing_now(void);
void mul_timing_service(uint32_t start);
void mul_timing_report(void);
void mul_timing_snapshot(uint32_t out[4]);
void mul_timing_reset_stats(void);
#else
static inline void mul_timing_snapshot(uint32_t out[4]) {for(unsigned i=0;i<4;i++)out[i]=0;}
#define mul_timing_reset_stats() ((void)0)
#define mul_timing_init() ((void)0)
#define mul_timing_spi_begin() ((void)0)
#define mul_timing_spi_end() ((void)0)
#define mul_timing_now() 0u
#define mul_timing_service(start) ((void)(start))
#define mul_timing_report() ((void)0)
#endif
#endif
