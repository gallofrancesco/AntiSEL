/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ==========================================================================
 *  Diagnostica fault: salva i registri di fault in DTCM (0x20000000, che
 *  sopravvive al reset software) e RIAVVIA la scheda. Dopo il riavvio, main.c
 *  rileva il record e invia una riga "FAULT ..." alla GUI (log verde).
 *  Definizioni FaultRecord_t / FAULT_MAGIC / FAULT_REC in main.h.
 * ========================================================================== */
/* 'frame' punta al frame impilato dall'eccezione: [r0,r1,r2,r3,r12,lr,PC,xPSR].
 * frame[6] e' quindi il PC dell'istruzione che ha causato il fault. */
__attribute__((used)) void Fault_Report(uint32_t *frame)
{
  FAULT_REC.cfsr  = SCB->CFSR;
  FAULT_REC.hfsr  = SCB->HFSR;
  FAULT_REC.bfar  = SCB->BFAR;    /* indirizzo BusFault  (valido se CFSR bit15=1) */
  FAULT_REC.mmfar = SCB->MMFAR;   /* indirizzo MemManage (valido se CFSR bit7=1) */
  FAULT_REC.pc    = (frame != 0) ? frame[6] : 0U;  /* PC reale dell'eccezione */
  FAULT_REC.lr    = (frame != 0) ? frame[5] : 0U;  /* LR = ritorno al chiamante */
  /* Scansiona lo stack sopra il frame per i primi 4 indirizzi in FLASH:
   * ricostruisce la catena di ritorno (utile quando PC/LR sono in RAM). */
  {
    int found = 0;
    if (frame != 0) {
      for (int i = 8; i < 300 && found < 4; i++) {
        /* non leggere oltre la fine dello stack (RAM_D1 finisce a 0x24080000) */
        if ((uint32_t)&frame[i] >= 0x2407FFF0U) { break; }
        uint32_t v = frame[i];
        if (v >= 0x08000000U && v < 0x08200000U) { FAULT_REC.stk[found++] = v; }
      }
    }
    for (; found < 4; found++) { FAULT_REC.stk[found] = 0U; }
  }
  FAULT_REC.magic = FAULT_MAGIC;
  __DSB();

  /* Conferma visiva: lampeggia LD3 (rosso, PB14) ~6 volte per segnalare che il
   * fault handler e' stato eseguito, POI resetta. Se vedi il rosso lampeggiare,
   * il crash e' passato di qui e il record e' salvato. */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
  GPIOB->MODER = (GPIOB->MODER & ~(3U << 28)) | (1U << 28);  /* PB14 = output */
  for (int b = 0; b < 6; b++) {
    GPIOB->BSRR = (1U << 14);                                /* LED ON  */
    for (volatile uint32_t k = 0; k < 12000000U; k++) { __NOP(); }
    GPIOB->BSRR = (1U << (14 + 16));                         /* LED OFF */
    for (volatile uint32_t k = 0; k < 12000000U; k++) { __NOP(); }
  }

  NVIC_SystemReset();             /* riavvia: al boot main.c invia i dati alla GUI */
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  /* Naked: nessun prologo, cosi' MSP/PSP puntano ancora al frame dell'eccezione.
   * Passa il frame in r0 e salta a Fault_Report (che salva i registri e resetta). */
  __asm volatile (
      "tst lr, #4       \n"   /* EXC_RETURN bit2: 0 = MSP, 1 = PSP */
      "ite eq           \n"
      "mrseq r0, msp    \n"
      "mrsne r0, psp    \n"
      "b Fault_Report   \n"
  );
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile (
      "tst lr, #4       \n"
      "ite eq           \n"
      "mrseq r0, msp    \n"
      "mrsne r0, psp    \n"
      "b Fault_Report   \n"
  );
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile (
      "tst lr, #4       \n"
      "ite eq           \n"
      "mrseq r0, msp    \n"
      "mrsne r0, psp    \n"
      "b Fault_Report   \n"
  );
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile (
      "tst lr, #4       \n"
      "ite eq           \n"
      "mrseq r0, msp    \n"
      "mrsne r0, psp    \n"
      "b Fault_Report   \n"
  );
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */

  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles EXTI line[15:10] interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI15_10_IRQn 0 */

  /* USER CODE END EXTI15_10_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(INA301_ALERT_Pin);
  /* USER CODE BEGIN EXTI15_10_IRQn 1 */

  /* USER CODE END EXTI15_10_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
