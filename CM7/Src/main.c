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
#include <stdio.h>
#include <stdlib.h>
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

/* ── Parametri acquisizione (R-06) ──────────────────────────────────────────
 * ADC1 triggherato da TIM6 TRGO a 100 kSa/s (10 µs/campione).
 * Buffer circolare da 40 ms: copre pre-trigger (>=1 ms) + T_HOLD max (10 ms)
 * + T_ON max (10 ms) con ampio margine.
 * -------------------------------------------------------------------------- */
#define ADC_SAMPLE_RATE_HZ 100000U
#define ADC_BUF_SIZE 4000U          /* 40 ms @ 100 kSa/s */
#define TRACE_PRE_MS 1.0f           /* pre-trigger richiesto da spec §6.1 */
#define SEL_RETRY_MAX 3U
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
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void SystemClock_Config_480MHz(void);
static void MPU_Config(void);
static void MicroTimebase_Init(void);
static void AdcTrigTimer_Init(void);
static void ADC_ConfigTimerTriggered(void);
static void ADC_Restart(void);
static void Trace_Freeze(uint8_t event_type);
static void TCP_Server_Init(void);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                             err_t err);
static void tcp_server_err(void *arg, err_t err);
static void handle_command(struct tcp_pcb *tpcb, char *line);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Timebase a 1 µs (R-03/R-04: risoluzione richiesta <= 100 µs).
 * TIM2 è a 32 bit: wrap ogni ~71 min, gestito con aritmetica unsigned. */
static inline uint32_t micros(void) { return TIM2->CNT; }

static inline uint8_t Is_Alert_Active(void) {
  return (HAL_GPIO_ReadPin(INA301_ALERT_GPIO_Port, INA301_ALERT_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void MicroTimebase_Init(void) {
  __HAL_RCC_TIM2_CLK_ENABLE();
  /* APB1 prescaler = /2 => clock timer = 2 x PCLK1 */
  uint32_t tim_clk = HAL_RCC_GetPCLK1Freq() * 2U;
  TIM2->PSC = (tim_clk / 1000000U) - 1U; /* 1 MHz */
  TIM2->ARR = 0xFFFFFFFFU;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 = TIM_CR1_CEN;
}

/* TIM6 genera TRGO (update) a 100 kHz per il pacing dell'ADC (R-06). */
static void AdcTrigTimer_Init(void) {
  __HAL_RCC_TIM6_CLK_ENABLE();
  uint32_t tim_clk = HAL_RCC_GetPCLK1Freq() * 2U;
  TIM6->PSC = 0U;
  TIM6->ARR = (tim_clk / ADC_SAMPLE_RATE_HZ) - 1U;
  TIM6->CR2 = TIM_CR2_MMS_1; /* MMS = 010: update -> TRGO */
  TIM6->EGR = TIM_EGR_UG;
  TIM6->CR1 = TIM_CR1_CEN;
}

/* Riconfigura l'ADC1 (inizializzato da CubeMX in free-run) in modalità
 * timer-triggered: un campione per ogni TRGO di TIM6, DMA circolare.
 * Fatto qui (e non in adc.c) per sopravvivere alla rigenerazione CubeMX. */
static void ADC_ConfigTimerTriggered(void) {
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  /* Sampling time più lungo: a 100 kSa/s c'è ampio margine e migliora
   * l'accuratezza a 16 bit (73 cicli ADC ~ 1.9 µs << 10 µs) */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }
}

static void ADC_Restart(void) {
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, ADC_BUF_SIZE);
  adc_running = 1;
}

/* Posizione di scrittura corrente del DMA nel buffer circolare */
static inline uint32_t ADC_WritePos(void) {
  return (ADC_BUF_SIZE - __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle)) % ADC_BUF_SIZE;
}

/* Ultimo campione ADC (corrente istantanea, 16 bit) scritto dal DMA. */
static inline uint16_t Adc_Latest(void) {
  if (!adc_running) return 0U;
  uint32_t idx = (ADC_WritePos() + ADC_BUF_SIZE - 1U) % ADC_BUF_SIZE;
  uint32_t addr = ((uint32_t)&adc_buffer[idx]) & ~31U;
  SCB_InvalidateDCache_by_Addr((uint32_t *)addr, 32);
  return adc_buffer[idx];
}

/* Soglia in conteggi ADC (16 bit) equivalente al V_LIMIT del DAC (12 bit):
 * stesso dominio di tensione -> thr = dac * 65535 / 4095. */
static inline uint32_t Threshold_Adc(void) {
  uint32_t dac = HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1);
  return (uint32_t)(((uint64_t)dac * 65535U) / 4095U);
}

/* In modo LATCHED l'ALERT resta basso: la discriminazione HCE/SEL usa la
 * corrente reale letta dall'ADC. */
static inline uint8_t Current_Over_Threshold(void) {
  return (Adc_Latest() > Threshold_Adc()) ? 1U : 0U;
}

/* Impulso di reset del latch INA301: LOW ~5 us poi HIGH (ri-arma latched). */
static void Ina301_ResetLatch(void) {
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_RESET);
  uint32_t t0 = micros();
  while ((uint32_t)(micros() - t0) < 5U) { }
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_SET);
}

/* Congela la traccia: ferma il DMA e calcola la finestra da inviare
 * (pre-trigger + T_HOLD [+ T_ON per i SEL], spec §6.1).
 * event_type: 1 = SEL, 2 = HCE. Se una traccia è già in invio (DMA fermo)
 * la protezione resta attiva ma la nuova traccia viene persa. */
static void Trace_Freeze(uint8_t event_type) {
  if (!adc_running || trace_is_sending || flag_send_trace != 0) {
    return; /* best-effort: non sovrascrivere una traccia in corso */
  }
  uint32_t write_pos = ADC_WritePos();
  /* D-Cache attiva: invalida prima di leggere dati scritti dal DMA */
  SCB_InvalidateDCache_by_Addr((uint32_t *)adc_buffer, sizeof(adc_buffer));

  trace_thold_ms = t_hold_ms;
  trace_ton_ms = (event_type == 1) ? t_on_ms : 0.0f;
  float win_ms = TRACE_PRE_MS + trace_thold_ms + trace_ton_ms;
  trace_len = (uint32_t)(win_ms * (ADC_SAMPLE_RATE_HZ / 1000.0f)) + 8U;
  if (trace_len > ADC_BUF_SIZE) {
    trace_len = ADC_BUF_SIZE;
  }
  trace_start_index = (write_pos + ADC_BUF_SIZE - trace_len) % ADC_BUF_SIZE;

  /* Copia i campioni nel buffer secondario per congelarli senza fermare il DMA */
  for (uint32_t i = 0; i < trace_len; i++) {
    uint32_t idx = (trace_start_index + i) % ADC_BUF_SIZE;
    trace_copy_buf[i] = adc_buffer[idx];
  }

  flag_send_trace = event_type;
}
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
  /* Timebase 1 µs per T_HOLD/T_ON (R-03/R-04, risoluzione <= 100 µs) */
  MicroTimebase_Init();
  /* ADC a 100 kSa/s triggherato da TIM6 (R-06) */
  AdcTrigTimer_Init();
  ADC_ConfigTimerTriggered();
  /* Avvia DMA dell'ADC per registrazione continua in background */
  ADC_Restart();

  /* Accendiamo LD2 per indicare che il sistema è avviato */
  HAL_GPIO_WritePin(LD2_YELLOW_GPIO_Port, LD2_YELLOW_Pin, GPIO_PIN_SET);
  /* Chiudiamo lo switch DUT all'avvio */
  HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);

  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
  Ina301_ResetLatch();   /* latch pulito all'avvio */
  TCP_Server_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* ── Macchina a stati AntiSEL (Polling non bloccante, timebase 1 µs) ── */
    if (antisel_state == STATE_THOLD) {
      if ((uint32_t)(micros() - antisel_t0_us) >=
          (uint32_t)(t_hold_ms * 1000.0f)) {
        /* Fine T_HOLD: in modo LATCHED l'ALERT resta basso -> discriminazione
         * HCE/SEL sulla corrente reale (ADC), non sul pin. */
        if (Current_Over_Threshold()) {
          /* SEL: corrente ancora oltre soglia -> sgancia il DUT. Conta una
           * sola volta (misura cross-section).
           * NB: il DMA continua a girare per catturare anche il T_ON (R-06) */
          sel_count++;
          HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin,
                            GPIO_PIN_RESET);
          antisel_state = STATE_TON;
          antisel_t0_us = micros();
        } else {
          /* HCE: corrente rientrata entro il T_HOLD. Nessuno sgancio. */
          hce_count++;
          sel_retry_count = 0;
          Trace_Freeze(2);        /* 2 = HCE */
          Ina301_ResetLatch();    /* sblocca il latch dell'INA301 */
          recover_is_sel = 0U;
          antisel_state = STATE_COOLDOWN;
          cooldown_high_t0 = HAL_GetTick();
        }
      }
    } else if (antisel_state == STATE_TON) {
      if ((uint32_t)(micros() - antisel_t0_us) >=
          (uint32_t)(t_on_ms * 1000.0f)) {
        /* Fine T_ON: la traccia copre pre + T_HOLD + T_ON. Riaccendi il DUT e
         * sblocca il latch, poi verifica il recupero (finestra T_CLEAR). */
        Trace_Freeze(1);          /* 1 = SEL */
        HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);
        Ina301_ResetLatch();
        recover_is_sel = 1U;
        antisel_state = STATE_COOLDOWN;
        cooldown_high_t0 = HAL_GetTick();
      }
    } else if (antisel_state == STATE_COOLDOWN) {
      /* Verifica recupero. Dopo un SEL: se la corrente torna sopra soglia entro
       * T_CLEAR -> ri-latch (recupero fallito) -> nuovo power-cycle; dopo N
       * ri-scatti consecutivi -> PERMANENT_OFF. Se resta pulito per T_CLEAR ->
       * recupero riuscito, azzera i tentativi e riarma l'EXTI. */
      if (recover_is_sel && Current_Over_Threshold()) {
        sel_retry_count++;
        HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_RESET);
        if (sel_retry_count >= sel_retry_max) {
          antisel_state = STATE_PERMANENT_OFF;   /* corto persistente */
        } else {
          antisel_state = STATE_TON;             /* nuovo power-cycle */
          antisel_t0_us = micros();
        }
      } else if (HAL_GetTick() - cooldown_high_t0 >= t_clear_ms) {
        sel_retry_count = 0;                      /* recupero riuscito */
        Ina301_ResetLatch();
        antisel_state = STATE_IDLE;
        __HAL_GPIO_EXTI_CLEAR_IT(INA301_ALERT_Pin);
        __DSB();
        (void)EXTI->PR1; /* Dummy read to flush the write buffer */
        HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
      }
    }

    /* ── LwIP polling ─────────────────────────────────────────────────────
     * MX_LWIP_Process() deve girare il più spesso possibile.
     * Non mettere mai HAL_Delay() in questo loop.
     * --------------------------------------------------------------------- */
    MX_LWIP_Process();

    /* ── Invio Traccia DMA ────────────────────────────────────────────────
     * Header con metadati (R-06): tipo evento, sample rate, n. campioni,
     * parametri configurati e tick. Poi righe "indice,valore".
     * --------------------------------------------------------------------- */
    if (flag_send_trace > 0 && !trace_is_sending) {
      uint8_t event_type = flag_send_trace;
      flag_send_trace = 0;

      if (tcp_client_pcb != NULL) {
        char hdr[128];
        int hl = snprintf(hdr, sizeof(hdr),
                          "TRACE_START %s FS=%lu N=%lu THOLD_MS=%d.%d "
                          "TON_MS=%d.%d DAC=%lu TICK=%lu\r\n",
                          (event_type == 1) ? "SEL" : "HCE",
                          (unsigned long)ADC_SAMPLE_RATE_HZ,
                          (unsigned long)trace_len, (int)trace_thold_ms,
                          (int)(trace_thold_ms * 10.0f) % 10, (int)trace_ton_ms,
                          (int)(trace_ton_ms * 10.0f) % 10,
                          (unsigned long)HAL_DAC_GetValue(&hdac1,
                                                          DAC_CHANNEL_1),
                          (unsigned long)HAL_GetTick());
        tcp_write(tcp_client_pcb, hdr, hl, TCP_WRITE_FLAG_COPY);
        tcp_output(tcp_client_pcb);
        trace_send_index = 0;
        trace_is_sending = 1;
      }
    }

    if (trace_is_sending) {
      if (tcp_client_pcb != NULL) {
        /* Controlliamo se c'è spazio sufficiente nel buffer TCP (max ~480 byte per 40 campioni)
         * prima di formattare la stringa, per evitare loop intensivi di snprintf. */
        if (tcp_sndbuf(tcp_client_pcb) >= 480U) {
          char buf[640] = {0};
          int len = 0;

          for (int i = 0; i < 40 && trace_send_index < trace_len; i++) {
            int remaining = sizeof(buf) - len;
            if (remaining <= 0) {
              break;
            }
            int written = snprintf(buf + len, remaining, "%lu,%u\r\n",
                                   (unsigned long)trace_send_index, trace_copy_buf[trace_send_index]);
            if (written > 0 && written < remaining) {
              len += written;
            } else {
              break;
            }
            trace_send_index++;
          }

          if (len > 0) {
            tcp_write(tcp_client_pcb, buf, len, TCP_WRITE_FLAG_COPY);
            tcp_output(tcp_client_pcb);
          }
        }

        if (trace_send_index >= trace_len) {
          /* Invia il terminatore traccia se c'è spazio sufficiente */
          if (tcp_sndbuf(tcp_client_pcb) >= 11U) {
            tcp_write(tcp_client_pcb, "TRACE_END\r\n", 11, TCP_WRITE_FLAG_COPY);
            tcp_output(tcp_client_pcb);
            trace_is_sending = 0;
          }
        }
      } else {
        /* Disconnesso improvvisamente */
        trace_is_sending = 0;
      }
    }

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
    /* È scattato l'allarme (o hai premuto il bottone blu!) */
    if (antisel_state == STATE_IDLE) {
      /* Disabilita temporaneamente l'interruzione EXTI per evitare rimbalzi o oscillazioni */
      HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

      antisel_state = STATE_THOLD;
      antisel_t0_us = micros();
    }
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

/* Server TCP porta 7755 — AntiSEL Control Protocol */

/**
 * @brief  Esegue un singolo comando (una riga, senza CR/LF).
 */
static void handle_command(struct tcp_pcb *tpcb, char *line) {
  char buf[96];

  if (strncmp(line, "PING", 4) == 0) {
    tcp_write(tpcb, "PONG\r\n", 6, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "STATUS", 6) == 0) {
    snprintf(buf, sizeof(buf),
             "OK STATUS=%s RETRY=%d SEL=%lu HCE=%lu TH=%u\r\n",
             state_names[antisel_state], (int)sel_retry_count,
             (unsigned long)sel_count, (unsigned long)hce_count, th_selected);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "DAC_GET", 7) == 0) {
    uint32_t val = HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1);
    snprintf(buf, sizeof(buf), "DAC=%lu\r\n", val);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "DAC_SET", 7) == 0) {
    uint32_t val = (uint32_t)atoi(line + 8);
    if (val > 4095U) val = 4095U;
    th_selected = 0; /* impostazione manuale: nessuna preset attiva */
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, val);
    snprintf(buf, sizeof(buf), "DAC_SET=%lu\r\n", val);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "THOLD_SET", 9) == 0) {
    float v = (float)atof(line + 10);
    if (v < 1.0f) v = 1.0f;
    if (v > 1000.0f) v = 1000.0f;
    t_hold_ms = v;
    snprintf(buf, sizeof(buf), "THOLD_SET=%d.%d\r\n", (int)t_hold_ms,
             (int)(t_hold_ms * 10.0f) % 10);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TON_SET", 7) == 0) {
    float v = (float)atof(line + 8);
    if (v < 1.0f) v = 1.0f;
    if (v > 1000.0f) v = 1000.0f;
    t_on_ms = v;
    snprintf(buf, sizeof(buf), "TON_SET=%d.%d\r\n", (int)t_on_ms,
             (int)(t_on_ms * 10.0f) % 10);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TH_LOAD", 7) == 0) {
    /* TH_LOAD <1|2|3> <counts> — carica una soglia preset (spec §8.2) */
    int n = 0;
    unsigned long v = 0;
    if (sscanf(line + 7, "%d %lu", &n, &v) == 2 && n >= 1 && n <= 3) {
      if (v > 4095UL) v = 4095UL;
      th_preset[n - 1] = (uint32_t)v;
      snprintf(buf, sizeof(buf), "TH_LOAD %d=%lu OK\r\n", n, v);
    } else {
      snprintf(buf, sizeof(buf), "TH_LOAD ERR\r\n");
    }
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TH_SELECT", 9) == 0) {
    /* TH_SELECT <1|2|3> — applica la soglia preset al DAC */
    int n = atoi(line + 10);
    if (n >= 1 && n <= 3) {
      th_selected = (uint8_t)n;
      HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R,
                       th_preset[n - 1]);
      snprintf(buf, sizeof(buf), "TH_SELECT=%d DAC=%lu\r\n", n,
               (unsigned long)th_preset[n - 1]);
    } else {
      snprintf(buf, sizeof(buf), "TH_SELECT ERR\r\n");
    }
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TH_GET", 6) == 0) {
    snprintf(buf, sizeof(buf), "TH 1=%lu 2=%lu 3=%lu SEL=%u\r\n",
             (unsigned long)th_preset[0], (unsigned long)th_preset[1],
             (unsigned long)th_preset[2], th_selected);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "DUT_ON", 6) == 0) {
    /* Override manuale (R-07): riabilita ANCHE la protezione, altrimenti
     * dopo un DUT_OFF lo stato resterebbe PERMANENT_OFF e l'EXTI (che arma
     * solo da IDLE) non proteggerebbe più il DUT. */
    sel_retry_count = 0;
    antisel_state = STATE_IDLE;
    HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);
    Ina301_ResetLatch();
    __HAL_GPIO_EXTI_CLEAR_IT(INA301_ALERT_Pin);
    __DSB();
    (void)EXTI->PR1; /* Dummy read to flush the write buffer */
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    tcp_write(tpcb, "DUT_ON OK\r\n", 11, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "DUT_OFF", 7) == 0) {
    HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_RESET);
    antisel_state = STATE_PERMANENT_OFF;
    tcp_write(tpcb, "DUT_OFF OK\r\n", 12, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "RESET", 5) == 0) {
    sel_retry_count = 0;
    sel_count = 0;
    hce_count = 0;
    antisel_state = STATE_IDLE;
    HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);
    Ina301_ResetLatch();
    __HAL_GPIO_EXTI_CLEAR_IT(INA301_ALERT_Pin);
    __DSB();
    (void)EXTI->PR1; /* Dummy read to flush the write buffer */
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    tcp_write(tpcb, "RESET OK\r\n", 10, TCP_WRITE_FLAG_COPY);

  } else if (strncmp(line, "INA_RST", 7) == 0) {
    Ina301_ResetLatch();
    tcp_write(tpcb, "INA_RST OK\r\n", 12, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "RETRY_SET", 9) == 0) {
    int n = atoi(line + 10);
    if (n < 1) n = 1;
    if (n > 100) n = 100;
    sel_retry_max = (uint32_t)n;
    snprintf(buf, sizeof(buf), "RETRY_SET=%lu\r\n", (unsigned long)sel_retry_max);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TCLEAR_SET", 10) == 0) {
    int n = atoi(line + 11);
    if (n < 1) n = 1;
    if (n > 10000) n = 10000;
    t_clear_ms = (uint32_t)n;
    snprintf(buf, sizeof(buf), "TCLEAR_SET=%lu\r\n", (unsigned long)t_clear_ms);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else {
    tcp_write(tpcb, "ACK\r\n", 5, TCP_WRITE_FLAG_COPY);
  }
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                             err_t err) {
  /* Parsing line-based: gestisce comandi multipli nello stesso segmento
   * e comandi frammentati su più segmenti (es. flood di DAC_SET) */
  static char line_buf[64];
  static uint8_t line_len = 0;

  if (p == NULL) {
    tcp_client_pcb = NULL;
    line_len = 0;
    tcp_close(tpcb);
    return ERR_OK;
  }

  for (struct pbuf *q = p; q != NULL; q = q->next) {
    const char *d = (const char *)q->payload;
    for (u16_t i = 0; i < q->len; i++) {
      char c = d[i];
      if (c == '\n' || c == '\r') {
        if (line_len > 0) {
          line_buf[line_len] = '\0';
          handle_command(tpcb, line_buf);
          line_len = 0;
        }
      } else if (line_len < sizeof(line_buf) - 1) {
        line_buf[line_len++] = c;
      } else {
        line_len = 0; /* riga troppo lunga: scartata */
      }
    }
  }

  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) { tcp_client_pcb = NULL; }

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
  if (err != ERR_OK || newpcb == NULL)
    return ERR_VAL;
  if (tcp_client_pcb != NULL) {
    /* Abortiamo la VECCHIA connessione "fantasma" per far posto alla nuova */
    tcp_abort(tcp_client_pcb);
    tcp_client_pcb = NULL;
  }
  tcp_client_pcb = newpcb;
  tcp_setprio(newpcb, TCP_PRIO_MIN);
  tcp_recv(newpcb, tcp_server_recv);
  tcp_err(newpcb, tcp_server_err);
  /* NON chiamare tcp_output o tcp_write qui dentro! Causa memory corruption in LwIP */
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
