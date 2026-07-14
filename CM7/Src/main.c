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
<<<<<<< HEAD
/* Logica AntiSEL, acquisizione, protocollo ed eventi sono nei moduli
 * antisel*, ina301, antisel_acquisition, antisel_protocol, antisel_storage. */
=======

static float t_hold_ms = 1.0f;
static float t_on_ms = 1.0f;

/* Macchina a Stati AntiSEL (Latch Mode) */
typedef enum { STATE_IDLE = 0, STATE_THOLD, STATE_TON, STATE_PERMANENT_OFF, STATE_COOLDOWN } AntiSEL_State_t;

static volatile AntiSEL_State_t antisel_state = STATE_IDLE;
static volatile uint32_t antisel_t0_us = 0;  /* riferimento TIM2 @ 1 MHz */
static volatile uint8_t flag_send_trace = 0; /* 1 = SEL, 2 = HCE */
static volatile uint8_t sel_retry_count = 0;
static uint32_t sel_count = 0; /* contatore eventi SEL (spec §5.2) */
static uint32_t hce_count = 0; /* contatore eventi HCE (spec §5.2) */

/* Soglie precaricate (spec §8.2): 3 valori DAC caricati prima del run
 * e selezionabili da PC senza interventi hardware */
static uint32_t th_preset[3] = {2048U, 2048U, 2048U};
static uint8_t th_selected = 0; /* 0 = nessuna preset attiva, 1..3 */

/* Allineato a 32 byte per la cache-maintenance (D-Cache attiva + DMA) */
__attribute__((aligned(32))) static uint16_t adc_buffer[ADC_BUF_SIZE];
static uint16_t trace_copy_buf[ADC_BUF_SIZE];
static volatile uint8_t adc_running = 0;

static uint32_t trace_len = 0;         /* n. campioni da inviare */
static uint32_t trace_send_index = 0;
static uint8_t  trace_is_sending = 0;

/* Post-mortem: record di fault recuperato dal DTCM dopo un crash+reset, da
 * inviare alla GUI come riga "FAULT ..." appena il client TCP e' connesso. */
static uint8_t  fault_pending    = 0;   /* c'e' un record di fault da inviare */
static uint32_t fault_snap[10]   = {0};
static uint32_t fault_last_send  = 0;
static uint8_t  fault_send_count = 0;
static uint32_t trace_start_index = 0;
static float    trace_thold_ms = 0.0f; /* snapshot parametri al freeze */
static float    trace_ton_ms = 0.0f;
static uint32_t cooldown_high_t0 = 0;

/* Policy di riarmo (latched): ri-scatti CONSECUTIVI dopo il power-cycle,
 * azzerati su recupero pulito. */
static uint32_t sel_retry_max  = SEL_RETRY_MAX; /* N riarmi consecutivi max */
static uint32_t t_clear_ms     = 30U;           /* finestra "pulito" (ms) */
static uint8_t  recover_is_sel = 0U;            /* 1 = cooldown dopo SEL */

/* Server TCP porta 7755 — AntiSEL Control Protocol */
static struct tcp_pcb *tcp_server_pcb = NULL;
static struct tcp_pcb *tcp_client_pcb = NULL;

static const char *state_names[] = {"IDLE", "THOLD", "TON", "PERMANENT_OFF", "COOLDOWN"};
>>>>>>> 1f91280fc8a3a7f189ecc5880e5174edca1650f3
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
  SystemClock_Config();
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
<<<<<<< HEAD
=======
  /* Chiudiamo lo switch DUT all'avvio */
  HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);

  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
  Ina301_ResetLatch();   /* latch pulito all'avvio */
  TCP_Server_Init();

  /* Recupero del record di fault dopo un crash+reset (salvato in DTCM dal
   * fault handler). Se presente, verra' inviato alla GUI nel loop principale. */
  if (FAULT_REC.magic == FAULT_MAGIC) {
    fault_pending  = 1;
    fault_snap[0]  = FAULT_REC.cfsr;
    fault_snap[1]  = FAULT_REC.hfsr;
    fault_snap[2]  = FAULT_REC.bfar;
    fault_snap[3]  = FAULT_REC.mmfar;
    fault_snap[4]  = FAULT_REC.pc;
    fault_snap[5]  = FAULT_REC.lr;
    fault_snap[6]  = FAULT_REC.stk[0];
    fault_snap[7]  = FAULT_REC.stk[1];
    fault_snap[8]  = FAULT_REC.stk[2];
    fault_snap[9]  = FAULT_REC.stk[3];
    FAULT_REC.magic = 0U;   /* consuma il record: non ripetere ai reset futuri */
  }
>>>>>>> 1f91280fc8a3a7f189ecc5880e5174edca1650f3
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

<<<<<<< HEAD
=======
    /* ── LOG_10HZ (ogni 100 ms, R-08) ─────────────────────────────────────
     * Invia l'ULTIMO campione scritto dal DMA (non un campione arbitrario)
     * più stato, retry e contatori SEL/HCE. FRESH=0 se il DMA è fermo
     * (traccia in invio) e il dato è quindi stantio.
     * --------------------------------------------------------------------- */
    if (HAL_GetTick() - last_log_10hz >= 100U) {
      last_log_10hz = HAL_GetTick();
      if (tcp_client_pcb != NULL) {
        char buf[128];
        uint16_t adc_val;
        uint8_t fresh = adc_running;
        if (adc_running) {
          uint32_t idx = (ADC_WritePos() + ADC_BUF_SIZE - 1U) % ADC_BUF_SIZE;
          /* Invalida la sola cache line che contiene il campione */
          uint32_t addr = ((uint32_t)&adc_buffer[idx]) & ~31U;
          SCB_InvalidateDCache_by_Addr((uint32_t *)addr, 32);
          adc_val = adc_buffer[idx];
        } else {
          adc_val = adc_buffer[0];
        }
        snprintf(buf, sizeof(buf),
                 "LOG_10HZ TICK=%lu I=%u FRESH=%u STATE=%d RETRY=%d SEL=%lu "
                 "HCE=%lu\r\n",
                 HAL_GetTick(), adc_val, fresh, (int)antisel_state,
                 (int)sel_retry_count, (unsigned long)sel_count,
                 (unsigned long)hce_count);
        if (tcp_sndbuf(tcp_client_pcb) >= strlen(buf)) {
          tcp_write(tcp_client_pcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
          tcp_output(tcp_client_pcb);
        }
      }
    }

    /* ── Invio del record di fault alla GUI dopo un crash ─────────────────
     * Dopo un crash la scheda si e' resettata e ha salvato i registri in DTCM.
     * Qui li mandiamo alla GUI (log verde) una volta al secondo, ripetuti ~20
     * volte, cosi' li vedi anche se riconnetti la GUI con calma.
     * --------------------------------------------------------------------- */
    if (fault_pending && tcp_client_pcb != NULL &&
        (HAL_GetTick() - fault_last_send >= 1000U)) {
      fault_last_send = HAL_GetTick();
      char fb[240];
      int fl = snprintf(fb, sizeof(fb),
          "FAULT HFSR=0x%08lX CFSR=0x%08lX BFAR=0x%08lX PC=0x%08lX LR=0x%08lX "
          "STK=%08lX,%08lX,%08lX,%08lX\r\n",
          (unsigned long)fault_snap[1], (unsigned long)fault_snap[0],
          (unsigned long)fault_snap[2],
          (unsigned long)fault_snap[4], (unsigned long)fault_snap[5],
          (unsigned long)fault_snap[6], (unsigned long)fault_snap[7],
          (unsigned long)fault_snap[8], (unsigned long)fault_snap[9]);
      if (fl > 0 && tcp_sndbuf(tcp_client_pcb) >= (uint16_t)fl) {
        tcp_write(tcp_client_pcb, fb, fl, TCP_WRITE_FLAG_COPY);
        tcp_output(tcp_client_pcb);
        if (++fault_send_count >= 20U) { fault_pending = 0; }
      }
    }

>>>>>>> 1f91280fc8a3a7f189ecc5880e5174edca1650f3
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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
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
