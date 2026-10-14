#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Change these switches, rebuild, then flash the resulting firmware. */
#define APP_EPUCK_ENABLED 1
#define APP_UWB_ENABLED   0

/* Board role constants are supplied by qorvo_platform.h in uwb_link.c. */
// For the transmitter board, use:
#define BOARD_ID   1U
#define BOARD_ROLE QORVO_ROLE_TX

// For the receiver board, use:
// #define BOARD_ID   2U
// #define BOARD_ROLE QORVO_ROLE_RX

#endif
