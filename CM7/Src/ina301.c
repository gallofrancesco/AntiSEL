/**
 ******************************************************************************
 * @file    ina301.c
 * @brief   Driver INA301 — conversioni corrente/tensione e controllo modo.
 ******************************************************************************
 */
#include "ina301.h"
#include "main.h"
#include "dac.h"

/* Calibrazione corrente (default: A1, shunt 1 ohm, VREF 3.3 V). */
static INA301_Cal_t cal = {
    .gain_v_v = 20.0f,
    .rshunt_ohm = 1.0f,
    .adc_vref_v = 3.3f,
    .dac_vref_v = 3.3f,
};

void INA301_SetCal(const INA301_Cal_t *c) {
  if (c != NULL) {
    cal = *c;
  }
}

const INA301_Cal_t *INA301_GetCal(void) { return &cal; }

/* ── Conversioni (pure) ─────────────────────────────────────────────────── */
float INA301_AdcToVoltage(uint32_t adc_raw) {
  return ((float)adc_raw / INA301_ADC_FULL_SCALE) * cal.adc_vref_v;
}

float INA301_VoltageToCurrent(float vout) {
  float denom = cal.gain_v_v * cal.rshunt_ohm;
  return (denom > 0.0f) ? (vout / denom) : 0.0f;
}

float INA301_AdcToCurrent(uint32_t adc_raw) {
  return INA301_VoltageToCurrent(INA301_AdcToVoltage(adc_raw));
}

float INA301_CurrentToLimitVoltage(float current_a) {
  return current_a * cal.rshunt_ohm * cal.gain_v_v;
}

uint32_t INA301_LimitVoltageToDacCode(float voltage) {
  if (voltage < 0.0f) {
    voltage = 0.0f;
  }
  float code = (voltage / cal.dac_vref_v) * INA301_DAC_FULL_SCALE + 0.5f;
  if (code > INA301_DAC_FULL_SCALE) {
    code = INA301_DAC_FULL_SCALE;
  }
  return (uint32_t)code;
}

/* ── Interfaccia hardware ───────────────────────────────────────────────── */
bool INA301_IsAlertActive(void) {
  return (HAL_GPIO_ReadPin(INA301_ALERT_GPIO_Port, INA301_ALERT_Pin) ==
          GPIO_PIN_RESET);
}

void INA301_ResetLatch(void) {
  /* Impulso di reset del latch INA301: LOW poi HIGH (ri-arma in modo
   * LATCHED). Il pull basso momentaneo azzera il latch; il ritorno alto
   * ri-arma il comparatore: se la corrente e' ancora sopra soglia l'ALERT
   * si ri-asserisce (si latcha di nuovo) immediatamente.
   * Larghezza impulso via NOP (non Acq_Micros/TIM2: non ancora avviato
   * quando questa funzione e' chiamata durante l'init). */
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_RESET);
  for (volatile uint32_t i = 0; i < 500U; i++) {
    __NOP();
  }
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_SET);
}

bool INA301_ResetAndVerify(void) {
  /* Azzera il latch, attende un assestamento e verifica che ALERT sia
   * tornato inattivo (nessuna corrente sopra soglia al momento del test). */
  INA301_ResetLatch();
  for (volatile uint32_t i = 0; i < 5000U; i++) {
    __NOP();
  }
  return !INA301_IsAlertActive();
}

bool INA301_SetLimitCurrent(float current_a) {
  float vlimit = INA301_CurrentToLimitVoltage(current_a);
  if (vlimit < 0.0f || vlimit > cal.dac_vref_v) {
    return false; /* fuori range elettrico DAC */
  }
  uint32_t code = INA301_LimitVoltageToDacCode(vlimit);
  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code) != HAL_OK) {
    return false;
  }
  return true;
}
