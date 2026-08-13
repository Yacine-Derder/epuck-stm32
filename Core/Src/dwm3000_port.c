#include "dwm3000_port.h"
#include "usbd_cdc_if.h"
#include "main.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart3;

static void stm32_send_uart(const char *s)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

static void log_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    CDC_Transmit_FS((uint8_t *)buf, strlen(buf));
    stm32_send_uart(buf);
}

static void dwm3000_select(void)
{
    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_RESET);
}

static void dwm3000_deselect(void)
{
    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_SET);
}

void dwm3000_port_init(void)
{
    dwm3000_deselect();
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_SET);
}

void dwm3000_hw_reset(void)
{
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(DWM_RSTN_GPIO_Port, DWM_RSTN_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

uint32_t dwm3000_read_dev_id(void)
{
    HAL_StatusTypeDef st;
    uint8_t tx[5] = {0};
    uint8_t rx[5] = {0};

    /* Read register file 0x00 (DEV_ID) */
    tx[0] = DWM3000_DEV_ID_REG & 0x3FU;

    dwm3000_select();
    st = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), HAL_MAX_DELAY);
    dwm3000_deselect();

    if (st != HAL_OK)
    {
        log_printf("DWM3000 SPI transfer failed, HAL status = %d\r\n", (int)st);
        return 0;
    }

    /* rx[0] is the header phase, actual data is rx[1..4], LSB first */
    return ((uint32_t)rx[1]) |
           ((uint32_t)rx[2] << 8) |
           ((uint32_t)rx[3] << 16) |
           ((uint32_t)rx[4] << 24);
}

void dwm3000_basic_test(void)
{
    uint32_t dev_id;

    log_printf("\r\n=== DWM3000 basic SPI test ===\r\n");

    dwm3000_port_init();
    HAL_Delay(20);

    dwm3000_hw_reset();

    dev_id = dwm3000_read_dev_id();

    log_printf("DWM3000 DEV_ID = 0x%08lX\r\n", dev_id);

    if (dev_id == DWM3000_EXPECTED_DEV_ID)
    {
        log_printf("DWM3000 OK: SPI communication works.\r\n");
    }
    else if (dev_id == 0x00000000UL)
    {
        log_printf("DWM3000 ERROR: got 0x00000000\r\n");
        log_printf("Check power, reset, MISO, and whether the module is asleep.\r\n");
    }
    else if (dev_id == 0xFFFFFFFFUL)
    {
        log_printf("DWM3000 ERROR: got 0xFFFFFFFF\r\n");
        log_printf("Check CS wiring and whether MISO is floating.\r\n");
    }
    else
    {
        log_printf("DWM3000 ERROR: unexpected DEV_ID\r\n");
        log_printf("Expected: 0x%08lX\r\n", (uint32_t)DWM3000_EXPECTED_DEV_ID);
    }
}