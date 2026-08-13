/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_ST_Pin GPIO_PIN_13
#define LED_ST_GPIO_Port GPIOC
#define SPI1_NCS_Pin GPIO_PIN_4
#define SPI1_NCS_GPIO_Port GPIOA
#define DWM_IRQ_Pin GPIO_PIN_0
#define DWM_IRQ_GPIO_Port GPIOB
#define DWM_IRQ_EXTI_IRQn EXTI0_IRQn
#define DWM_RSTN_Pin GPIO_PIN_1
#define DWM_RSTN_GPIO_Port GPIOB
#define ST_ESP_TX_Pin GPIO_PIN_10
#define ST_ESP_TX_GPIO_Port GPIOB
#define ST_ESP_RX_Pin GPIO_PIN_11
#define ST_ESP_RX_GPIO_Port GPIOB
#define ESP_EN_Pin GPIO_PIN_12
#define ESP_EN_GPIO_Port GPIOB
#define ESP_IRQ_WAKE_Pin GPIO_PIN_13
#define ESP_IRQ_WAKE_GPIO_Port GPIOB
#define ST_HOST_REQ_Pin GPIO_PIN_14
#define ST_HOST_REQ_GPIO_Port GPIOB
#define DWM_WAKEUP_Pin GPIO_PIN_9
#define DWM_WAKEUP_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
