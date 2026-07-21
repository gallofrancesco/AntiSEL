/**
 ******************************************************************************
 * @file    antisel.c
 * @brief   Macchina a stati AntiSEL a 11 stati (modo LATCHED).
 *
 *  INA301 in modo latched: l'ALERT resta basso finche' non si pulsa RST
 *  (INA301_ResetLatch), quindi la discriminazione HCE/SEL non puo' piu'
 *  basarsi sul ritorno alto di ALERT durante T_HOLD ma sulla corrente ADC
 *  reale campionata a fine T_HOLD.
 *
 *  Percorso protezione:  ALERT (EXTI) -> ALARM -> HOLD_RUN
 *    - a fine T_HOLD corrente sotto soglia -> HCE_SAVE -> IDLE  (nessun power-cycle)
 *    - a fine T_HOLD corrente sopra soglia -> CUTOFF -> TON_RUN -> RECOVERY ->
 *      VERIFY -> IDLE  (oppure FAULT con retry finiti)
 ******************************************************************************
 */
#include "antisel.h"
#include "ina301.h"
#include "antisel_acquisition.h"
#include "antisel_storage.h"
#include "main.h"
#include "dac.h"
#include <string.h>

/* Flag diagnostici (diagnostic_flags del record) */
#define DIAG_ALERT_STUCK_LOW 0x01U
/* Limite di re-iniezioni consecutive in exti_rearm_or_trigger(): oltre
 * questa soglia una linea ALERT che non si libera mai non e' piu' trattata
 * come una sequenza di eventi genuini ma come guasto bloccato -> FAULT,
 * per evitare un loop stretto senza fine (visto in pratica: migliaia di
 * HCE_SAVE in pochi minuti per rumore/bounce sulla linea). */
#define EXTI_REARM_RETRIGGER_LIMIT 5U
#define DIAG_ADC_SAT 0x02U
#define DIAG_RECOVERY_FAIL 0x04U

#define ADC_SAT_THRESHOLD 64000U /* vicino al fondo scala 16 bit */

/* ── Stato modulo ───────────────────────────────────────────────────────── */
static volatile AntiSelState_t state;
static volatile uint8_t st_entry; /* 1 al primo tick di uno stato */
static AntiSelConfig_t cfg;

static volatile uint8_t alert_flag;    /* set dall'ISR ALERT */
static volatile uint32_t alarm_t0_us;  /* timestamp ISR */
static uint32_t t0_us;                 /* base T_HOLD / T_ON */
static uint32_t rec_t0_us;             /* base settle RECOVERY */
static uint32_t ver_t0_us;             /* base settle VERIFY */
static uint32_t save_t0_ms;            /* base finestra post-evento HCE */
static uint32_t retry_count;
static uint32_t diag_flags;
static float peak_current_a;
static float current_at_trigger_a;
static uint8_t fault_cause; /* AntiSelEventType_t */

/* ── Helper HW ──────────────────────────────────────────────────────────── */
static void switch_on(void) {
  HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);
}
static void switch_off(void) {
  HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_RESET);
}
static void exti_rearm(void) {
  __HAL_GPIO_EXTI_CLEAR_IT(INA301_ALERT_Pin);
  __DSB();
  (void)EXTI->PR1; /* flush write buffer */
  HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}
static void exti_disable(void) { HAL_NVIC_DisableIRQ(EXTI15_10_IRQn); }
static void go(AntiSelState_t s); /* forward decl: usata da exti_rearm_or_trigger */

/* Riarma l'interrupt sul fronte di discesa, ma se ALERT e' gia' asserto
 * basso in questo istante nessun nuovo fronte verra' mai generato (race
 * edge-vs-livello: la linea era gia' bassa al momento del riarmo, es. eventi
 * ravvicinati nell'auto-cycle). In quel caso si inietta manualmente la
 * condizione di allarme come se l'ISR fosse scattata, per non restare
 * bloccati in IDLE a tempo indeterminato con ALERT attivo e non rilevato.
 * Se questo si ripete troppe volte di fila (linea davvero bloccata/rumorosa,
 * non un evento genuino) si evita il loop infinito passando a FAULT, con lo
 * stesso schema gia' usato per i retry di RECOVERY/VERIFY.
 * Ritorna true se il chiamante deve procedere verso IDLE, false se e' gia'
 * stato reindirizzato internamente a FAULT. */
static bool exti_rearm_or_trigger(void) {
  static uint32_t retrig_count;
  exti_rearm();
  if (!INA301_IsAlertActive()) {
    retrig_count = 0U;
    return true;
  }
  if (++retrig_count >= EXTI_REARM_RETRIGGER_LIMIT) {
    retrig_count = 0U;
    exti_disable();
    diag_flags |= DIAG_ALERT_STUCK_LOW;
    fault_cause = (uint8_t)EVENT_ALERT_STUCK_LOW;
    Storage_UpdateLastEvent(2U, diag_flags);
    go(ANTISEL_STATE_FAULT);
    return false;
  }
  exti_disable();
  alarm_t0_us = Acq_Micros();
  alert_flag = 1U;
  return true;
}

static void go(AntiSelState_t s) {
  state = s;
  st_entry = 1U;
}

/* Applica la calibrazione elettrica a ina301 e riprogramma il DAC. */
static void apply_cal(void) {
  INA301_Cal_t c = {cfg.ina_gain_v_v, cfg.rshunt_ohm, cfg.adc_vref_v,
                    cfg.dac_vref_v};
  INA301_SetCal(&c);
  INA301_SetLimitCurrent(cfg.current_threshold_a);
}

/* Costruisce e registra un record evento (spec sezione 12). */
static void add_event(uint8_t type, uint32_t extra_diag) {
  AntiSelEvent_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.timestamp_us = (uint64_t)Acq_Micros();
  ev.pretrigger_start_index = Acq_TraceStartIndex();
  ev.trigger_index = Acq_TraceTrigIndex();
  ev.event_end_index = Acq_TraceLen();
  ev.threshold_a = cfg.current_threshold_a;
  ev.t_hold_us = cfg.t_hold_us;
  ev.t_on_us = (type == (uint8_t)EVENT_SEL) ? cfg.t_on_us : 0U;
  ev.peak_current_a = peak_current_a;
  ev.current_at_trigger_a = current_at_trigger_a;
  ev.event_type = type;
  ev.recovery_result = 0U;
  ev.override_active = (state == ANTISEL_STATE_MANUAL_OFF) ? 1U : 0U;
  ev.diagnostic_flags = diag_flags | extra_diag;
  Storage_AddEvent(&ev);
}

/* ── Init ───────────────────────────────────────────────────────────────── */
void AntiSel_Init(void) {
  Storage_Init();

  cfg.ina_gain_v_v = 20.0f;
  cfg.rshunt_ohm = 1.0f;
  cfg.adc_vref_v = 3.3f;
  cfg.dac_vref_v = 3.3f;
  cfg.current_threshold_a = 0.010f; /* 10 mA */
  cfg.t_hold_us = 5000U;
  cfg.t_on_us = 2000U;
  cfg.recovery_settle_us = 2000U;
  cfg.max_recovery_retries = 3U;
  cfg.t_clear_ms = 30U;

  retry_count = 0U;
  diag_flags = 0U;
  alert_flag = 0U;

  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  apply_cal();            /* ina301 cal + programma il DAC */
  INA301_ResetLatch();    /* latch pulito all'avvio (modo LATCHED) */
  switch_off();           /* DUT spento fino ai controlli iniziali */

  Acq_Init();  /* TIM2/TIM6 + calibrazione ADC */
  Acq_Start(); /* DMA circolare */

  state = ANTISEL_STATE_INIT;
  st_entry = 1U;
}

/* ── ISR ALERT (fronte di discesa) ─────────────────────────────────────── */
void AntiSel_OnAlertISR(void) {
  if (state == ANTISEL_STATE_IDLE) {
    alarm_t0_us = Acq_Micros();
    alert_flag = 1U;
    exti_disable(); /* debounce: riarmato al ritorno in IDLE */
  }
}

/* ── FSM ────────────────────────────────────────────────────────────────── */
void AntiSel_Task(void) {
  uint8_t first = st_entry;
  st_entry = 0U;

  switch (state) {
  case ANTISEL_STATE_INIT:
    /* Controlli iniziali completati in AntiSel_Init: abilita DUT e passa IDLE */
    switch_on();
    if (exti_rearm_or_trigger()) {
      go(ANTISEL_STATE_IDLE);
    }
    break;

  case ANTISEL_STATE_IDLE:
    if (alert_flag) {
      alert_flag = 0U;
      go(ANTISEL_STATE_ALARM);
    }
    break;

  case ANTISEL_STATE_ALARM:
    /* Transiente: cattura la corrente al trigger e avvia T_HOLD. */
    diag_flags = 0U;
    current_at_trigger_a = INA301_AdcToCurrent(Acq_Latest());
    peak_current_a = current_at_trigger_a;
    t0_us = alarm_t0_us;
    go(ANTISEL_STATE_HOLD_RUN);
    break;

  case ANTISEL_STATE_HOLD_RUN: {
    float c = INA301_AdcToCurrent(Acq_Latest());
    if (c > peak_current_a) {
      peak_current_a = c;
    }
    if ((uint32_t)(Acq_Micros() - t0_us) >= cfg.t_hold_us) {
      /* Modo LATCHED: ALERT resta basso a prescindere dall'andamento della
       * corrente durante T_HOLD, quindi la discriminazione HCE/SEL si basa
       * sulla corrente ADC reale campionata a fine T_HOLD.
       * NOTA: qui era stata provata una media mobile (Acq_AvgRecent) al
       * posto del singolo campione, per robustezza al rumore/jitter. Test
       * di regressione ha mostrato pero' che con la media anche un evento
       * SEL isolato e pulito (che prima funzionava) viene misclassificato
       * come HCE in modo ripetuto. Tornato al campione singolo (com'era nei
       * test funzionanti) finche' non si capisce la causa nella media. */
      if (c > cfg.current_threshold_a) {
        go(ANTISEL_STATE_CUTOFF); /* ancora sopra soglia -> SEL */
      } else {
        go(ANTISEL_STATE_HCE_SAVE); /* rientrata entro T_HOLD -> HCE */
      }
    }
    break;
  }

  case ANTISEL_STATE_HCE_SAVE:
    if (first) {
      Acq_FreezeTrace(ACQ_TRACE_HCE, cfg.t_hold_us, 0U);
      add_event((uint8_t)EVENT_HCE, 0U);
      INA301_ResetLatch(); /* modo LATCHED: l'evento HCE ha comunque latchato ALERT */
      save_t0_ms = HAL_GetTick();
    }
    /* finestra post-evento, poi riarma e torna in IDLE (nessun power-cycle) */
    if ((uint32_t)(HAL_GetTick() - save_t0_ms) >= cfg.t_clear_ms) {
      if (exti_rearm_or_trigger()) {
        go(ANTISEL_STATE_IDLE);
      }
    }
    break;

  case ANTISEL_STATE_CUTOFF:
    /* Sgancio immediato del DUT e avvio T_ON. */
    switch_off();
    t0_us = Acq_Micros();
    go(ANTISEL_STATE_TON_RUN);
    break;

  case ANTISEL_STATE_TON_RUN: {
    float c = INA301_AdcToCurrent(Acq_Latest());
    if (c > peak_current_a) {
      peak_current_a = c;
    }
    if ((uint32_t)(Acq_Micros() - t0_us) >= cfg.t_on_us) {
      /* Traccia SEL completa (pre + T_HOLD + T_ON). DUT resta spento. */
      Acq_FreezeTrace(ACQ_TRACE_SEL, cfg.t_hold_us, cfg.t_on_us);
      if (retry_count == 0U) {
        add_event((uint8_t)EVENT_SEL, 0U); /* incrementa SEL una sola volta */
      }
      rec_t0_us = Acq_Micros();
      go(ANTISEL_STATE_RECOVERY);
    }
    break;
  }

  case ANTISEL_STATE_RECOVERY:
    /* DUT spento: azzera il latch (modo LATCHED, altrimenti ALERT resta
     * basso a prescindere dallo stato reale) poi, dopo l'assestamento,
     * verifica che ALERT sia rimasto alto (nessuna corrente). Se si
     * ri-latcha -> comparatore/linea bloccati -> FAULT. */
    if (first) {
      INA301_ResetLatch();
    }
    if ((uint32_t)(Acq_Micros() - rec_t0_us) >= cfg.recovery_settle_us) {
      if (INA301_IsAlertActive()) {
        diag_flags |= DIAG_ALERT_STUCK_LOW;
        fault_cause = (uint8_t)EVENT_ALERT_STUCK_LOW;
        Storage_UpdateLastEvent(2U, diag_flags);
        go(ANTISEL_STATE_FAULT);
      } else {
        switch_on(); /* rialimenta il DUT */
        ver_t0_us = Acq_Micros();
        go(ANTISEL_STATE_VERIFY);
      }
    }
    break;

  case ANTISEL_STATE_VERIFY:
    if ((uint32_t)(Acq_Micros() - ver_t0_us) >= cfg.recovery_settle_us) {
      uint16_t adc = Acq_Latest();
      if (INA301_IsAlertActive()) {
        /* Ancora in latch-up dopo la riaccensione: retry finiti o FAULT. */
        retry_count++;
        if (retry_count >= cfg.max_recovery_retries) {
          diag_flags |= DIAG_RECOVERY_FAIL;
          fault_cause = (uint8_t)EVENT_RECOVERY_FAULT;
          Storage_UpdateLastEvent(2U, diag_flags);
          go(ANTISEL_STATE_FAULT);
        } else {
          switch_off();
          t0_us = Acq_Micros(); /* nuovo power-cycle */
          go(ANTISEL_STATE_TON_RUN);
        }
      } else if (adc >= ADC_SAT_THRESHOLD) {
        diag_flags |= DIAG_ADC_SAT;
        fault_cause = (uint8_t)EVENT_ADC_FAULT;
        Storage_UpdateLastEvent(2U, diag_flags);
        go(ANTISEL_STATE_FAULT);
      } else {
        /* Recupero riuscito. */
        retry_count = 0U;
        Storage_UpdateLastEvent(1U, diag_flags);
        if (exti_rearm_or_trigger()) {
          go(ANTISEL_STATE_IDLE);
        }
      }
    }
    break;

  case ANTISEL_STATE_MANUAL_OFF:
    switch_off(); /* override OFF: DUT sempre spento */
    break;

  case ANTISEL_STATE_FAULT:
    switch_off(); /* DUT spento fino ad ACK esplicito */
    break;

  default:
    go(ANTISEL_STATE_FAULT);
    break;
  }
}

/* ── Comandi ────────────────────────────────────────────────────────────── */
AntiSelResult_t AntiSel_ManualOff(void) {
  /* Massima priorita': spegne da qualsiasi stato (spec sezione 7). */
  exti_disable();
  switch_off();
  peak_current_a = 0.0f;
  current_at_trigger_a = 0.0f;
  add_event((uint8_t)EVENT_MANUAL_OFF, 0U);
  go(ANTISEL_STATE_MANUAL_OFF);
  return ASEL_OK;
}

AntiSelResult_t AntiSel_ManualOn(void) {
  if (state == ANTISEL_STATE_FAULT) {
    return ASEL_ERR_FAULT; /* serve ACK FAULT esplicito */
  }
  if (state == ANTISEL_STATE_ALARM || state == ANTISEL_STATE_HOLD_RUN ||
      state == ANTISEL_STATE_CUTOFF || state == ANTISEL_STATE_TON_RUN ||
      state == ANTISEL_STATE_RECOVERY || state == ANTISEL_STATE_VERIFY) {
    return ASEL_ERR_BUSY; /* sequenza SEL attiva: non scavalcare */
  }
  INA301_ResetLatch(); /* modo LATCHED: azzera prima di leggere lo stato reale */
  if (INA301_IsAlertActive()) {
    return ASEL_ERR_ALERT_LOW; /* sovracorrente presente, si e' ri-latchato subito */
  }
  retry_count = 0U;
  switch_on();
  if (exti_rearm_or_trigger()) {
    go(ANTISEL_STATE_IDLE);
  }
  return ASEL_OK;
}

AntiSelResult_t AntiSel_AckFault(void) {
  if (state != ANTISEL_STATE_FAULT) {
    return ASEL_ERR_NOT_IN_FAULT;
  }
  INA301_ResetLatch(); /* modo LATCHED: azzera prima di leggere lo stato reale */
  if (INA301_IsAlertActive()) {
    return ASEL_ERR_ALERT_LOW;
  }
  retry_count = 0U;
  diag_flags = 0U;
  switch_on();
  if (exti_rearm_or_trigger()) {
    go(ANTISEL_STATE_IDLE);
  }
  return ASEL_OK;
}

void AntiSel_ResetSystem(void) {
  retry_count = 0U;
  diag_flags = 0U;
  Storage_ResetCounters();
  switch_on();
  INA301_ResetLatch();
  if (exti_rearm_or_trigger()) {
    go(ANTISEL_STATE_IDLE);
  }
}

/* ── Setter config ──────────────────────────────────────────────────────── */
AntiSelResult_t AntiSel_SetGain(float v) {
  if (v != 20.0f && v != 50.0f && v != 100.0f) {
    return ASEL_ERR_RANGE;
  }
  cfg.ina_gain_v_v = v;
  apply_cal();
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetRshunt(float v) {
  if (v <= 0.0f || v > 100.0f) {
    return ASEL_ERR_RANGE;
  }
  cfg.rshunt_ohm = v;
  apply_cal();
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetVrefAdc(float v) {
  if (v < 1.0f || v > 3.6f) {
    return ASEL_ERR_RANGE;
  }
  cfg.adc_vref_v = v;
  apply_cal();
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetVrefDac(float v) {
  if (v < 1.0f || v > 3.6f) {
    return ASEL_ERR_RANGE;
  }
  cfg.dac_vref_v = v;
  apply_cal();
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetThresholdMa(float ma) {
  if (ma < 1.0f || ma > 50.0f) {
    return ASEL_ERR_RANGE;
  }
  float a = ma / 1000.0f;
  if (!INA301_SetLimitCurrent(a)) {
    return ASEL_ERR_RANGE; /* VLIMIT fuori range DAC */
  }
  cfg.current_threshold_a = a;
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetTholdUs(uint32_t us) {
  if (us < 1000U || us > 10000U) {
    return ASEL_ERR_RANGE;
  }
  cfg.t_hold_us = us;
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetTonUs(uint32_t us) {
  if (us < 1000U || us > 10000U) {
    return ASEL_ERR_RANGE;
  }
  cfg.t_on_us = us;
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetRetryMax(uint32_t n) {
  if (n < 1U || n > 100U) {
    return ASEL_ERR_RANGE;
  }
  cfg.max_recovery_retries = n;
  return ASEL_OK;
}
AntiSelResult_t AntiSel_SetTclearMs(uint32_t ms) {
  if (ms < 1U || ms > 10000U) {
    return ASEL_ERR_RANGE;
  }
  cfg.t_clear_ms = ms;
  return ASEL_OK;
}

/* ── Getter ─────────────────────────────────────────────────────────────── */
AntiSelState_t AntiSel_GetState(void) { return state; }

const char *AntiSel_StateName(AntiSelState_t s) {
  static const char *names[] = {"INIT",    "IDLE",     "ALARM",
                                "HOLD_RUN", "HCE_SAVE", "CUTOFF",
                                "TON_RUN",  "RECOVERY", "VERIFY",
                                "MANUAL_OFF", "FAULT"};
  return (s <= ANTISEL_STATE_FAULT) ? names[s] : "?";
}

const AntiSelConfig_t *AntiSel_GetConfig(void) { return &cfg; }
uint32_t AntiSel_RetryCount(void) { return retry_count; }
uint8_t AntiSel_OverrideActive(void) {
  return (state == ANTISEL_STATE_MANUAL_OFF) ? 1U : 0U;
}
uint8_t AntiSel_SwitchOn(void) {
  return (HAL_GPIO_ReadPin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin) ==
          GPIO_PIN_SET)
             ? 1U
             : 0U;
}
uint32_t AntiSel_LastDac(void) {
  return HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1);
}
