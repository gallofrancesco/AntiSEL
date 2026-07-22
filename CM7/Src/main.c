/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body — AntiSEL NUCLEO-H755ZI-Q
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
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "lwip.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ethernetif.h" /* per ethernet_link_check_state() e gnetif */
#include "lwip/netif.h"
#include "antisel.h"          /* macchina a stati a 11 stati */
#include "antisel_protocol.h" /* server TCP + telemetria */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DUAL_CORE_BOOT_SYNC_SEQUENCE

#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U)
#endif
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t last_blink = 0U;    /* timestamp ultimo toggle LED    */
static uint32_t last_link_chk = 0U; /* timestamp ultimo check ETH link */
extern struct netif gnetif;         /* dichiarato in lwip.c */
/* Logica AntiSEL, acquisizione, protocollo ed eventi sono nei moduli
 * antisel*, ina301, antisel_acquisition, antisel_protocol, antisel_storage. */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void SystemClock_Config_480MHz(void);
static void MPU_Config(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* La logica di acquisizione, conversione, protezione e protocollo e' ora nei
 * moduli antisel*. main.c si limita al bring-up HAL e a delegare. */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  MPU_Config();
  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif
/* USER CODE END Boot_Mode_Sequence_0 */

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0))
    ;
  if (timeout < 0) {
    Error_Handler();
  }
#endif
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  /* NOTA: chiamare la variante 480 MHz, non SystemClock_Config() (75 MHz).
   * Questa riga e' fuori dai blocchi USER CODE: una "Generate Code" da
   * CubeMX la resetta a SystemClock_Config() senza avvisare -- ripristinare
   * SystemClock_Config_480MHz() a mano dopo ogni rigenerazione. */
  SystemClock_Config_480MHz();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  __HAL_RCC_HSEM_CLK_ENABLE();
  HAL_HSEM_FastTake(HSEM_ID_0);
  HAL_HSEM_Release(HSEM_ID_0, 0);
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0))
    ;
  if (timeout < 0) {
    Error_Handler();
  }
#endif
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_LWIP_Init();
  MX_DAC1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  /* Bring-up AntiSEL: config, ina301, acquisizione (TIM2/TIM6/ADC/DMA),
   * calibrazione ADC, DAC, modo transparent -> stato INIT. */
  AntiSel_Init();
  /* Server TCP (porta 7755) + telemetria */
  Proto_Init();

  /* LD2 acceso: sistema avviato */
  HAL_GPIO_WritePin(LD2_YELLOW_GPIO_Port, LD2_YELLOW_Pin, GPIO_PIN_SET);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* ── Macchina a stati AntiSEL a 11 stati (non bloccante) ────────────── */
    AntiSel_Task();

    /* ── LwIP polling ─────────────────────────────────────────────────────
     * MX_LWIP_Process() deve girare il più spesso possibile.
     * Non mettere mai HAL_Delay() in questo loop.
     * --------------------------------------------------------------------- */
    MX_LWIP_Process();

    /* ── Telemetria: log 10 Hz + invio traccia evento ──────────────────── */
    Proto_Service();

    /* ── ETH link check ogni 100 ms ───────────────────────────────────────
     * Verifica se il link Ethernet è salito/sceso e aggiorna LwIP.
     * --------------------------------------------------------------------- */
    if (HAL_GetTick() - last_link_chk >= 100U) {
      last_link_chk = HAL_GetTick();
      ethernet_link_check_state(&gnetif);

      /* Indicatore link: LD3 rosso = link DOWN, LD2 giallo = link UP */
      if (netif_is_link_up(&gnetif)) {
        HAL_GPIO_WritePin(LD2_YELLOW_GPIO_Port, LD2_YELLOW_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LD3_RED_GPIO_Port, LD3_RED_Pin, GPIO_PIN_RESET);
      } else {
        HAL_GPIO_WritePin(LD2_YELLOW_GPIO_Port, LD2_YELLOW_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LD3_RED_GPIO_Port, LD3_RED_Pin, GPIO_PIN_SET);
      }
    }

    /* ── Heartbeat LED verde ogni 500 ms ──────────────────────────────────
     * LD1 verde lampeggia per indicare che il firmware gira.
     * --------------------------------------------------------------------- */
    if (HAL_GetTick() - last_blink >= 500U) {
      last_blink = HAL_GetTick();
      HAL_GPIO_TogglePin(LD1_GREEN_GPIO_Port, LD1_GREEN_Pin);
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 18;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
  RCC_OscInitStruct.PLL.PLLFRACN = 6144;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
 * @brief  EXTI line detection callbacks.
 * @param  GPIO_Pin: Specifies the pins connected EXTI line
 * @retval None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == INA301_ALERT_Pin) {
    AntiSel_OnAlertISR(); /* ISR minima: flag + timestamp (spec ALARM) */
  }
}

/**
 * @brief  SystemClock a 480 MHz — protetta dalla rigenerazione CubeMX.
 *         HSE = 8 MHz (MCO ST-LINK, BYPASS), PLL1: M=4 N=480 P=2
 *         SYSCLK = 480 MHz, AHB = 240 MHz, APBx = 120 MHz
 *         Voltage Scale 0 richiesto per 480 MHz su STM32H755.
 *         Flash Latency 4 WS obbligatoria a 480 MHz.
 * @retval None
 */
void SystemClock_Config_480MHz(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
  }

  /* HSE = MCO del ST-LINK: al power-on a freddo l'uscita del probe puo'
   * non essere ancora stabile nell'istante in cui la PLL ci si aggancia
   * (HSERDY in bypass non verifica la qualita' del segnale). Margine
   * prima dell'aggancio + un paio di tentativi come rete di sicurezza. */
  HAL_Delay(50);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 480;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 10;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  {
    uint32_t attempts_left = 3;
    HAL_StatusTypeDef osc_status;
    do {
      osc_status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
      if (osc_status == HAL_OK) {
        break;
      }
      HAL_Delay(50);
    } while (--attempts_left > 0);
    if (osc_status != HAL_OK) {
      Error_Handler();
    }
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief  Configura MPU — RAM_D2 non cacheable per DMA Ethernet
 *         Deve essere chiamata PRIMA di SCB_EnableDCache()
 */
static void MPU_Config(void) {
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  /* RAM_D2: 0x30000000, 256KB — Device, non cacheable
   * Usata da: DMA Ethernet descriptors + LwIP RX pool */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  /* Accendi LD3 rosso per segnalare errore */
  HAL_GPIO_WritePin(LD3_RED_GPIO_Port, LD3_RED_Pin, GPIO_PIN_SET);
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
