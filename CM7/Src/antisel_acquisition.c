/**
 ******************************************************************************
 * @file    antisel_acquisition.c
 * @brief   ADC 100 kSa/s (TIM6 TRGO + DMA circolare), timebase 1 us (TIM2),
 *          buffer circolare e congelamento traccia con pre-trigger.
 ******************************************************************************
 */
#include "antisel_acquisition.h"
#include "main.h"
#include "adc.h"

/* Buffer DMA allineato a 32 byte (cache-line) per la manutenzione D-Cache.
 * Collocato in .bss -> RAM_D1 (AXI SRAM), accessibile da DMA1. */
__attribute__((aligned(32))) static uint16_t adc_buffer[ACQ_BUF_SIZE];
static uint16_t trace_copy_buf[ACQ_BUF_SIZE];
static volatile uint8_t adc_running;

/* Stato traccia congelata */
static volatile uint8_t trace_pending;
static uint8_t tr_event_type;
static uint32_t trace_len;
static uint32_t trace_start_index;
static uint32_t tr_thold_us;
static uint32_t tr_ton_us;
static uint32_t tr_trig_index;

/* ── Timebase e trigger ─────────────────────────────────────────────────── */
static void micro_timebase_init(void) {
  __HAL_RCC_TIM2_CLK_ENABLE();
  uint32_t tim_clk = HAL_RCC_GetPCLK1Freq() * 2U; /* APB1 timer clock */
  TIM2->PSC = (tim_clk / 1000000U) - 1U;          /* 1 MHz */
  TIM2->ARR = 0xFFFFFFFFU;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 = TIM_CR1_CEN;
}

static void adc_trig_timer_init(void) {
  __HAL_RCC_TIM6_CLK_ENABLE();
  uint32_t tim_clk = HAL_RCC_GetPCLK1Freq() * 2U;
  TIM6->PSC = 0U;
  TIM6->ARR = (tim_clk / ACQ_SAMPLE_RATE_HZ) - 1U;
  TIM6->CR2 = TIM_CR2_MMS_1; /* MMS=010: update -> TRGO */
  TIM6->EGR = TIM_EGR_UG;
  TIM6->CR1 = TIM_CR1_CEN;
}

void Acq_Init(void) {
  micro_timebase_init();
  adc_trig_timer_init();
  /* Calibrazione ADC (spec INIT). L'ADC e' gia' configurato timer-triggered +
   * DMA circolare da MX_ADC1_Init (.ioc aggiornato). */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
}

void Acq_Start(void) {
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, ACQ_BUF_SIZE) == HAL_OK) {
    adc_running = 1U;
  }
}

bool Acq_Running(void) { return adc_running != 0U; }

uint32_t Acq_Micros(void) { return TIM2->CNT; }

uint32_t Acq_WritePos(void) {
  return (ACQ_BUF_SIZE - __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle)) % ACQ_BUF_SIZE;
}

uint16_t Acq_Latest(void) {
  if (!adc_running) {
    return 0U;
  }
  uint32_t idx = (Acq_WritePos() + ACQ_BUF_SIZE - 1U) % ACQ_BUF_SIZE;
  uint32_t addr = ((uint32_t)&adc_buffer[idx]) & ~31U;
  SCB_InvalidateDCache_by_Addr((uint32_t *)addr, 32);
  return adc_buffer[idx];
}

void Acq_FreezeTrace(uint8_t event_type, uint32_t thold_us, uint32_t ton_us) {
  if (!adc_running || trace_pending) {
    return; /* best-effort: non sovrascrivere una traccia in corso */
  }
  uint32_t write_pos = Acq_WritePos();
  /* D-Cache attiva: invalida prima di leggere i dati scritti dal DMA */
  SCB_InvalidateDCache_by_Addr((uint32_t *)adc_buffer, sizeof(adc_buffer));

  tr_thold_us = thold_us;
  tr_ton_us = (event_type == ACQ_TRACE_SEL) ? ton_us : 0U;
  float win_ms = ACQ_PRE_MS + (thold_us / 1000.0f) + (tr_ton_us / 1000.0f);
  trace_len = (uint32_t)(win_ms * (ACQ_SAMPLE_RATE_HZ / 1000.0f)) + 8U;
  if (trace_len > ACQ_BUF_SIZE) {
    trace_len = ACQ_BUF_SIZE;
  }
  trace_start_index = (write_pos + ACQ_BUF_SIZE - trace_len) % ACQ_BUF_SIZE;
  for (uint32_t i = 0; i < trace_len; i++) {
    trace_copy_buf[i] = adc_buffer[(trace_start_index + i) % ACQ_BUF_SIZE];
  }
  tr_trig_index = (uint32_t)(ACQ_PRE_MS * ACQ_SAMPLE_RATE_HZ / 1000.0f);
  tr_event_type = event_type;
  trace_pending = 1U;
}

bool Acq_TracePending(void) { return trace_pending != 0U; }
uint8_t Acq_TraceEventType(void) { return tr_event_type; }
uint32_t Acq_TraceLen(void) { return trace_len; }
uint16_t Acq_TraceSample(uint32_t i) {
  return (i < trace_len) ? trace_copy_buf[i] : 0U;
}
uint32_t Acq_TraceTholdUs(void) { return tr_thold_us; }
uint32_t Acq_TraceTonUs(void) { return tr_ton_us; }
uint32_t Acq_TraceTrigIndex(void) { return tr_trig_index; }
uint32_t Acq_TraceStartIndex(void) { return trace_start_index; }
void Acq_TraceMarkSent(void) { trace_pending = 0U; }
