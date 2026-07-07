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
#include "lwip/tcp.h"
#include <string.h>

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
static uint32_t last_log_10hz = 0U; /* timestamp ultimo log 10Hz */
extern struct netif gnetif;         /* dichiarato in lwip.c */

static float t_hold_ms = 1.0f;
static float t_on_ms = 1.0f;

/* Server TCP porta 7755 — AntiSEL Control Protocol */
static struct tcp_pcb *tcp_server_pcb = NULL;
static struct tcp_pcb *tcp_client_pcb = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void SystemClock_Config_480MHz(void);
static void MPU_Config(void);
static void TCP_Server_Init(void);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                             err_t err);
static void tcp_server_err(void *arg, err_t err);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
  /* Accendiamo LD2 per indicare che il sistema è avviato */
  HAL_GPIO_WritePin(LD2_YELLOW_GPIO_Port, LD2_YELLOW_Pin, GPIO_PIN_SET);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
  TCP_Server_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* ── LwIP polling ─────────────────────────────────────────────────────
     * MX_LWIP_Process() deve girare il più spesso possibile.
     * Non mettere mai HAL_Delay() in questo loop.
     * --------------------------------------------------------------------- */
    MX_LWIP_Process();

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

    /* ── LOG_10HZ (ogni 100 ms) ──────────────────────────────────────────
     * Invia periodicamente un log 10Hz al client se connesso.
     * --------------------------------------------------------------------- */
    if (HAL_GetTick() - last_log_10hz >= 100U)
    {
      last_log_10hz = HAL_GetTick();
      if (tcp_client_pcb != NULL)
      {
        char buf[64];
        snprintf(buf, sizeof(buf), "LOG_10HZ TICK=%lu\r\n", HAL_GetTick());
        if (tcp_sndbuf(tcp_client_pcb) >= strlen(buf))
        {
          tcp_write(tcp_client_pcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
          tcp_output(tcp_client_pcb);
        }
      }
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

/* Server TCP porta 7755 — AntiSEL Control Protocol */

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                             err_t err) {
  if (p == NULL) {
    tcp_client_pcb = NULL;
    tcp_close(tpcb);
    return ERR_OK;
  }
  char *data = (char *)p->payload;
  if (strncmp(data, "PING", 4) == 0)
    tcp_write(tpcb, "PONG\r\n", 6, TCP_WRITE_FLAG_COPY);
  else if (strncmp(data, "STATUS", 6) == 0)
    tcp_write(tpcb, "OK STATUS=IDLE\r\n", 16, TCP_WRITE_FLAG_COPY);
  else if (strncmp(data, "DAC_GET", 7) == 0) {
    uint32_t val = HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1);
    char buf[32];
    snprintf(buf, sizeof(buf), "DAC=%lu\r\n", val);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(data, "DAC_SET", 7) == 0) {
    uint32_t val = atoi(data + 8);
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, val);
    char buf[32];
    snprintf(buf, sizeof(buf), "DAC_SET=%lu\r\n", val);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(data, "THOLD_SET", 9) == 0) {
    t_hold_ms = atof(data + 10);
    char buf[32];
    int val_i = (int)t_hold_ms;
    int val_d = (int)(t_hold_ms * 10) % 10;
    snprintf(buf, sizeof(buf), "THOLD_SET=%d.%d\r\n", val_i, val_d);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(data, "TON_SET", 7) == 0) {
    t_on_ms = atof(data + 8);
    char buf[32];
    int val_i = (int)t_on_ms;
    int val_d = (int)(t_on_ms * 10) % 10;
    snprintf(buf, sizeof(buf), "TON_SET=%d.%d\r\n", val_i, val_d);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(data, "DUT_ON", 6) == 0) {
    /* TODO: Aggiungere il toggle del GPIO del DUT_SWITCH qui (es. HAL_GPIO_WritePin) */
    tcp_write(tpcb, "DUT_ON OK\r\n", 11, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(data, "DUT_OFF", 7) == 0) {
    /* TODO: Aggiungere il toggle del GPIO del DUT_SWITCH qui (es. HAL_GPIO_WritePin) */
    tcp_write(tpcb, "DUT_OFF OK\r\n", 12, TCP_WRITE_FLAG_COPY);
  } else
    tcp_write(tpcb, "ACK\r\n", 5, TCP_WRITE_FLAG_COPY);
  tcp_output(tpcb);
  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) { tcp_client_pcb = NULL; }

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
  if (err != ERR_OK || newpcb == NULL)
    return ERR_VAL;
  if (tcp_client_pcb != NULL) {
    tcp_abort(newpcb);
    return ERR_ABRT;
  }
  tcp_client_pcb = newpcb;
  tcp_setprio(newpcb, TCP_PRIO_MIN);
  tcp_recv(newpcb, tcp_server_recv);
  tcp_err(newpcb, tcp_server_err);
  tcp_write(newpcb, "AntiSEL v1.0\r\n", 14, TCP_WRITE_FLAG_COPY);
  tcp_output(newpcb);
  return ERR_OK;
}

static void TCP_Server_Init(void) {
  tcp_server_pcb = tcp_new();
  if (tcp_server_pcb == NULL)
    return;
  tcp_bind(tcp_server_pcb, IP_ADDR_ANY, 7755);
  tcp_server_pcb = tcp_listen_with_backlog(tcp_server_pcb, 1);
  if (tcp_server_pcb == NULL)
    return;
  tcp_accept(tcp_server_pcb, tcp_server_accept);
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
