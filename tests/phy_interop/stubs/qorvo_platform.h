#include "main.h"
#include "deca_device_api.h"
int32_t qorvo_probe_and_init(void);
void qorvo_spi_set_fastrate(void);
typedef enum { QORVO_ROLE_TX, QORVO_ROLE_RX } qorvo_role_t;
int32_t qorvo_link_init(uint8_t id,qorvo_role_t role);
void qorvo_link_process(void);
