/**
 ******************************************************************************
 * @file    antisel_storage.h
 * @brief   Record eventi SEL/HCE e contatori (AntiSEL, spec sezione 12).
 ******************************************************************************
 */
#ifndef ANTISEL_STORAGE_H
#define ANTISEL_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

/* Tipi evento (spec sezione 12) */
typedef enum {
  EVENT_HCE = 0,
  EVENT_SEL,
  EVENT_MANUAL_OFF,
  EVENT_RECOVERY_FAULT,
  EVENT_ADC_FAULT,
  EVENT_ALERT_STUCK_LOW
} AntiSelEventType_t;

typedef struct {
  uint64_t timestamp_us;
  uint32_t event_id;
  uint32_t pretrigger_start_index;
  uint32_t trigger_index;
  uint32_t event_end_index;
  float threshold_a;
  uint32_t t_hold_us;
  uint32_t t_on_us;
  float peak_current_a;
  float current_at_trigger_a;
  uint8_t event_type;
  uint8_t recovery_result;
  uint8_t override_active;
  uint32_t diagnostic_flags;
} AntiSelEvent_t;

/* Numero di eventi mantenuti (ring, allocazione statica). */
#define ANTISEL_EVENT_RING 32U

void Storage_Init(void);

/* Aggiunge un evento: assegna event_id progressivo e aggiorna i contatori.
 * Ritorna l'id assegnato. */
uint32_t Storage_AddEvent(AntiSelEvent_t *ev);

/* Recupera un evento per id; ritorna false se non piu' presente nel ring. */
bool Storage_GetEvent(uint32_t id, AntiSelEvent_t *out);

/* Aggiorna esito recovery e flag diagnostici dell'ultimo evento aggiunto. */
void Storage_UpdateLastEvent(uint8_t recovery_result, uint32_t diagnostic_flags);

uint32_t Storage_SelCount(void);
uint32_t Storage_HceCount(void);
uint32_t Storage_TotalEvents(void);
void Storage_ResetCounters(void);

#endif /* ANTISEL_STORAGE_H */
