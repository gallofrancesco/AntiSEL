/**
 ******************************************************************************
 * @file    ina301.h
 * @brief   Driver INA301 (current-shunt monitor) — conversioni e modo
 *          transparent/latched. AntiSEL, spec sezione 3/4/9.
 ******************************************************************************
 */
#ifndef INA301_H
#define INA301_H

#include <stdint.h>
#include <stdbool.h>

/* Fondo scala convertitori della Nucleo-H755 */
#define INA301_ADC_FULL_SCALE 65535.0f /* ADC 16 bit */
#define INA301_DAC_FULL_SCALE 4095.0f  /* DAC 12 bit */

/* Calibrazione elettrica del front-end (impostata a runtime via Ethernet). */
typedef struct {
  float gain_v_v;   /* 20 / 50 / 100  (INA301 A1/A2/A3) */
  float rshunt_ohm; /* resistenza di shunt */
  float adc_vref_v; /* riferimento ADC */
  float dac_vref_v; /* riferimento DAC */
} INA301_Cal_t;

/* Calibrazione ------------------------------------------------------------ */
void INA301_SetCal(const INA301_Cal_t *cal);
const INA301_Cal_t *INA301_GetCal(void);

/* Conversioni (pure, testabili) — spec sezione 9 ------------------------- */
float INA301_AdcToVoltage(uint32_t adc_raw);
float INA301_VoltageToCurrent(float vout);
float INA301_AdcToCurrent(uint32_t adc_raw);
float INA301_CurrentToLimitVoltage(float current_a);
uint32_t INA301_LimitVoltageToDacCode(float voltage);

/* Interfaccia hardware ---------------------------------------------------- */
bool INA301_IsAlertActive(void);   /* pin ALERT basso = allarme attivo */
void INA301_ResetLatch(void); /* impulso RESET basso poi alto: azzera e ri-arma il latch */
bool INA301_ResetAndVerify(void);  /* impulso RESET (diag latched) + verifica */

/* Programma la soglia VLIMIT sul DAC a partire dalla corrente [A].
 * Ritorna false (e non applica) se VLIMIT esce dal range elettrico DAC. */
bool INA301_SetLimitCurrent(float current_a);

#endif /* INA301_H */
