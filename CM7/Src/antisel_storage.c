/**
 ******************************************************************************
 * @file    antisel_storage.c
 * @brief   Ring buffer eventi + contatori SEL/HCE.
 ******************************************************************************
 */
#include "antisel_storage.h"
#include <string.h>

static AntiSelEvent_t ring[ANTISEL_EVENT_RING];
static uint32_t total_events; /* id progressivo del prossimo evento */
static uint32_t sel_count;
static uint32_t hce_count;

void Storage_Init(void) {
  memset(ring, 0, sizeof(ring));
  total_events = 0U;
  sel_count = 0U;
  hce_count = 0U;
}

uint32_t Storage_AddEvent(AntiSelEvent_t *ev) {
  uint32_t id = total_events;
  ev->event_id = id;
  ring[id % ANTISEL_EVENT_RING] = *ev;
  total_events++;
  if (ev->event_type == (uint8_t)EVENT_SEL) {
    sel_count++;
  } else if (ev->event_type == (uint8_t)EVENT_HCE) {
    hce_count++;
  }
  return id;
}

bool Storage_GetEvent(uint32_t id, AntiSelEvent_t *out) {
  /* Valido solo se l'id e' stato assegnato e non ancora sovrascritto. */
  if (id >= total_events) {
    return false;
  }
  if (total_events > ANTISEL_EVENT_RING &&
      id < (total_events - ANTISEL_EVENT_RING)) {
    return false; /* uscito dal ring */
  }
  *out = ring[id % ANTISEL_EVENT_RING];
  return true;
}

void Storage_UpdateLastEvent(uint8_t recovery_result,
                             uint32_t diagnostic_flags) {
  if (total_events == 0U) {
    return;
  }
  uint32_t id = total_events - 1U;
  ring[id % ANTISEL_EVENT_RING].recovery_result = recovery_result;
  ring[id % ANTISEL_EVENT_RING].diagnostic_flags = diagnostic_flags;
}

uint32_t Storage_SelCount(void) { return sel_count; }
uint32_t Storage_HceCount(void) { return hce_count; }
uint32_t Storage_TotalEvents(void) { return total_events; }

void Storage_ResetCounters(void) {
  sel_count = 0U;
  hce_count = 0U;
}
