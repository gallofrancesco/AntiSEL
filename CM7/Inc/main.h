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
#include "stm32h7xx_hal.h"

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
#define INA301_ALERT_Pin GPIO_PIN_13
#define INA301_ALERT_GPIO_Port GPIOE
#define INA301_ALERT_EXTI_IRQn EXTI15_10_IRQn
#define INA301_RST_Pin GPIO_PIN_14
#define INA301_RST_GPIO_Port GPIOE
#define INA301_OUT_Pin GPIO_PIN_3
#define INA301_OUT_GPIO_Port GPIOA
#define LD1_GREEN_Pin GPIO_PIN_0
#define LD1_GREEN_GPIO_Port GPIOB
#define LD3_RED_Pin GPIO_PIN_14
#define LD3_RED_GPIO_Port GPIOB
#define DUT_SWITCH_Pin GPIO_PIN_14
#define DUT_SWITCH_GPIO_Port GPIOG
#define LD2_YELLOW_Pin GPIO_PIN_1
#define LD2_YELLOW_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
/* Record di fault salvato in DTCM (0x20000000): sopravvive al reset software.
 * Al boot main.c lo rileva e invia i registri alla GUI come riga "FAULT ...". */
typedef struct {
  uint32_t magic;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t bfar;
  uint32_t mmfar;
  uint32_t pc;
  uint32_t lr;    /* LR impilato = indirizzo di ritorno del chiamante */
  uint32_t stk[4];/* primi 4 indirizzi FLASH trovati sullo stack = call chain */
} FaultRecord_t;
#define FAULT_MAGIC   0xFA017ED0U
#define FAULT_REC     (*(volatile FaultRecord_t *)0x20000000U)
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
