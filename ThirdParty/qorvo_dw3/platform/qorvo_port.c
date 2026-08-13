#include "qorvo_platform.h"
#include "deca_interface.h"
#include "usbd_cdc_if.h"
#include "dw3000_deca_regs.h"
#include "dw3000_deca_vals.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>

extern UART_HandleTypeDef huart3;
extern const struct dwt_driver_s dw3000_driver;

/* -------------------------------------------------------------------------- */
/* Project tuning knobs                                                       */
/* -------------------------------------------------------------------------- */
/*
 * 1) Channel:
 *    We switch to channel 9 here.
 *
 * 2) Reply timing:
 *    ANSWER and FINAL are sent as delayed TX relative to the RX timestamp of the
 *    triggering frame. This keeps DS-TWR symmetric and follows APS011 guidance
 *    to keep reply delays short and as equal as possible.
 *
 * 3) Power:
 *    The code below provides an optional fixed-TX-power block, but it is OFF by
 *    default. For a starter build I prefer keeping the driver/device defaults
 *    until the ranging exchange is stable and antenna delay is calibrated.
 *    After that you can enable the fixed block and characterize the result.
 *
 * 4) Calibration:
 *    - ANT_DELAY mode prints mean/stddev at a known distance.
 *    - APPLY_EXTRA mode lets you quickly test manual DTU offsets.
 *    - RANGE_BIAS hook is present but left OFF by default because there is no
 *      public DW3000/DWM3000 APS011-equivalent bias table in the files here.
 */

#define QORVO_CFG_CHANNEL                 DWT_CH9
#define QORVO_CFG_TX_CODE                 9U
#define QORVO_CFG_RX_CODE                 9U
#define QORVO_CFG_DATA_RATE               DWT_BR_6M8
#define QORVO_CFG_PREAMBLE_LEN            DWT_PLEN_128
#define QORVO_CFG_PAC                     DWT_PAC8
#define QORVO_CFG_SFD_TYPE                DWT_SFD_IEEE_4A
#define QORVO_CFG_STS_MODE                DWT_STS_MODE_OFF
#define QORVO_CFG_STS_LEN                 DWT_STS_LEN_64
#define QORVO_CFG_PDOA_MODE               DWT_PDOA_M0

/* Use delayed symmetric replies for DS-TWR. */
#define QORVO_REPLY_DELAY_UUS             1600U
#define QORVO_TWR_RX_TIMEOUT_UUS          12000U

/* Antenna delays: start with equal placeholders, then calibrate. */
#define QORVO_TX_ANT_DLY_DTU              16380U
#define QORVO_RX_ANT_DLY_DTU              16380U

/* Fast way to test manual offsets during calibration without editing the base values. */
#define QORVO_CAL_APPLY_EXTRA_ANT_DLY     0
#define QORVO_CAL_EXTRA_TX_ANT_DLY_DTU    0
#define QORVO_CAL_EXTRA_RX_ANT_DLY_DTU    0

/* Optional fixed TX power block.
 * OFF by default because the best value is board + regulatory dependent.
 * Once the link is stable, you can set this to 1 and characterize it.
 * A conservative starter candidate is kept here as a placeholder only.
 */
#define QORVO_USE_FIXED_TX_POWER          0
#define QORVO_FIXED_TX_POWER              0xA0A0A0A0UL
#define QORVO_FIXED_TX_PGDELAY            0x34U
#define QORVO_FIXED_TX_PGCOUNT            0U

/* Calibration / reporting modes. */
#define QORVO_CAL_ANT_DELAY_MODE          0
#define QORVO_CAL_KNOWN_DISTANCE_M        5.000
#define QORVO_CAL_SAMPLE_TARGET           100U
#define QORVO_CAL_PRINT_EVERY_SAMPLE      1
#define QORVO_ENABLE_RANGE_BIAS_HOOK      0

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static void qorvo_log_printf(const char *fmt, ...)
{
    char buf[224];
    va_list ap;
    uint16_t len;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    len = (uint16_t)strlen(buf);

    (void)CDC_Transmit_FS((uint8_t *)buf, len);
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

/* -------------------------------------------------------------------------- */
/* Low-level board helpers                                                    */
/* -------------------------------------------------------------------------- */

void qorvo_hw_reset(void)
{
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

void qorvo_wakeup_device_with_io(void)
{
    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_SET);
    HAL_Delay(2);
}

static struct dwt_spi_s g_qorvo_spi = {
    .readfromspi = readfromspi,
    .writetospi = writetospi,
    .writetospiwithcrc = writetospiwithcrc,
    .setslowrate = qorvo_spi_set_slowrate,
    .setfastrate = qorvo_spi_set_fastrate,
};

static struct dwt_driver_s *g_driver_list[] = {
    (struct dwt_driver_s *)&dw3000_driver,
};

/* -------------------------------------------------------------------------- */
/* Bring-up helpers                                                           */
/* -------------------------------------------------------------------------- */

static uint16_t qorvo_get_tx_ant_dly(void)
{
    uint32_t val = (uint32_t)QORVO_TX_ANT_DLY_DTU;
#if QORVO_CAL_APPLY_EXTRA_ANT_DLY
    val += (uint32_t)QORVO_CAL_EXTRA_TX_ANT_DLY_DTU;
#endif
    if (val > 0xFFFFUL) {
        val = 0xFFFFUL;
    }
    return (uint16_t)val;
}

static uint16_t qorvo_get_rx_ant_dly(void)
{
    uint32_t val = (uint32_t)QORVO_RX_ANT_DLY_DTU;
#if QORVO_CAL_APPLY_EXTRA_ANT_DLY
    val += (uint32_t)QORVO_CAL_EXTRA_RX_ANT_DLY_DTU;
#endif
    if (val > 0xFFFFUL) {
        val = 0xFFFFUL;
    }
    return (uint16_t)val;
}

int32_t qorvo_probe_and_init(void)
{
    struct dwt_probe_s probe;
    int32_t ret;

    memset(&probe, 0, sizeof(probe));
    probe.dw = NULL;
    probe.spi = &g_qorvo_spi;
    probe.wakeup_device_with_io = qorvo_wakeup_device_with_io;
    probe.driver_list = g_driver_list;
    probe.dw_driver_num = 1U;

    qorvo_hw_reset();
    qorvo_spi_set_slowrate();

    ret = dwt_probe(&probe);
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Qorvo probe failed\r\n");
        return ret;
    }

    qorvo_log_printf("Qorvo probe OK\r\n");

    ret = dwt_initialise(DWT_READ_OTP_ALL);
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Qorvo init failed: %ld\r\n", (long)ret);
        return ret;
    }

    qorvo_log_printf("Qorvo init OK\r\n");
    return DWT_SUCCESS;
}

uint32_t qorvo_read_devid(void)
{
    return dwt_readdevid();
}

static void qorvo_apply_txrf_if_enabled(void)
{
#if QORVO_USE_FIXED_TX_POWER
    dwt_txconfig_t txrf;

    memset(&txrf, 0, sizeof(txrf));
    txrf.PGdly   = (uint8_t)QORVO_FIXED_TX_PGDELAY;
    txrf.power   = (uint32_t)QORVO_FIXED_TX_POWER;
    txrf.PGcount = (uint16_t)QORVO_FIXED_TX_PGCOUNT;

    dwt_configuretxrf(&txrf);

    qorvo_log_printf("TXRF fixed config applied: power=0x%08lX PGdly=0x%02X PGcount=%u\r\n",
                     (unsigned long)txrf.power,
                     txrf.PGdly,
                     txrf.PGcount);
#else
    qorvo_log_printf("TXRF fixed config not applied; using driver/device defaults\r\n");
#endif
}

int32_t qorvo_configure_radio_default(void)
{
    dwt_config_t config;
    int32_t ret;
    uint16_t tx_ant_dly;
    uint16_t rx_ant_dly;

    memset(&config, 0, sizeof(config));

    config.chan           = QORVO_CFG_CHANNEL;
    config.txPreambLength = QORVO_CFG_PREAMBLE_LEN;
    config.rxPAC          = QORVO_CFG_PAC;
    config.txCode         = QORVO_CFG_TX_CODE;
    config.rxCode         = QORVO_CFG_RX_CODE;
    config.sfdType        = QORVO_CFG_SFD_TYPE;
    config.dataRate       = QORVO_CFG_DATA_RATE;
    config.phrMode        = DWT_PHRMODE_STD;
    config.phrRate        = DWT_PHRRATE_STD;
    config.sfdTO          = DWT_SFDTOC_DEF;
    config.stsMode        = QORVO_CFG_STS_MODE;
    config.stsLength      = QORVO_CFG_STS_LEN;
    config.pdoaMode       = QORVO_CFG_PDOA_MODE;

    ret = dwt_configure(&config);
    if (ret != DWT_SUCCESS) {
        return ret;
    }

    qorvo_spi_set_fastrate();

    tx_ant_dly = qorvo_get_tx_ant_dly();
    rx_ant_dly = qorvo_get_rx_ant_dly();
    dwt_settxantennadelay(tx_ant_dly);
    dwt_setrxantennadelay(rx_ant_dly);

    qorvo_apply_txrf_if_enabled();

    qorvo_log_printf("Radio configured: ch=%u txCode=%u rxCode=%u preamble=%u dataRate=%u\r\n",
                     (unsigned int)config.chan,
                     (unsigned int)config.txCode,
                     (unsigned int)config.rxCode,
                     (unsigned int)config.txPreambLength,
                     (unsigned int)config.dataRate);
    qorvo_log_printf("Antenna delays: TX=%u DTU RX=%u DTU\r\n",
                     (unsigned int)tx_ant_dly,
                     (unsigned int)rx_ant_dly);

    return DWT_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Existing basic tests                                                       */
/* -------------------------------------------------------------------------- */

void qorvo_basic_test(void)
{
    uint32_t dev_id;
    int32_t ret;

    qorvo_log_printf("\r\n=== Qorvo driver basic test ===\r\n");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Qorvo driver test FAILED\r\n");
        return;
    }

    dev_id = qorvo_read_devid();
    qorvo_log_printf("Qorvo API DEV_ID = 0x%08lX\r\n", dev_id);

    if (dev_id == DWT_DW3000_DEV_ID) {
        qorvo_log_printf("Qorvo driver OK: DW3000 communication works.\r\n");
    } else {
        qorvo_log_printf("Qorvo driver WARNING: unexpected DEV_ID.\r\n");
    }
}

void qorvo_config_test(void)
{
    int32_t ret;

    qorvo_log_printf("\r\n=== Qorvo radio config test ===\r\n");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Probe/init failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_configure_radio_default();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("dwt_configure() failed: %ld\r\n", (long)ret);
        return;
    }

    qorvo_log_printf("dwt_configure() OK\r\n");
}

int32_t qorvo_tx_test_once(void)
{
    static uint8_t tx_msg[] = {
        0x41, 0x88,
        0x00,
        0xCA, 0xDE,
        0x01, 0x02,
        0x03, 0x04,
        'H', 'E', 'L', 'L', 'O'
    };

    uint16_t tx_len = (uint16_t)sizeof(tx_msg);
    uint32_t status_reg;
    uint32_t timeout;

    tx_msg[2]++;

    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

    if (dwt_writetxdata(tx_len, tx_msg, 0U) != DWT_SUCCESS) {
        return DWT_ERROR;
    }

    dwt_writetxfctrl(tx_len + 2U, 0U, 1U);

    if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
        return DWT_ERROR;
    }

    timeout = HAL_GetTick() + 100U;
    do {
        status_reg = dwt_read_reg(SYS_STATUS_ID);

        if ((status_reg & DWT_INT_TXFRS_BIT_MASK) != 0U) {
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
            return DWT_SUCCESS;
        }
    } while (HAL_GetTick() < timeout);

    return DWT_ERROR;
}

void qorvo_tx_test(void)
{
    int32_t ret;

    qorvo_log_printf("\r\n=== Qorvo TX test ===\r\n");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Probe/init failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_configure_radio_default();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Radio config failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_tx_test_once();
    if (ret == DWT_SUCCESS) {
        qorvo_log_printf("TX complete OK\r\n");
    } else {
        qorvo_log_printf("TX failed or timed out\r\n");
    }
}

void qorvo_tx_timestamp_test(void)
{
    uint8_t ts[5] = {0};
    uint64_t tx_ts = 0;
    int32_t ret;
    int i;

    qorvo_log_printf("\r\n=== Qorvo TX timestamp test ===\r\n");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Probe/init failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_configure_radio_default();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Radio config failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_tx_test_once();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("TX failed or timed out\r\n");
        return;
    }

    dwt_readtxtimestamp(ts);

    for (i = 0; i < 5; i++) {
        tx_ts |= ((uint64_t)ts[i]) << (8 * i);
    }

    qorvo_log_printf("TX timestamp = 0x%02X%08lX\r\n",
                     (uint8_t)((tx_ts >> 32) & 0xFFU),
                     (uint32_t)(tx_ts & 0xFFFFFFFFULL));
}

void qorvo_rx_timeout_test(void)
{
    int32_t ret;
    uint32_t status_reg;
    uint32_t timeout;

    qorvo_log_printf("\r\n=== Qorvo RX timeout test ===\r\n");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Probe/init failed: %ld\r\n", (long)ret);
        return;
    }

    ret = qorvo_configure_radio_default();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Radio config failed: %ld\r\n", (long)ret);
        return;
    }

    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK |
                         DWT_INT_RXFTO_BIT_MASK |
                         DWT_INT_RXPTO_BIT_MASK |
                         DWT_INT_RXFCE_BIT_MASK |
                         DWT_INT_RXFSL_BIT_MASK |
                         DWT_INT_RXSTO_BIT_MASK |
                         DWT_INT_ARFE_BIT_MASK |
                         DWT_INT_CIAERR_BIT_MASK |
                         DWT_INT_RXOVRR_BIT_MASK |
                         DWT_INT_TXFRS_BIT_MASK);

    dwt_setrxtimeout(1000U);

    ret = dwt_rxenable(DWT_START_RX_IMMEDIATE);
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("RX enable failed: %ld\r\n", (long)ret);
        return;
    }

    timeout = HAL_GetTick() + 200U;

    do {
        status_reg = dwt_read_reg(SYS_STATUS_ID);

        if ((status_reg & DWT_INT_RXFCG_BIT_MASK) != 0U) {
            qorvo_log_printf("RX frame received\r\n");
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
            return;
        }

        if ((status_reg & DWT_INT_RXFTO_BIT_MASK) != 0U) {
            qorvo_log_printf("RX frame timeout\r\n");
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);
            return;
        }

        if ((status_reg & DWT_INT_RXPTO_BIT_MASK) != 0U) {
            qorvo_log_printf("RX preamble timeout\r\n");
            dwt_writesysstatuslo(DWT_INT_RXPTO_BIT_MASK);
            return;
        }

        if ((status_reg & (DWT_INT_RXFCE_BIT_MASK |
                           DWT_INT_RXFSL_BIT_MASK |
                           DWT_INT_RXSTO_BIT_MASK |
                           DWT_INT_ARFE_BIT_MASK |
                           DWT_INT_CIAERR_BIT_MASK |
                           DWT_INT_RXOVRR_BIT_MASK)) != 0U) {
            qorvo_log_printf("RX error, SYS_STATUS=0x%08lX\r\n", status_reg);
            dwt_writesysstatuslo(DWT_INT_RXFCE_BIT_MASK |
                                 DWT_INT_RXFSL_BIT_MASK |
                                 DWT_INT_RXSTO_BIT_MASK |
                                 DWT_INT_ARFE_BIT_MASK |
                                 DWT_INT_CIAERR_BIT_MASK |
                                 DWT_INT_RXOVRR_BIT_MASK);
            return;
        }

    } while (HAL_GetTick() < timeout);

    qorvo_log_printf("RX polling timeout in MCU loop\r\n");
}

/* -------------------------------------------------------------------------- */
/* DS-TWR link layer                                                          */
/* -------------------------------------------------------------------------- */
/*
 * Message flow:
 *   Initiator (TX board): POLL  -> ANSWER -> FINAL -> REPORT
 *   Responder (RX board): listens, answers, then reports its timestamps
 *
 * Range is computed on the initiator after REPORT using:
 *
 *   Ra = t4 - t1   (initiator round-trip: POLL_TX -> ANSWER_RX)
 *   Rb = t6 - t3   (responder round-trip: ANSWER_TX -> FINAL_RX)
 *   Da = t3 - t2   (responder reply delay: POLL_RX -> ANSWER_TX)
 *   Db = t5 - t4   (initiator reply delay: ANSWER_RX -> FINAL_TX)
 *
 *   ToF = (Ra*Rb - Da*Db) / (Ra + Rb + Da + Db)
 *
 * where timestamps are DW3000 device time units (DTU).
 */

#define QORVO_PAN_ID               0xDECAU
#define QORVO_BROADCAST_ADDR       0xFFFFU
#define QORVO_MAX_FRAME_LEN        127U
#define QORVO_TX_PERIOD_MS         1000U

#define QORVO_MSG_POLL             0xE1U
#define QORVO_MSG_ANSWER           0xE2U
#define QORVO_MSG_FINAL            0xE3U
#define QORVO_MSG_REPORT           0xE4U

#define QORVO_HDR_IDX_TYPE         9U
#define QORVO_HDR_IDX_BOARD_ID     10U
#define QORVO_HDR_IDX_COUNTER      11U

#define QORVO_POLL_PAYLOAD_LEN     15U
#define QORVO_ANSWER_PAYLOAD_LEN   15U
#define QORVO_FINAL_PAYLOAD_LEN    25U
#define QORVO_REPORT_PAYLOAD_LEN   30U

#define QORVO_POLL_FRAME_LEN       (QORVO_POLL_PAYLOAD_LEN + 2U)
#define QORVO_ANSWER_FRAME_LEN     (QORVO_ANSWER_PAYLOAD_LEN + 2U)
#define QORVO_FINAL_FRAME_LEN      (QORVO_FINAL_PAYLOAD_LEN + 2U)
#define QORVO_REPORT_FRAME_LEN     (QORVO_REPORT_PAYLOAD_LEN + 2U)

#define QORVO_TS_MASK_40           0xFFFFFFFFFFULL
#define QORVO_SPEED_OF_LIGHT_M_S   299702547.0
#define QORVO_DX_TIME_SHIFT        8U

typedef enum
{
    QORVO_TXMSG_NONE = 0,
    QORVO_TXMSG_POLL,
    QORVO_TXMSG_ANSWER,
    QORVO_TXMSG_FINAL,
    QORVO_TXMSG_REPORT
} qorvo_tx_msg_t;

typedef enum
{
    QORVO_STATE_IDLE = 0,
    QORVO_STATE_WAIT_ANSWER,
    QORVO_STATE_WAIT_FINAL,
    QORVO_STATE_WAIT_REPORT
} qorvo_link_state_t;

typedef struct
{
    uint32_t samples;
    double sum_m;
    double sumsq_m;
    double min_m;
    double max_m;
} qorvo_stats_t;

typedef struct
{
    uint8_t board_id;
    qorvo_role_t role;
    uint8_t seq;
    uint32_t tx_counter;
    uint32_t rx_counter;
    uint32_t last_tx_tick;
    uint8_t initialised;
    uint8_t tx_busy;

    uint8_t peer_board_id;
    uint32_t current_exchange_counter;

    qorvo_tx_msg_t last_tx_msg;
    qorvo_link_state_t state;

    uint8_t poll_tx_ts[5];
    uint8_t poll_rx_ts[5];
    uint8_t answer_tx_ts[5];
    uint8_t answer_rx_ts[5];
    uint8_t final_tx_ts[5];
    uint8_t final_rx_ts[5];
    uint8_t report_tx_ts[5];
    uint8_t report_rx_ts[5];

    double last_tof_dtu;
    double last_tof_ns;
    double last_distance_m;
    double last_distance_corrected_m;
    uint8_t range_valid;

    qorvo_stats_t cal_stats;
} qorvo_link_ctx_t;

static qorvo_link_ctx_t g_link = {0};

static volatile uint32_t g_irq_rx_ok_count = 0U;
static volatile uint32_t g_irq_tx_done_count = 0U;
static volatile uint32_t g_irq_rx_to_count = 0U;
static volatile uint32_t g_irq_rx_err_count = 0U;

static uint64_t qorvo_ts5_to_u64(const uint8_t ts[5])
{
    uint64_t v = 0ULL;

    v |= ((uint64_t)ts[0] << 0);
    v |= ((uint64_t)ts[1] << 8);
    v |= ((uint64_t)ts[2] << 16);
    v |= ((uint64_t)ts[3] << 24);
    v |= ((uint64_t)ts[4] << 32);

    return v & QORVO_TS_MASK_40;
}

static uint64_t qorvo_ts_diff_u40(uint64_t end_ts, uint64_t start_ts)
{
    return (end_ts - start_ts) & QORVO_TS_MASK_40;
}

static void qorvo_read_rx_timestamp(uint8_t ts[5], uint64_t *ts_u64)
{
    dwt_readrxtimestamp(ts, DWT_COMPAT_NONE);

    if (ts_u64 != NULL) {
        *ts_u64 = qorvo_ts5_to_u64(ts);
    }
}

static void qorvo_clear_status_events(void)
{
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK |
                         SYS_STATUS_ALL_RX_GOOD |
                         SYS_STATUS_ALL_RX_ERR |
                         SYS_STATUS_ALL_RX_TO |
                         DWT_INT_RXOVRR_BIT_MASK);
}

static void qorvo_stats_reset(qorvo_stats_t *s)
{
    memset(s, 0, sizeof(*s));
    s->min_m = 1.0e9;
    s->max_m = -1.0e9;
}

static void qorvo_stats_add(qorvo_stats_t *s, double x)
{
    if (s == NULL) {
        return;
    }
    s->samples++;
    s->sum_m += x;
    s->sumsq_m += x * x;
    if (x < s->min_m) {
        s->min_m = x;
    }
    if (x > s->max_m) {
        s->max_m = x;
    }
}

static void qorvo_cal_log_summary(void)
{
#if QORVO_CAL_ANT_DELAY_MODE
    double mean_m;
    double var_m;
    double std_m;
    double err_m;
    double err_cm;

    if (g_link.cal_stats.samples == 0U) {
        return;
    }

    mean_m = g_link.cal_stats.sum_m / (double)g_link.cal_stats.samples;
    var_m = (g_link.cal_stats.sumsq_m / (double)g_link.cal_stats.samples) - (mean_m * mean_m);
    if (var_m < 0.0) {
        var_m = 0.0;
    }
    std_m = sqrt(var_m);
    err_m = mean_m - (double)QORVO_CAL_KNOWN_DISTANCE_M;
    err_cm = err_m * 100.0;

    qorvo_log_printf("[CAL] samples=%lu mean=%.4f m std=%.4f m min=%.4f m max=%.4f m\r\n",
                     (unsigned long)g_link.cal_stats.samples,
                     mean_m,
                     std_m,
                     g_link.cal_stats.min_m,
                     g_link.cal_stats.max_m);
    qorvo_log_printf("[CAL] known=%.4f m error=%+.2f cm\r\n",
                     (double)QORVO_CAL_KNOWN_DISTANCE_M,
                     err_cm);
    qorvo_log_printf("[CAL] If mean is too high, increase both TX/RX antenna delays on both boards.\r\n");
    qorvo_log_printf("[CAL] If mean is too low, decrease both TX/RX antenna delays on both boards.\r\n");
#endif
}

static void qorvo_reset_exchange_timestamps(void)
{
    memset(g_link.poll_tx_ts, 0, sizeof(g_link.poll_tx_ts));
    memset(g_link.poll_rx_ts, 0, sizeof(g_link.poll_rx_ts));
    memset(g_link.answer_tx_ts, 0, sizeof(g_link.answer_tx_ts));
    memset(g_link.answer_rx_ts, 0, sizeof(g_link.answer_rx_ts));
    memset(g_link.final_tx_ts, 0, sizeof(g_link.final_tx_ts));
    memset(g_link.final_rx_ts, 0, sizeof(g_link.final_rx_ts));
    memset(g_link.report_tx_ts, 0, sizeof(g_link.report_tx_ts));
    memset(g_link.report_rx_ts, 0, sizeof(g_link.report_rx_ts));

    g_link.last_tof_dtu = 0.0;
    g_link.last_tof_ns = 0.0;
    g_link.last_distance_m = 0.0;
    g_link.last_distance_corrected_m = 0.0;
    g_link.range_valid = 0U;
}

static void qorvo_enter_idle_rx(void)
{
    g_link.state = QORVO_STATE_IDLE;
    g_link.last_tx_msg = QORVO_TXMSG_NONE;
    g_link.tx_busy = 0U;
    dwt_forcetrxoff();
    qorvo_clear_status_events();
    dwt_setrxtimeout(0U);
    dwt_setpreambledetecttimeout(0U);
    (void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static uint16_t qorvo_build_mac_header(uint8_t *buf,
                                       uint8_t seq,
                                       uint16_t dest_addr,
                                       uint16_t src_addr)
{
    uint16_t i = 0U;

    buf[i++] = 0x41U;
    buf[i++] = 0x88U;
    buf[i++] = seq;

    buf[i++] = (uint8_t)(QORVO_PAN_ID & 0xFFU);
    buf[i++] = (uint8_t)((QORVO_PAN_ID >> 8) & 0xFFU);

    buf[i++] = (uint8_t)(dest_addr & 0xFFU);
    buf[i++] = (uint8_t)((dest_addr >> 8) & 0xFFU);

    buf[i++] = (uint8_t)(src_addr & 0xFFU);
    buf[i++] = (uint8_t)((src_addr >> 8) & 0xFFU);

    return i;
}

static uint16_t qorvo_build_poll_frame(uint8_t *buf,
                                       uint8_t seq,
                                       uint8_t src_board_id,
                                       uint32_t counter)
{
    uint16_t i;

    i = qorvo_build_mac_header(buf, seq, QORVO_BROADCAST_ADDR, (uint16_t)src_board_id);

    buf[i++] = QORVO_MSG_POLL;
    buf[i++] = src_board_id;

    buf[i++] = (uint8_t)(counter & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 8) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 16) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 24) & 0xFFU);

    return i;
}

static uint16_t qorvo_build_answer_frame(uint8_t *buf,
                                         uint8_t seq,
                                         uint8_t src_board_id,
                                         uint8_t dst_board_id,
                                         uint32_t counter)
{
    uint16_t i;

    i = qorvo_build_mac_header(buf, seq, (uint16_t)dst_board_id, (uint16_t)src_board_id);

    buf[i++] = QORVO_MSG_ANSWER;
    buf[i++] = src_board_id;

    buf[i++] = (uint8_t)(counter & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 8) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 16) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 24) & 0xFFU);

    return i;
}

static uint16_t qorvo_build_final_frame(uint8_t *buf,
                                        uint8_t seq,
                                        uint8_t src_board_id,
                                        uint8_t dst_board_id,
                                        uint32_t counter,
                                        const uint8_t poll_tx_ts[5],
                                        const uint8_t answer_rx_ts[5])
{
    uint16_t i;

    i = qorvo_build_mac_header(buf, seq, (uint16_t)dst_board_id, (uint16_t)src_board_id);

    buf[i++] = QORVO_MSG_FINAL;
    buf[i++] = src_board_id;

    buf[i++] = (uint8_t)(counter & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 8) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 16) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 24) & 0xFFU);

    memcpy(&buf[i], poll_tx_ts, 5U);
    i += 5U;

    memcpy(&buf[i], answer_rx_ts, 5U);
    i += 5U;

    return i;
}

static uint16_t qorvo_build_report_frame(uint8_t *buf,
                                         uint8_t seq,
                                         uint8_t src_board_id,
                                         uint8_t dst_board_id,
                                         uint32_t counter,
                                         const uint8_t poll_rx_ts[5],
                                         const uint8_t answer_tx_ts[5],
                                         const uint8_t final_rx_ts[5])
{
    uint16_t i;

    i = qorvo_build_mac_header(buf, seq, (uint16_t)dst_board_id, (uint16_t)src_board_id);

    buf[i++] = QORVO_MSG_REPORT;
    buf[i++] = src_board_id;

    buf[i++] = (uint8_t)(counter & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 8) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 16) & 0xFFU);
    buf[i++] = (uint8_t)((counter >> 24) & 0xFFU);

    memcpy(&buf[i], poll_rx_ts, 5U);
    i += 5U;

    memcpy(&buf[i], answer_tx_ts, 5U);
    i += 5U;

    memcpy(&buf[i], final_rx_ts, 5U);
    i += 5U;

    return i;
}

static int32_t qorvo_send_frame_immediate(const uint8_t *tx_buf,
                                          uint16_t payload_len,
                                          uint32_t starttx_mode,
                                          qorvo_tx_msg_t tx_msg,
                                          qorvo_link_state_t next_state)
{
    int32_t ret;

    ret = dwt_writetxdata(payload_len, (uint8_t *)tx_buf, 0U);
    if (ret != DWT_SUCCESS) {
        return ret;
    }

    dwt_writetxfctrl(payload_len + 2U, 0U, 1U);

    g_link.tx_busy = 1U;
    g_link.last_tx_msg = tx_msg;
    g_link.state = next_state;

    ret = dwt_starttx((uint8_t)starttx_mode);
    if (ret != DWT_SUCCESS) {
        g_link.tx_busy = 0U;
        g_link.last_tx_msg = QORVO_TXMSG_NONE;
        g_link.state = QORVO_STATE_IDLE;
        return ret;
    }

    g_link.seq++;
    return DWT_SUCCESS;
}

static int32_t qorvo_send_frame_delayed_rs(const uint8_t *tx_buf,
                                           uint16_t payload_len,
                                           uint32_t reply_delay_uus,
                                           uint32_t starttx_mode,
                                           qorvo_tx_msg_t tx_msg,
                                           qorvo_link_state_t next_state)
{
    uint32_t dx_time;
    int32_t ret;

    dx_time = US_TO_DTU(reply_delay_uus);
    dx_time >>= QORVO_DX_TIME_SHIFT;
    dwt_setdelayedtrxtime(dx_time);

    ret = dwt_writetxdata(payload_len, (uint8_t *)tx_buf, 0U);
    if (ret != DWT_SUCCESS) {
        return ret;
    }

    dwt_writetxfctrl(payload_len + 2U, 0U, 1U);

    g_link.tx_busy = 1U;
    g_link.last_tx_msg = tx_msg;
    g_link.state = next_state;

    ret = dwt_starttx((uint8_t)starttx_mode);
    if (ret != DWT_SUCCESS) {
        g_link.tx_busy = 0U;
        g_link.last_tx_msg = QORVO_TXMSG_NONE;
        g_link.state = QORVO_STATE_IDLE;
        return ret;
    }

    g_link.seq++;
    return DWT_SUCCESS;
}

static int32_t qorvo_send_poll(void)
{
    uint8_t tx_buf[QORVO_MAX_FRAME_LEN];
    uint16_t tx_len;

    tx_len = qorvo_build_poll_frame(tx_buf,
                                    g_link.seq,
                                    g_link.board_id,
                                    g_link.current_exchange_counter);

    dwt_setrxtimeout(QORVO_TWR_RX_TIMEOUT_UUS);
    dwt_setpreambledetecttimeout(0U);

    return qorvo_send_frame_immediate(tx_buf,
                                      tx_len,
                                      DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED,
                                      QORVO_TXMSG_POLL,
                                      QORVO_STATE_WAIT_ANSWER);
}

static int32_t qorvo_send_answer(uint8_t dst_board_id)
{
    uint8_t tx_buf[QORVO_MAX_FRAME_LEN];
    uint16_t tx_len;

    tx_len = qorvo_build_answer_frame(tx_buf,
                                      g_link.seq,
                                      g_link.board_id,
                                      dst_board_id,
                                      g_link.current_exchange_counter);

    dwt_setrxtimeout(QORVO_TWR_RX_TIMEOUT_UUS);
    dwt_setpreambledetecttimeout(0U);

    return qorvo_send_frame_immediate(tx_buf,
                                      tx_len,
                                      DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED,
                                      QORVO_TXMSG_ANSWER,
                                      QORVO_STATE_WAIT_FINAL);
}

static int32_t qorvo_send_final(uint8_t dst_board_id)
{
    uint8_t tx_buf[QORVO_MAX_FRAME_LEN];
    uint16_t tx_len;

    tx_len = qorvo_build_final_frame(tx_buf,
                                     g_link.seq,
                                     g_link.board_id,
                                     dst_board_id,
                                     g_link.current_exchange_counter,
                                     g_link.poll_tx_ts,
                                     g_link.answer_rx_ts);

    dwt_setrxtimeout(QORVO_TWR_RX_TIMEOUT_UUS);
    dwt_setpreambledetecttimeout(0U);

    return qorvo_send_frame_immediate(tx_buf,
                                      tx_len,
                                      DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED,
                                      QORVO_TXMSG_FINAL,
                                      QORVO_STATE_WAIT_REPORT);
}

static int32_t qorvo_send_report(uint8_t dst_board_id)
{
    uint8_t tx_buf[QORVO_MAX_FRAME_LEN];
    uint16_t tx_len;

    tx_len = qorvo_build_report_frame(tx_buf,
                                      g_link.seq,
                                      g_link.board_id,
                                      dst_board_id,
                                      g_link.current_exchange_counter,
                                      g_link.poll_rx_ts,
                                      g_link.answer_tx_ts,
                                      g_link.final_rx_ts);

    return qorvo_send_frame_immediate(tx_buf,
                                      tx_len,
                                      DWT_START_TX_IMMEDIATE,
                                      QORVO_TXMSG_REPORT,
                                      QORVO_STATE_IDLE);
}

static bool qorvo_compute_ds_twr(const uint8_t poll_tx_ts[5],
                                 const uint8_t poll_rx_ts[5],
                                 const uint8_t answer_tx_ts[5],
                                 const uint8_t answer_rx_ts[5],
                                 const uint8_t final_tx_ts[5],
                                 const uint8_t final_rx_ts[5],
                                 double *tof_dtu,
                                 double *tof_ns,
                                 double *distance_m)
{
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    uint64_t t6;

    double ra;
    double rb;
    double da;
    double db;
    double denom;
    double tof_dtu_local;

    if ((tof_dtu == NULL) || (tof_ns == NULL) || (distance_m == NULL)) {
        return false;
    }

    t1 = qorvo_ts5_to_u64(poll_tx_ts);
    t2 = qorvo_ts5_to_u64(poll_rx_ts);
    t3 = qorvo_ts5_to_u64(answer_tx_ts);
    t4 = qorvo_ts5_to_u64(answer_rx_ts);
    t5 = qorvo_ts5_to_u64(final_tx_ts);
    t6 = qorvo_ts5_to_u64(final_rx_ts);

    ra = (double)qorvo_ts_diff_u40(t4, t1);
    rb = (double)qorvo_ts_diff_u40(t6, t3);
    da = (double)qorvo_ts_diff_u40(t3, t2);
    db = (double)qorvo_ts_diff_u40(t5, t4);

    denom = ra + rb + da + db;
    if (denom <= 0.0) {
        return false;
    }

    tof_dtu_local = ((ra * rb) - (da * db)) / denom;

    if (tof_dtu_local < 0.0) {
        return false;
    }

    *tof_dtu = tof_dtu_local;
    *tof_ns = tof_dtu_local * DWT_TIME_UNITS * 1.0e9;
    *distance_m = tof_dtu_local * DWT_TIME_UNITS * QORVO_SPEED_OF_LIGHT_M_S;

    return true;
}

static double qorvo_apply_range_bias_hook(double raw_distance_m)
{
#if QORVO_ENABLE_RANGE_BIAS_HOOK
    /* Placeholder for a future empirical DWM3000 channel-9 correction. */
    return raw_distance_m;
#else
    return raw_distance_m;
#endif
}

static void qorvo_log_ds_twr_result(const uint8_t rep_poll_rx_ts[5],
                                    const uint8_t rep_answer_tx_ts[5],
                                    const uint8_t rep_final_rx_ts[5])
{
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    uint64_t t6;

    uint64_t ra_u64;
    uint64_t rb_u64;
    uint64_t da_u64;
    uint64_t db_u64;

    double tof_dtu;
    double tof_ns;
    double distance_m;
    double corrected_distance_m;

    t1 = qorvo_ts5_to_u64(g_link.poll_tx_ts);
    t2 = qorvo_ts5_to_u64(rep_poll_rx_ts);
    t3 = qorvo_ts5_to_u64(rep_answer_tx_ts);
    t4 = qorvo_ts5_to_u64(g_link.answer_rx_ts);
    t5 = qorvo_ts5_to_u64(g_link.final_tx_ts);
    t6 = qorvo_ts5_to_u64(rep_final_rx_ts);

    ra_u64 = qorvo_ts_diff_u40(t4, t1);
    rb_u64 = qorvo_ts_diff_u40(t6, t3);
    da_u64 = qorvo_ts_diff_u40(t3, t2);
    db_u64 = qorvo_ts_diff_u40(t5, t4);

    qorvo_log_printf("  ds_twr.Ra         = %lu dtu\r\n", (unsigned long long)ra_u64);
    qorvo_log_printf("  ds_twr.Rb         = %lu dtu\r\n", (unsigned long long)rb_u64);
    qorvo_log_printf("  ds_twr.Da         = %lu dtu\r\n", (unsigned long long)da_u64);
    qorvo_log_printf("  ds_twr.Db         = %lu dtu\r\n", (unsigned long long)db_u64);

    if (qorvo_compute_ds_twr(g_link.poll_tx_ts,
                             rep_poll_rx_ts,
                             rep_answer_tx_ts,
                             g_link.answer_rx_ts,
                             g_link.final_tx_ts,
                             rep_final_rx_ts,
                             &tof_dtu,
                             &tof_ns,
                             &distance_m)) {

        corrected_distance_m = qorvo_apply_range_bias_hook(distance_m);

        g_link.last_tof_dtu = tof_dtu;
        g_link.last_tof_ns = tof_ns;
        g_link.last_distance_m = distance_m;
        g_link.last_distance_corrected_m = corrected_distance_m;
        g_link.range_valid = 1U;

        qorvo_log_printf("  ds_twr.ToF        = %.3f dtu\r\n", tof_dtu);
        qorvo_log_printf("  ds_twr.ToF        = %.3f ns\r\n", tof_ns);
        qorvo_log_printf("  ds_twr.distance   = %.3f m\r\n", distance_m);
        qorvo_log_printf("  ds_twr.corrected  = %.3f m\r\n", corrected_distance_m);

#if QORVO_CAL_ANT_DELAY_MODE
        qorvo_stats_add(&g_link.cal_stats, corrected_distance_m);
#if QORVO_CAL_PRINT_EVERY_SAMPLE
        qorvo_log_printf("[CAL] sample %lu/%u -> %.4f m (known %.4f m, err %+0.2f cm)\r\n",
                         (unsigned long)g_link.cal_stats.samples,
                         (unsigned int)QORVO_CAL_SAMPLE_TARGET,
                         corrected_distance_m,
                         (double)QORVO_CAL_KNOWN_DISTANCE_M,
                         (corrected_distance_m - (double)QORVO_CAL_KNOWN_DISTANCE_M) * 100.0);
#endif
        if (g_link.cal_stats.samples >= (uint32_t)QORVO_CAL_SAMPLE_TARGET) {
            qorvo_cal_log_summary();
            qorvo_stats_reset(&g_link.cal_stats);
        }
#endif
    } else {
        g_link.range_valid = 0U;
        qorvo_log_printf("  ds_twr.compute    = FAILED\r\n");
    }
}

static void qorvo_cb_tx_done(const dwt_cb_data_t *cb_data)
{
    uint8_t tx_ts[5] = {0};
    uint64_t tx_ts_u64 = 0ULL;
    uint32_t tx_ts_lo;
    uint8_t tx_ts_hi;

    (void)cb_data;

    g_irq_tx_done_count++;
    g_link.tx_busy = 0U;

    dwt_readtxtimestamp(tx_ts);

    switch (g_link.last_tx_msg) {
    case QORVO_TXMSG_POLL:
        memcpy(g_link.poll_tx_ts, tx_ts, 5U);
        break;
    case QORVO_TXMSG_ANSWER:
        memcpy(g_link.answer_tx_ts, tx_ts, 5U);
        break;
    case QORVO_TXMSG_FINAL:
        memcpy(g_link.final_tx_ts, tx_ts, 5U);
        break;
    case QORVO_TXMSG_REPORT:
        memcpy(g_link.report_tx_ts, tx_ts, 5U);
        break;
    default:
        break;
    }

    tx_ts_u64 = qorvo_ts5_to_u64(tx_ts);
    tx_ts_lo = (uint32_t)(tx_ts_u64 & 0xFFFFFFFFULL);
    tx_ts_hi = (uint8_t)((tx_ts_u64 >> 32) & 0xFFU);

    qorvo_log_printf("[IRQ][board %u] TX done, tx_ts=0x%02X%08lX, total=%lu\r\n",
                     g_link.board_id,
                     tx_ts_hi,
                     (unsigned long)tx_ts_lo,
                     (unsigned long)g_irq_tx_done_count);

    if ((g_link.role == QORVO_ROLE_RX) && (g_link.last_tx_msg == QORVO_TXMSG_REPORT)) {
        qorvo_enter_idle_rx();
    }
}

static void qorvo_cb_rx_ok(const dwt_cb_data_t *cb_data)
{
    uint8_t rx_buf[QORVO_MAX_FRAME_LEN];
    uint16_t frame_len;
    uint8_t rx_ts[5] = {0};
    uint64_t rx_ts_u64 = 0ULL;
    uint32_t rx_ts_lo;
    uint8_t rx_ts_hi;
    uint8_t rng_bit = 0U;
    uint8_t msg_type;
    uint8_t src_board_id;
    uint32_t counter;

    g_irq_rx_ok_count++;

    frame_len = cb_data->datalength;
    if (frame_len > QORVO_MAX_FRAME_LEN) {
        frame_len = QORVO_MAX_FRAME_LEN;
    }

    memset(rx_buf, 0, sizeof(rx_buf));
    dwt_readrxdata(rx_buf, frame_len, 0U);

    (void)dwt_getframelength(&rng_bit);

    qorvo_read_rx_timestamp(rx_ts, &rx_ts_u64);
    rx_ts_lo = (uint32_t)(rx_ts_u64 & 0xFFFFFFFFULL);
    rx_ts_hi = (uint8_t)((rx_ts_u64 >> 32) & 0xFFU);

    if (frame_len < QORVO_POLL_FRAME_LEN) {
        qorvo_log_printf("[IRQ][board %u] short frame len=%u\r\n",
                         g_link.board_id,
                         frame_len);
        if (g_link.role == QORVO_ROLE_RX) {
            qorvo_enter_idle_rx();
        }
        return;
    }

    msg_type = rx_buf[QORVO_HDR_IDX_TYPE];
    src_board_id = rx_buf[QORVO_HDR_IDX_BOARD_ID];
    counter =
        ((uint32_t)rx_buf[QORVO_HDR_IDX_COUNTER + 0U]) |
        ((uint32_t)rx_buf[QORVO_HDR_IDX_COUNTER + 1U] << 8) |
        ((uint32_t)rx_buf[QORVO_HDR_IDX_COUNTER + 2U] << 16) |
        ((uint32_t)rx_buf[QORVO_HDR_IDX_COUNTER + 3U] << 24);

    if ((msg_type == QORVO_MSG_POLL) && (frame_len == QORVO_POLL_FRAME_LEN)) {
        qorvo_log_printf("[IRQ][board %u] POLL from board %u, seq=%u, cnt=%lu len=%u rng=%u rx_ts=0x%02X%08lX\r\n",
                         g_link.board_id,
                         src_board_id,
                         rx_buf[2],
                         (unsigned long)counter,
                         frame_len,
                         rng_bit,
                         rx_ts_hi,
                         (unsigned long)rx_ts_lo);

        if (g_link.role == QORVO_ROLE_RX) {
            int32_t ret;

            g_link.peer_board_id = src_board_id;
            g_link.current_exchange_counter = counter;
            g_link.rx_counter = counter;
            g_link.range_valid = 0U;

            memset(g_link.answer_tx_ts, 0, sizeof(g_link.answer_tx_ts));
            memset(g_link.final_rx_ts, 0, sizeof(g_link.final_rx_ts));
            memset(g_link.report_tx_ts, 0, sizeof(g_link.report_tx_ts));

            memcpy(g_link.poll_rx_ts, rx_ts, 5U);

            ret = qorvo_send_answer(src_board_id);
            if (ret == DWT_SUCCESS) {
                qorvo_log_printf("[ANSWER board %u] ANSWER scheduled to board %u, seq=%u cnt=%lu delay=%u uus\r\n",
                                 g_link.board_id,
                                 src_board_id,
                                 (uint8_t)(g_link.seq - 1U),
                                 (unsigned long)g_link.current_exchange_counter,
                                 (unsigned int)QORVO_REPLY_DELAY_UUS);
            } else {
                qorvo_log_printf("[ANSWER board %u] ANSWER start failed\r\n",
                                 g_link.board_id);
                qorvo_enter_idle_rx();
            }
        }
        return;
    }

    if ((msg_type == QORVO_MSG_ANSWER) && (frame_len == QORVO_ANSWER_FRAME_LEN)) {
        qorvo_log_printf("[IRQ][board %u] ANSWER from board %u, seq=%u cnt=%lu rx_ts=0x%02X%08lX\r\n",
                         g_link.board_id,
                         src_board_id,
                         rx_buf[2],
                         (unsigned long)counter,
                         rx_ts_hi,
                         (unsigned long)rx_ts_lo);

        if ((g_link.role == QORVO_ROLE_TX) &&
            (g_link.state == QORVO_STATE_WAIT_ANSWER) &&
            (counter == g_link.current_exchange_counter)) {
            int32_t ret;

            g_link.peer_board_id = src_board_id;
            memcpy(g_link.answer_rx_ts, rx_ts, 5U);

            ret = qorvo_send_final(src_board_id);
            if (ret == DWT_SUCCESS) {
                qorvo_log_printf("[FINAL board %u] FINAL scheduled to board %u, seq=%u cnt=%lu delay=%u uus\r\n",
                                 g_link.board_id,
                                 src_board_id,
                                 (uint8_t)(g_link.seq - 1U),
                                 (unsigned long)g_link.current_exchange_counter,
                                 (unsigned int)QORVO_REPLY_DELAY_UUS);
            } else {
                qorvo_log_printf("[FINAL board %u] FINAL start failed\r\n",
                                 g_link.board_id);
                g_link.state = QORVO_STATE_IDLE;
                g_link.last_tx_msg = QORVO_TXMSG_NONE;
            }
        }
        return;
    }

    if ((msg_type == QORVO_MSG_FINAL) && (frame_len == QORVO_FINAL_FRAME_LEN)) {
        qorvo_log_printf("[IRQ][board %u] FINAL from board %u, seq=%u cnt=%lu rx_ts=0x%02X%08lX\r\n",
                         g_link.board_id,
                         src_board_id,
                         rx_buf[2],
                         (unsigned long)counter,
                         rx_ts_hi,
                         (unsigned long)rx_ts_lo);

        if ((g_link.role == QORVO_ROLE_RX) &&
            (g_link.state == QORVO_STATE_WAIT_FINAL) &&
            (counter == g_link.current_exchange_counter)) {
            int32_t ret;

            memcpy(g_link.final_rx_ts, rx_ts, 5U);

            ret = qorvo_send_report(src_board_id);
            if (ret == DWT_SUCCESS) {
                qorvo_log_printf("[REPORT board %u] REPORT started to board %u, seq=%u cnt=%lu\r\n",
                                 g_link.board_id,
                                 src_board_id,
                                 (uint8_t)(g_link.seq - 1U),
                                 (unsigned long)g_link.current_exchange_counter);
            } else {
                qorvo_log_printf("[REPORT board %u] REPORT start failed\r\n",
                                 g_link.board_id);
                qorvo_enter_idle_rx();
            }
        }
        return;
    }

    if ((msg_type == QORVO_MSG_REPORT) && (frame_len == QORVO_REPORT_FRAME_LEN)) {
        uint8_t rep_poll_rx_ts[5];
        uint8_t rep_answer_tx_ts[5];
        uint8_t rep_final_rx_ts[5];
        uint64_t ts64;
        uint32_t lo;
        uint8_t hi;

        memcpy(g_link.report_rx_ts, rx_ts, 5U);
        memcpy(rep_poll_rx_ts, &rx_buf[15], 5U);
        memcpy(rep_answer_tx_ts, &rx_buf[20], 5U);
        memcpy(rep_final_rx_ts, &rx_buf[25], 5U);

        qorvo_log_printf("[IRQ][board %u] REPORT from board %u, seq=%u cnt=%lu rx_ts=0x%02X%08lX\r\n",
                         g_link.board_id,
                         src_board_id,
                         rx_buf[2],
                         (unsigned long)counter,
                         rx_ts_hi,
                         (unsigned long)rx_ts_lo);

        ts64 = qorvo_ts5_to_u64(rep_poll_rx_ts);
        lo = (uint32_t)(ts64 & 0xFFFFFFFFULL);
        hi = (uint8_t)((ts64 >> 32) & 0xFFU);
        qorvo_log_printf("  report.poll_rx_ts   = 0x%02X%08lX\r\n", hi, (unsigned long)lo);

        ts64 = qorvo_ts5_to_u64(rep_answer_tx_ts);
        lo = (uint32_t)(ts64 & 0xFFFFFFFFULL);
        hi = (uint8_t)((ts64 >> 32) & 0xFFU);
        qorvo_log_printf("  report.answer_tx_ts = 0x%02X%08lX\r\n", hi, (unsigned long)lo);

        ts64 = qorvo_ts5_to_u64(rep_final_rx_ts);
        lo = (uint32_t)(ts64 & 0xFFFFFFFFULL);
        hi = (uint8_t)((ts64 >> 32) & 0xFFU);
        qorvo_log_printf("  report.final_rx_ts  = 0x%02X%08lX\r\n", hi, (unsigned long)lo);

        if ((g_link.role == QORVO_ROLE_TX) &&
            (g_link.state == QORVO_STATE_WAIT_REPORT) &&
            (counter == g_link.current_exchange_counter)) {

            qorvo_log_ds_twr_result(rep_poll_rx_ts,
                                    rep_answer_tx_ts,
                                    rep_final_rx_ts);

            g_link.state = QORVO_STATE_IDLE;
            g_link.last_tx_msg = QORVO_TXMSG_NONE;
        }
        return;
    }

    qorvo_log_printf("[IRQ][board %u] unknown frame type=0x%02X len=%u rx_ts=0x%02X%08lX\r\n",
                     g_link.board_id,
                     msg_type,
                     frame_len,
                     rx_ts_hi,
                     (unsigned long)rx_ts_lo);

    if (g_link.role == QORVO_ROLE_RX) {
        qorvo_enter_idle_rx();
    }
}

static void qorvo_cb_rx_to(const dwt_cb_data_t *cb_data)
{
    g_irq_rx_to_count++;

    qorvo_log_printf("[IRQ][board %u] timeout, status=0x%08lX, total=%lu\r\n",
                     g_link.board_id,
                     (unsigned long)cb_data->status,
                     (unsigned long)g_irq_rx_to_count);

    if (g_link.role == QORVO_ROLE_RX) {
        qorvo_enter_idle_rx();
    } else {
        g_link.state = QORVO_STATE_IDLE;
        g_link.last_tx_msg = QORVO_TXMSG_NONE;
        g_link.tx_busy = 0U;
    }
}

static void qorvo_cb_rx_err(const dwt_cb_data_t *cb_data)
{
    g_irq_rx_err_count++;

    qorvo_log_printf("[IRQ][board %u] error, status=0x%08lX, total=%lu\r\n",
                     g_link.board_id,
                     (unsigned long)cb_data->status,
                     (unsigned long)g_irq_rx_err_count);

    if (g_link.role == QORVO_ROLE_RX) {
        qorvo_enter_idle_rx();
    } else {
        g_link.state = QORVO_STATE_IDLE;
        g_link.last_tx_msg = QORVO_TXMSG_NONE;
        g_link.tx_busy = 0U;
    }
}

static int32_t qorvo_enable_irq_driven_events(void)
{
    dwt_callbacks_s cbs;

    memset(&cbs, 0, sizeof(cbs));
    cbs.cbTxDone = qorvo_cb_tx_done;
    cbs.cbRxOk   = qorvo_cb_rx_ok;
    cbs.cbRxTo   = qorvo_cb_rx_to;
    cbs.cbRxErr  = qorvo_cb_rx_err;

    dwt_setcallbacks(&cbs);
    dwt_configureisr(0);

    dwt_setinterrupt(
        DWT_INT_TXFRS_BIT_MASK  |
        DWT_INT_RXFCG_BIT_MASK  |
        DWT_INT_RXFTO_BIT_MASK  |
        DWT_INT_RXPTO_BIT_MASK  |
        DWT_INT_RXFCE_BIT_MASK  |
        DWT_INT_RXFSL_BIT_MASK  |
        DWT_INT_RXPHE_BIT_MASK  |
        DWT_INT_RXSTO_BIT_MASK  |
        DWT_INT_ARFE_BIT_MASK   |
        DWT_INT_CIAERR_BIT_MASK |
        DWT_INT_RXOVRR_BIT_MASK,
        0U,
        DWT_ENABLE_INT_ONLY
    );

    return DWT_SUCCESS;
}

int32_t qorvo_link_init(uint8_t board_id, qorvo_role_t role)
{
    int32_t ret;

    memset(&g_link, 0, sizeof(g_link));
    g_link.board_id = board_id;
    g_link.role = role;
    g_link.seq = 0U;
    g_link.tx_counter = 0U;
    g_link.rx_counter = 0U;
    g_link.last_tx_tick = HAL_GetTick();
    g_link.initialised = 0U;
    g_link.tx_busy = 0U;
    g_link.peer_board_id = 0U;
    g_link.current_exchange_counter = 0U;
    g_link.last_tx_msg = QORVO_TXMSG_NONE;
    g_link.state = QORVO_STATE_IDLE;

    qorvo_reset_exchange_timestamps();
    qorvo_stats_reset(&g_link.cal_stats);

    qorvo_log_printf("\r\n=== Qorvo link init ===\r\n");
    qorvo_log_printf("board_id=%u, role=%s\r\n",
                     board_id,
                     (role == QORVO_ROLE_TX) ? "TX" : "RX");

    ret = qorvo_probe_and_init();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Probe/init failed: %ld\r\n", (long)ret);
        return ret;
    }

    ret = qorvo_configure_radio_default();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("Radio config failed: %ld\r\n", (long)ret);
        return ret;
    }

    dwt_forcetrxoff();
    qorvo_clear_status_events();

    ret = qorvo_enable_irq_driven_events();
    if (ret != DWT_SUCCESS) {
        qorvo_log_printf("IRQ event config failed\r\n");
        return ret;
    }

    if (role == QORVO_ROLE_RX) {
        qorvo_enter_idle_rx();
    }

    g_link.initialised = 1U;
    qorvo_log_printf("Link init OK\r\n");
    return DWT_SUCCESS;
}

void qorvo_link_process(void)
{
    uint32_t now;
    int32_t ret;

    if (g_link.initialised == 0U) {
        return;
    }

    if (g_link.role == QORVO_ROLE_TX) {
        now = HAL_GetTick();

        if ((g_link.state == QORVO_STATE_IDLE) &&
            (g_link.tx_busy == 0U) &&
            ((now - g_link.last_tx_tick) >= QORVO_TX_PERIOD_MS)) {

            g_link.last_tx_tick = now;
            g_link.current_exchange_counter = ++g_link.tx_counter;
            g_link.peer_board_id = 0U;
            qorvo_reset_exchange_timestamps();

            dwt_forcetrxoff();
            qorvo_clear_status_events();

            ret = qorvo_send_poll();
            if (ret == DWT_SUCCESS) {
                qorvo_log_printf("[POLL board %u] POLL sent seq=%u cnt=%lu len=%u\r\n",
                                 g_link.board_id,
                                 (uint8_t)(g_link.seq - 1U),
                                 (unsigned long)g_link.current_exchange_counter,
                                 (unsigned int)QORVO_POLL_PAYLOAD_LEN);
            } else {
                g_link.state = QORVO_STATE_IDLE;
                g_link.last_tx_msg = QORVO_TXMSG_NONE;
                g_link.tx_busy = 0U;
                qorvo_log_printf("[POLL board %u] POLL start failed\r\n",
                                 g_link.board_id);
            }
        }
    }
}
