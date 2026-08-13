#include "qorvo_platform.h"

extern SPI_HandleTypeDef hspi1;

static void qorvo_spi_set_prescaler(uint32_t prescaler)
{
    if (hspi1.Init.BaudRatePrescaler == prescaler) {
        return;
    }

    HAL_SPI_DeInit(&hspi1);
    hspi1.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(&hspi1);
}

void qorvo_spi_set_slowrate(void)
{
    /* Safe bring-up speed: APB2(84 MHz) / 64 = 1.3125 MHz */
    qorvo_spi_set_prescaler(SPI_BAUDRATEPRESCALER_64);
}

void qorvo_spi_set_fastrate(void)
{
    /* Keep same rate for now.
       Later you can safely move this to /8 or /4 after validation. */
    qorvo_spi_set_prescaler(SPI_BAUDRATEPRESCALER_64);
}

int32_t writetospiwithcrc(uint16_t headerLength,
                          const uint8_t *headerBuffer,
                          uint16_t bodyLength,
                          const uint8_t *bodyBuffer,
                          uint8_t crc8)
{
    HAL_StatusTypeDef st = HAL_OK;
    decaIrqStatus_t irq_state = decamutexon();

    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_RESET);

    st = HAL_SPI_Transmit(&hspi1, (uint8_t *)headerBuffer, headerLength, HAL_MAX_DELAY);

    if ((st == HAL_OK) && (bodyLength > 0U)) {
        st = HAL_SPI_Transmit(&hspi1, (uint8_t *)bodyBuffer, bodyLength, HAL_MAX_DELAY);
    }

    if (st == HAL_OK) {
        st = HAL_SPI_Transmit(&hspi1, &crc8, 1, HAL_MAX_DELAY);
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_SET);

    decamutexoff(irq_state);
    return (st == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}

int32_t writetospi(uint16_t headerLength,
                   const uint8_t *headerBuffer,
                   uint16_t bodyLength,
                   const uint8_t *bodyBuffer)
{
    HAL_StatusTypeDef st = HAL_OK;
    decaIrqStatus_t irq_state = decamutexon();

    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_RESET);

    st = HAL_SPI_Transmit(&hspi1, (uint8_t *)headerBuffer, headerLength, HAL_MAX_DELAY);

    if ((st == HAL_OK) && (bodyLength > 0U)) {
        st = HAL_SPI_Transmit(&hspi1, (uint8_t *)bodyBuffer, bodyLength, HAL_MAX_DELAY);
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_SET);

    decamutexoff(irq_state);
    return (st == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}

int32_t readfromspi(uint16_t headerLength,
                    uint8_t *headerBuffer,
                    uint16_t readlength,
                    uint8_t *readBuffer)
{
    HAL_StatusTypeDef st = HAL_OK;
    decaIrqStatus_t irq_state = decamutexon();

    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_RESET);

    st = HAL_SPI_Transmit(&hspi1, headerBuffer, headerLength, HAL_MAX_DELAY);

    if (st == HAL_OK) {
        /* Same pattern as the F429 reference: header first, then clock data out. */
        st = HAL_SPI_Receive(&hspi1, readBuffer, readlength, HAL_MAX_DELAY);
    }

    HAL_GPIO_WritePin(SPI1_NCS_GPIO_Port, SPI1_NCS_Pin, GPIO_PIN_SET);

    decamutexoff(irq_state);
    return (st == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}