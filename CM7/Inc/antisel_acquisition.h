/**
 ******************************************************************************
 * @file    antisel_acquisition.h
 * @brief   Acquisizione ADC/DMA circolare, timebase 1us, traccia con
 *          pre-trigger (AntiSEL, spec sezione 6/8).
 ******************************************************************************
 */
#ifndef ANTISEL_ACQUISITION_H
#define ANTISEL_ACQUISITION_H

#include <stdint.h>
#include <stdbool.h>

#define ACQ_SAMPLE_RATE_HZ 100000U /* 100 kSa/s */
#define ACQ_BUF_SIZE 4000U         /* 40 ms @ 100 kSa/s */
#define ACQ_PRE_MS 1.0f            /* pre-trigger minimo (spec sezione 6) */

/* Tag traccia */
#define ACQ_TRACE_SEL 1U
#define ACQ_TRACE_HCE 2U

void Acq_Init(void);   /* TIM2 (micros), TIM6 (trigger ADC), calibrazione ADC */
void Acq_Start(void);  /* avvia ADC in DMA circolare */
bool Acq_Running(void);

uint32_t Acq_Micros(void);   /* timebase 1 us (TIM2, 32 bit) */
uint32_t Acq_WritePos(void); /* posizione di scrittura DMA */
uint16_t Acq_Latest(void);   /* ultimo campione (con invalidate cache) */

/* Congela pre-trigger + finestra evento nel buffer secondario.
 * event_type: ACQ_TRACE_SEL / ACQ_TRACE_HCE. */
void Acq_FreezeTrace(uint8_t event_type, uint32_t thold_us, uint32_t ton_us);

/* Consumo traccia (lato protocollo) */
bool Acq_TracePending(void);
uint8_t Acq_TraceEventType(void);
uint32_t Acq_TraceLen(void);
uint16_t Acq_TraceSample(uint32_t i);
uint32_t Acq_TraceTholdUs(void);
uint32_t Acq_TraceTonUs(void);
uint32_t Acq_TraceTrigIndex(void);
uint32_t Acq_TraceStartIndex(void);
void Acq_TraceMarkSent(void);

#endif /* ANTISEL_ACQUISITION_H */
