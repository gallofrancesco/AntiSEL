/**
 ******************************************************************************
 * @file    antisel.h
 * @brief   Macchina a stati AntiSEL a 11 stati (spec sezione 6) e config.
 ******************************************************************************
 */
#ifndef ANTISEL_H
#define ANTISEL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  ANTISEL_STATE_INIT = 0,
  ANTISEL_STATE_IDLE,
  ANTISEL_STATE_ALARM,
  ANTISEL_STATE_HOLD_RUN,
  ANTISEL_STATE_HCE_SAVE,
  ANTISEL_STATE_CUTOFF,
  ANTISEL_STATE_TON_RUN,
  ANTISEL_STATE_RECOVERY,
  ANTISEL_STATE_VERIFY,
  ANTISEL_STATE_MANUAL_OFF,
  ANTISEL_STATE_FAULT
} AntiSelState_t;

/* Config parametrica centralizzata (spec sezione 10). */
typedef struct {
  float ina_gain_v_v;
  float rshunt_ohm;
  float adc_vref_v;
  float dac_vref_v;
  float current_threshold_a; /* soglia I_TH in ampere */
  uint32_t t_hold_us;
  uint32_t t_on_us;
  uint32_t recovery_settle_us;
  uint32_t max_recovery_retries;
  uint32_t t_clear_ms; /* finestra post-evento / "pulito" */
} AntiSelConfig_t;

typedef enum {
  ASEL_OK = 0,
  ASEL_ERR_FAULT,
  ASEL_ERR_SEL_ACTIVE,
  ASEL_ERR_BUSY,
  ASEL_ERR_ALERT_LOW,
  ASEL_ERR_RANGE,
  ASEL_ERR_NOT_IN_FAULT
} AntiSelResult_t;

void AntiSel_Init(void);       /* bring-up periferiche + stato INIT */
void AntiSel_Task(void);       /* tick non bloccante della FSM */
void AntiSel_OnAlertISR(void); /* chiamata dalla EXTI callback */

/* Comandi (priorita' spec sezione 7) */
AntiSelResult_t AntiSel_ManualOff(void); /* override OFF, massima priorita' */
AntiSelResult_t AntiSel_ManualOn(void);  /* non scavalca SEL/FAULT/ALERT */
AntiSelResult_t AntiSel_AckFault(void);
AntiSelResult_t AntiSel_ResetSystem(void); /* azzera contatori e riarma */

/* Setter config (validano; ASEL_OK oppure ASEL_ERR_RANGE) */
AntiSelResult_t AntiSel_SetGain(float v);
AntiSelResult_t AntiSel_SetRshunt(float v);
AntiSelResult_t AntiSel_SetVrefAdc(float v);
AntiSelResult_t AntiSel_SetVrefDac(float v);
AntiSelResult_t AntiSel_SetThresholdMa(float ma);
AntiSelResult_t AntiSel_SetTholdUs(uint32_t us);
AntiSelResult_t AntiSel_SetTonUs(uint32_t us);
AntiSelResult_t AntiSel_SetRetryMax(uint32_t n);
AntiSelResult_t AntiSel_SetTclearMs(uint32_t ms);

/* Getter */
AntiSelState_t AntiSel_GetState(void);
const char *AntiSel_StateName(AntiSelState_t s);
const AntiSelConfig_t *AntiSel_GetConfig(void);
uint32_t AntiSel_RetryCount(void);
uint8_t AntiSel_OverrideActive(void);
uint8_t AntiSel_SwitchOn(void);
uint32_t AntiSel_LastDac(void);

#endif /* ANTISEL_H */
