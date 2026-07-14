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

void INA301_SetTransparentMode(void) {
  /* RESET basso: il comparatore segue VOUT>VLIMIT in tempo reale (spec s.4). */
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_RESET);
}

bool INA301_ResetAndVerify(void) {
  /* In transparent mode non c'e' un latch da azzerare: si garantisce RESET
   * basso, si attende un assestamento e si verifica che ALERT sia inattivo. */
  INA301_SetTransparentMode();
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
