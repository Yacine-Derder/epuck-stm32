#define APP_CONFIG_H
#define BOARD_ID 1U
#define BOARD_ROLE QORVO_ROLE_TX
#include <assert.h>
#include "../../Core/Src/uwb_link.c"
static int enabled,initCalls,processCalls,isrCalls,fail;
void HAL_NVIC_DisableIRQ(int irq) { assert(irq==EXTI0_IRQn); enabled=0; }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq==EXTI0_IRQn); enabled=1; }
void HAL_Delay(uint32_t t) { assert(t==100); }
int32_t mul_diag_init(void) { initCalls++; return fail ? -1 : 0; }
void mul_diag_process(void) { processCalls++; }
int32_t phy_interop_init(void) { initCalls++; return fail ? -1 : 0; }
void phy_interop_process(void) { processCalls++; }
#if APP_UWB_ENABLED || PHY_INTEROP_ROLE || MUL_DIAG_ROLE
int32_t qorvo_link_init(uint8_t id,qorvo_role_t role) { assert(id==1 && role==QORVO_ROLE_TX); initCalls++; return fail ? -1 : 0; }
#endif
void qorvo_link_process(void) { processCalls++; }
void dwt_isr(void) { isrCalls++; }
void usb_console_write(const char *s) { (void)s; }
int main(void) {
  uwb_link_task(); assert(processCalls==0); uwb_link_init(); uwb_link_task(); uwb_link_exti_callback(DWM_IRQ_Pin);
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE
  assert(initCalls==1 && processCalls==1 && !enabled && !isrCalls);
#elif APP_UWB_ENABLED
  assert(initCalls==1 && processCalls==1 && enabled && isrCalls==1);
#else
  assert(!initCalls && !processCalls && !enabled && !isrCalls);
#endif
  fail=1; processCalls=0; uwb_link_init(); uwb_link_task();
  assert(!enabled && !processCalls);
}
