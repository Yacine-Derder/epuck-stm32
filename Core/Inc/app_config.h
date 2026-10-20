#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Change these switches, rebuild, then flash the resulting firmware. */
#define APP_EPUCK_ENABLED 1
#define APP_UWB_ENABLED   0
#define APP_ESP_ENABLED   1

/* 0 disables automatic DATA; otherwise 50..10000 ms. */
#ifndef APP_EPUCK_STREAM_PERIOD_MS
#define APP_EPUCK_STREAM_PERIOD_MS 1000U
#endif

/* Experimental compile-only policy. Keep UART1 safety active, prohibit ARM/M,
 * request STOP before UWB startup, and explicitly stop dsPIC automatic DATA. */
#ifndef MUL_UWB_ONLY
#define MUL_UWB_ONLY 0
#endif
#ifndef APP_DEBUG_TELEMETRY
#define APP_DEBUG_TELEMETRY 0
#endif
#define APP_WIFI_SENSOR_PERIOD_MS 1000U
#if MUL_UWB_ONLY
#undef APP_ESP_ENABLED
#define APP_ESP_ENABLED 0
#undef APP_EPUCK_ENABLED
#define APP_EPUCK_ENABLED 1
#undef APP_EPUCK_STREAM_PERIOD_MS
#define APP_EPUCK_STREAM_PERIOD_MS 0U
#undef APP_DEBUG_TELEMETRY
#define APP_DEBUG_TELEMETRY 0
#endif

/* Board role constants are supplied by qorvo_platform.h in uwb_link.c. */
// For the transmitter board, use:
#define BOARD_ID   1U
#define BOARD_ROLE QORVO_ROLE_TX

// For the receiver board, use:
// #define BOARD_ID   2U
// #define BOARD_ROLE QORVO_ROLE_RX

#endif
