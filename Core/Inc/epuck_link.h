#ifndef EPUCK_LINK_H
#define EPUCK_LINK_H

/* Set to 0 and rebuild to remove all link activity, IRQ setup and callbacks.
 * Alternatively, comment out BOTH calls in main.c and rebuild.
 */
#define EPUCK_LINK_ENABLED 1

#ifdef __cplusplus
extern "C" {
#endif

/* Existing USART1: PA9 TX, PA10 RX, 115200 8N1. No motor/arming commands.
 * Init enables reception and its NVIC interrupt; task must run regularly.
 * With the switch off, both functions are side-effect-free stubs.
 */
void epuck_link_init(void);
void epuck_link_task(void);

#ifdef __cplusplus
}
#endif

#endif
