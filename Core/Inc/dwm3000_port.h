#ifndef __DWM3000_PORT_H__
#define __DWM3000_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define DWM3000_DEV_ID_REG      0x00U
#define DWM3000_EXPECTED_DEV_ID 0xDECA0302UL

void dwm3000_port_init(void);
void dwm3000_hw_reset(void);
uint32_t dwm3000_read_dev_id(void);
void dwm3000_basic_test(void);

#ifdef __cplusplus
}
#endif

#endif /* __DWM3000_PORT_H__ */