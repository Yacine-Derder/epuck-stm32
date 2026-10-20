#ifndef MUL_TEST_TIMING_MAIN_H
#define MUL_TEST_TIMING_MAIN_H
#include <stdint.h>
typedef struct { uint32_t CTRL,CYCCNT; } MockDwt;
typedef struct { uint32_t DEMCR; } MockDebug;
extern MockDwt fakeDwt;
extern MockDebug fakeDebug;
extern uint32_t SystemCoreClock,irqMask;
#define DWT (&fakeDwt)
#define CoreDebug (&fakeDebug)
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
static inline uint32_t __get_PRIMASK(void) { return irqMask; }
static inline void __disable_irq(void) { irqMask=1; }
static inline void __enable_irq(void) { irqMask=0; }
#endif
