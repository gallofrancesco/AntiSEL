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
 *  Diagnostica fault: lampeggia LD3 (rosso, PB14) con un CODICE che dice
 *  quale tipo di eccezione ha bloccato la CPU.
 *
 *    2 lampeggi  = BusFault    -> accesso a memoria/puntatore/DMA/cache non valido
 *    3 lampeggi  = UsageFault  -> istruzione illegale, accesso non allineato, div/0
 *    4 lampeggi  = MemManage   -> violazione MPU
 *    5 lampeggi  = HardFault forzato / causa non classificata
 *
 *  Sequenza: N lampeggi (~0,25 s l'uno) -> pausa lunga (~1,5 s) -> si ripete.
 *  NB: NON usa HAL_Delay (il SysTick non gira in contesto di fault): busy-wait.
 *  I registri di fault restano leggibili anche via debugger nei globali sotto.
 * ========================================================================== */
volatile uint32_t g_fault_cfsr = 0;   /* SCB->CFSR:  bit di causa del fault */
volatile uint32_t g_fault_hfsr = 0;   /* SCB->HFSR:  HardFault status */
volatile uint32_t g_fault_bfar = 0;   /* SCB->BFAR:  indirizzo del BusFault (se valido) */
volatile uint32_t g_fault_mmfar = 0;  /* SCB->MMFAR: indirizzo del MemManage (se valido) */
volatile uint32_t g_fault_pc = 0;     /* PC impilato: istruzione che ha faultato */

/* Ritardo in ms preciso, basato sul contatore cicli DWT (indipendente dal
 * costo del loop). Funziona anche in contesto di fault, senza SysTick. */
static void Fault_DelayMs(uint32_t ms)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* abilita DWT */
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;              /* abilita il contatore cicli */
  uint32_t cyc_per_ms = SystemCoreClock / 1000U;    /* 480000 @ 480 MHz */
  while (ms--) {
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cyc_per_ms) { __NOP(); }
  }
}

#define LED_ON()  (GPIOB->BSRR = (1U << 14))
#define LED_OFF() (GPIOB->BSRR = (1U << (14 + 16)))

/* Pattern (si ripete all'infinito):
 *   1 lampeggio LUNGO (1,5 s acceso) = MARCATORE di inizio conteggio
 *   pausa 0,8 s
 *   N lampeggi CORTI (0,3 s) = codice del fault
 *   pausa LUNGA 3 s
 * Conta SOLO i lampeggi corti dopo quello lungo. */
static void Fault_BlinkCode(int n)
{
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;                 /* clock GPIOB */
  GPIOB->MODER = (GPIOB->MODER & ~(3U << (14U * 2U)))  /* PB14 = output */
               | (1U << (14U * 2U));
  for (;;) {
    LED_ON();  Fault_DelayMs(1500);   /* marcatore lungo */
    LED_OFF(); Fault_DelayMs(800);
    for (int i = 0; i < n; i++) {
      LED_ON();  Fault_DelayMs(300);
      LED_OFF(); Fault_DelayMs(300);
    }
    Fault_DelayMs(3000);              /* pausa lunga prima di ripetere */
  }
}

/* Legge CFSR/HFSR e sceglie il codice. In caso di fault "forzato" (bus/usage/
 * mem escalati a HardFault) i bit di CFSR restano validi. Non ritorna mai. */
static void Fault_Report(void)
{
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  g_fault_bfar = SCB->BFAR;    /* valido solo se CFSR bit 15 (BFARVALID) = 1 */
  g_fault_mmfar = SCB->MMFAR;  /* valido solo se CFSR bit 7  (MMARVALID) = 1 */
  /* PC impilato dall'eccezione (r0..r3,r12,lr,PC,xPSR): la CPU usa MSP.
   * Cerca all'indietro nello stack il primo indirizzo in area FLASH. */
  {
    uint32_t *sp = (uint32_t *)__get_MSP();
    for (int i = 0; i < 64; i++) {
      uint32_t v = sp[i];
      if (v >= 0x08000000U && v < 0x08200000U) { g_fault_pc = v; break; }
    }
  }
  int code;
  if      (g_fault_cfsr & 0xFFFF0000U) { code = 3; }  /* UsageFault */
  else if (g_fault_cfsr & 0x0000FF00U) { code = 2; }  /* BusFault */
  else if (g_fault_cfsr & 0x000000FFU) { code = 4; }  /* MemManage */
  else                                 { code = 5; }  /* forzato/altro */
  Fault_BlinkCode(code);
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
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  Fault_Report();   /* lampeggia LD3 rosso col codice del fault - non ritorna */
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  Fault_Report();
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  Fault_Report();
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  Fault_Report();
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
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
