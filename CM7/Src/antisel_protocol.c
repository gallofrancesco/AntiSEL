/**
 ******************************************************************************
 * @file    antisel_protocol.c
 * @brief   Server TCP AntiSEL (porta 7755): comandi v5, telemetria 10 Hz e
 *          invio traccia evento.
 ******************************************************************************
 */
#include "antisel_protocol.h"
#include "antisel.h"
#include "antisel_acquisition.h"
#include "antisel_storage.h"
#include "ina301.h"
#include "main.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct tcp_pcb *server_pcb;
static struct tcp_pcb *client_pcb;

/* Stato invio traccia */
static uint8_t trace_sending;
static uint32_t trace_send_index;
static uint32_t last_log_ms;

/* Crash post-mortem (spec §16): record hard-fault salvato in DTCM dal fault
 * handler (stm32h7xx_it.c), riportato alla GUI dopo il reset. */
static uint8_t crash_pending;
static uint8_t crash_count;
static uint32_t crash_last_ms;
static FaultRecord_t crash;

/* ── util ───────────────────────────────────────────────────────────────── */
static void send_str(struct tcp_pcb *tpcb, const char *s) {
  tcp_write(tpcb, s, strlen(s), TCP_WRITE_FLAG_COPY);
}

static const char *err_str(AntiSelResult_t r) {
  switch (r) {
  case ASEL_ERR_FAULT: return "ERR FAULT\r\n";
  case ASEL_ERR_SEL_ACTIVE: return "ERR SEL_ACTIVE\r\n";
  case ASEL_ERR_BUSY: return "ERR BUSY\r\n";
  case ASEL_ERR_ALERT_LOW: return "ERR ALERT_LOW\r\n";
  case ASEL_ERR_RANGE: return "ERR RANGE\r\n";
  case ASEL_ERR_NOT_IN_FAULT: return "ERR NOT_IN_FAULT\r\n";
  default: return "OK\r\n";
  }
}

/* ── comandi ────────────────────────────────────────────────────────────── */
static void handle_command(struct tcp_pcb *tpcb, char *line) {
  char buf[200];

  if (strncmp(line, "PING", 4) == 0) {
    send_str(tpcb, "PONG\r\n");
    return;
  }
  if (strncmp(line, "GET STATUS", 10) == 0 || strncmp(line, "STATUS", 6) == 0) {
    snprintf(buf, sizeof(buf),
             "OK STATUS=%s ALERT=%u SWITCH=%s OVERRIDE=%u RETRY=%lu SEL=%lu "
             "HCE=%lu\r\n",
             AntiSel_StateName(AntiSel_GetState()), INA301_IsAlertActive(),
             AntiSel_SwitchOn() ? "ON" : "OFF", AntiSel_OverrideActive(),
             (unsigned long)AntiSel_RetryCount(),
             (unsigned long)Storage_SelCount(),
             (unsigned long)Storage_HceCount());
    send_str(tpcb, buf);
    return;
  }
  if (strncmp(line, "GET COUNTERS", 12) == 0) {
    snprintf(buf, sizeof(buf), "OK SEL=%lu HCE=%lu\r\n",
             (unsigned long)Storage_SelCount(),
             (unsigned long)Storage_HceCount());
    send_str(tpcb, buf);
    return;
  }
  if (strncmp(line, "GET CONFIG", 10) == 0) {
    const AntiSelConfig_t *c = AntiSel_GetConfig();
    uint32_t dac = AntiSel_LastDac();
    float thr_ma = c->current_threshold_a * 1000.0f;
    int rs_i = (int)c->rshunt_ohm, rs_f = (int)((c->rshunt_ohm - rs_i) * 1000.0f);
    int va_i = (int)c->adc_vref_v, va_f = (int)((c->adc_vref_v - va_i) * 1000.0f);
    int vd_i = (int)c->dac_vref_v, vd_f = (int)((c->dac_vref_v - vd_i) * 1000.0f);
    int th_i = (int)thr_ma, th_f = (int)((thr_ma - th_i) * 10.0f);
    snprintf(buf, sizeof(buf),
             "OK CONFIG GAIN=%d RSHUNT=%d.%03d VREF_ADC=%d.%03d "
             "VREF_DAC=%d.%03d THRESHOLD_MA=%d.%d DAC=%lu THOLD_US=%lu "
             "TON_US=%lu SETTLE_US=%lu RETRY_MAX=%lu TCLEAR_MS=%lu\r\n",
             (int)c->ina_gain_v_v, rs_i, rs_f, va_i, va_f, vd_i, vd_f, th_i,
             th_f, (unsigned long)dac, (unsigned long)c->t_hold_us,
             (unsigned long)c->t_on_us, (unsigned long)c->recovery_settle_us,
             (unsigned long)c->max_recovery_retries,
             (unsigned long)c->t_clear_ms);
    send_str(tpcb, buf);
    return;
  }
  if (strncmp(line, "GET EVENT", 9) == 0) {
    uint32_t id = (uint32_t)atol(line + 9);
    AntiSelEvent_t ev;
    if (!Storage_GetEvent(id, &ev)) {
      send_str(tpcb, "ERR RANGE\r\n");
      return;
    }
    float pk = ev.peak_current_a * 1000.0f;
    float it = ev.current_at_trigger_a * 1000.0f;
    float thr = ev.threshold_a * 1000.0f;
    snprintf(buf, sizeof(buf),
             "OK EVENT id=%lu type=%u ts_us=%lu trig=%lu start=%lu end=%lu "
             "peak_mA=%d.%03d itrig_mA=%d.%03d thr_mA=%d.%d thold_us=%lu "
             "ton_us=%lu rec=%u ovr=%u diag=0x%lx\r\n",
             (unsigned long)ev.event_id, ev.event_type,
             (unsigned long)(uint32_t)ev.timestamp_us,
             (unsigned long)ev.trigger_index,
             (unsigned long)ev.pretrigger_start_index,
             (unsigned long)ev.event_end_index, (int)pk,
             (int)((pk - (int)pk) * 1000.0f), (int)it,
             (int)((it - (int)it) * 1000.0f), (int)thr,
             (int)((thr - (int)thr) * 10.0f), (unsigned long)ev.t_hold_us,
             (unsigned long)ev.t_on_us, ev.recovery_result, ev.override_active,
             (unsigned long)ev.diagnostic_flags);
    send_str(tpcb, buf);
    return;
  }

  /* ---- Config ---- */
  if (strncmp(line, "SET GAIN", 8) == 0) {
    AntiSelResult_t r = AntiSel_SetGain((float)atof(line + 8));
    send_str(tpcb, r == ASEL_OK ? "OK GAIN\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET RSHUNT", 10) == 0) {
    AntiSelResult_t r = AntiSel_SetRshunt((float)atof(line + 10));
    send_str(tpcb, r == ASEL_OK ? "OK RSHUNT\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET VREF_ADC", 12) == 0) {
    AntiSelResult_t r = AntiSel_SetVrefAdc((float)atof(line + 12));
    send_str(tpcb, r == ASEL_OK ? "OK VREF_ADC\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET VREF_DAC", 12) == 0) {
    AntiSelResult_t r = AntiSel_SetVrefDac((float)atof(line + 12));
    send_str(tpcb, r == ASEL_OK ? "OK VREF_DAC\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET THRESHOLD_MA", 16) == 0) {
    AntiSelResult_t r = AntiSel_SetThresholdMa((float)atof(line + 16));
    if (r == ASEL_OK) {
      snprintf(buf, sizeof(buf), "OK THRESHOLD_MA DAC=%lu\r\n",
               (unsigned long)AntiSel_LastDac());
      send_str(tpcb, buf);
    } else {
      send_str(tpcb, err_str(r));
    }
    return;
  }
  if (strncmp(line, "SET THOLD_US", 12) == 0) {
    AntiSelResult_t r = AntiSel_SetTholdUs((uint32_t)atol(line + 12));
    send_str(tpcb, r == ASEL_OK ? "OK THOLD_US\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET TON_US", 10) == 0) {
    AntiSelResult_t r = AntiSel_SetTonUs((uint32_t)atol(line + 10));
    send_str(tpcb, r == ASEL_OK ? "OK TON_US\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET RETRY_MAX", 13) == 0) {
    AntiSelResult_t r = AntiSel_SetRetryMax((uint32_t)atol(line + 13));
    send_str(tpcb, r == ASEL_OK ? "OK RETRY_MAX\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "SET TCLEAR_MS", 13) == 0) {
    AntiSelResult_t r = AntiSel_SetTclearMs((uint32_t)atol(line + 13));
    send_str(tpcb, r == ASEL_OK ? "OK TCLEAR_MS\r\n" : err_str(r));
    return;
  }

  /* ---- Controllo ---- */
  if (strncmp(line, "SWITCH OFF", 10) == 0 || strncmp(line, "DUT_OFF", 7) == 0) {
    AntiSel_ManualOff();
    send_str(tpcb, "OK SWITCH=OFF\r\n");
    return;
  }
  if (strncmp(line, "SWITCH ON", 9) == 0 || strncmp(line, "DUT_ON", 6) == 0) {
    AntiSelResult_t r = AntiSel_ManualOn();
    send_str(tpcb, r == ASEL_OK ? "OK SWITCH=ON\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "ACK FAULT", 9) == 0) {
    AntiSelResult_t r = AntiSel_AckFault();
    send_str(tpcb, r == ASEL_OK ? "OK ACK\r\n" : err_str(r));
    return;
  }
  if (strncmp(line, "RESET", 5) == 0) {
    AntiSel_ResetSystem();
    send_str(tpcb, "OK RESET\r\n");
    return;
  }
  if (strncmp(line, "INA_RST", 7) == 0) {
    INA301_ResetLatch();
    send_str(tpcb, "OK INA_RST\r\n");
    return;
  }

  send_str(tpcb, "ERR UNKNOWN\r\n");
}

/* ── TCP server ─────────────────────────────────────────────────────────── */
static err_t on_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                     err_t err) {
  static char line_buf[64];
  static uint8_t line_len;
  (void)arg;
  (void)err;

  if (p == NULL) {
    client_pcb = NULL;
    line_len = 0U;
    tcp_close(tpcb);
    return ERR_OK;
  }
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    const char *d = (const char *)q->payload;
    for (u16_t i = 0; i < q->len; i++) {
      char c = d[i];
      if (c == '\n' || c == '\r') {
        if (line_len > 0U) {
          line_buf[line_len] = '\0';
          handle_command(tpcb, line_buf);
          line_len = 0U;
        }
      } else if (line_len < sizeof(line_buf) - 1U) {
        line_buf[line_len++] = c;
      } else {
        line_len = 0U;
      }
    }
  }
  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

static void on_err(void *arg, err_t err) {
  (void)arg;
  (void)err;
  client_pcb = NULL;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
  (void)arg;
  if (err != ERR_OK || newpcb == NULL) {
    return ERR_VAL;
  }
  if (client_pcb != NULL) {
    tcp_abort(client_pcb);
    client_pcb = NULL;
  }
  client_pcb = newpcb;
  tcp_setprio(newpcb, TCP_PRIO_MIN);
  tcp_recv(newpcb, on_recv);
  tcp_err(newpcb, on_err);
  return ERR_OK;
}

void Proto_Init(void) {
  /* Rileva un eventuale record di fault sopravvissuto al reset e consumalo. */
  if (FAULT_REC.magic == FAULT_MAGIC) {
    crash = FAULT_REC;      /* copia il record dal DTCM */
    FAULT_REC.magic = 0U;   /* consuma: non ripetere ai reset futuri */
    crash_pending = 1U;
  }

  server_pcb = tcp_new();
  if (server_pcb == NULL) {
    return;
  }
  tcp_bind(server_pcb, IP_ADDR_ANY, 7755);
  server_pcb = tcp_listen_with_backlog(server_pcb, 1);
  if (server_pcb == NULL) {
    return;
  }
  tcp_accept(server_pcb, on_accept);
}

/* ── Telemetria ─────────────────────────────────────────────────────────── */
static void send_trace(void) {
  if (Acq_TracePending() && !trace_sending) {
    if (client_pcb == NULL) {
      return;
    }
    char hdr[128];
    int hl = snprintf(
        hdr, sizeof(hdr),
        "TRACE_START %s FS=%lu N=%lu THOLD_US=%lu TON_US=%lu DAC=%lu TRIG=%lu "
        "TICK=%lu\r\n",
        (Acq_TraceEventType() == ACQ_TRACE_SEL) ? "SEL" : "HCE",
        (unsigned long)ACQ_SAMPLE_RATE_HZ, (unsigned long)Acq_TraceLen(),
        (unsigned long)Acq_TraceTholdUs(), (unsigned long)Acq_TraceTonUs(),
        (unsigned long)AntiSel_LastDac(), (unsigned long)Acq_TraceTrigIndex(),
        (unsigned long)HAL_GetTick());
    tcp_write(client_pcb, hdr, hl, TCP_WRITE_FLAG_COPY);
    tcp_output(client_pcb);
    trace_send_index = 0U;
    trace_sending = 1U;
  }

  if (!trace_sending) {
    return;
  }
  if (client_pcb == NULL) {
    trace_sending = 0U;
    Acq_TraceMarkSent();
    return;
  }
  uint32_t len = Acq_TraceLen();
  if (tcp_sndbuf(client_pcb) >= 480U) {
    char buf[640];
    int used = 0;
    for (int i = 0; i < 40 && trace_send_index < len; i++) {
      int remaining = (int)sizeof(buf) - used;
      if (remaining <= 0) {
        break;
      }
      int w = snprintf(buf + used, remaining, "%lu,%u\r\n",
                       (unsigned long)trace_send_index,
                       Acq_TraceSample(trace_send_index));
      if (w > 0 && w < remaining) {
        used += w;
      } else {
        break;
      }
      trace_send_index++;
    }
    if (used > 0) {
      tcp_write(client_pcb, buf, used, TCP_WRITE_FLAG_COPY);
      tcp_output(client_pcb);
    }
  }
  if (trace_send_index >= len) {
    if (tcp_sndbuf(client_pcb) >= 11U) {
      tcp_write(client_pcb, "TRACE_END\r\n", 11, TCP_WRITE_FLAG_COPY);
      tcp_output(client_pcb);
      trace_sending = 0U;
      Acq_TraceMarkSent();
    }
  }
}

static void send_log(void) {
  if (HAL_GetTick() - last_log_ms < 100U) {
    return;
  }
  last_log_ms = HAL_GetTick();
  if (client_pcb == NULL) {
    return;
  }
  const AntiSelConfig_t *c = AntiSel_GetConfig();
  uint16_t adc = Acq_Latest();
  float vout = INA301_AdcToVoltage(adc);
  float ima = INA301_AdcToCurrent(adc) * 1000.0f;
  float thr = c->current_threshold_a * 1000.0f;
  int vo_i = (int)vout, vo_f = (int)((vout - vo_i) * 1000.0f);
  int im_i = (int)ima, im_f = (int)((ima - im_i) * 1000.0f);
  if (im_f < 0) {
    im_f = -im_f;
  }
  int th_i = (int)thr, th_f = (int)((thr - th_i) * 10.0f);
  char buf[192];
  snprintf(buf, sizeof(buf),
           "LOG_10HZ TICK=%lu ADC=%u VOUT=%d.%03d I_MA=%d.%03d THR_MA=%d.%d "
           "ALERT=%u SWITCH=%s OVERRIDE=%u STATE=%d RETRY=%lu SEL=%lu HCE=%lu "
           "FRESH=%u\r\n",
           (unsigned long)HAL_GetTick(), adc, vo_i, vo_f, im_i, im_f, th_i,
           th_f, INA301_IsAlertActive(), AntiSel_SwitchOn() ? "ON" : "OFF",
           AntiSel_OverrideActive(), (int)AntiSel_GetState(),
           (unsigned long)AntiSel_RetryCount(),
           (unsigned long)Storage_SelCount(),
           (unsigned long)Storage_HceCount(), Acq_Running());
  if (tcp_sndbuf(client_pcb) >= strlen(buf)) {
    tcp_write(client_pcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
    tcp_output(client_pcb);
  }
}

/* Invia il report di crash alla GUI ~1 volta/s, ripetuto fino a 20 volte
 * (cosi' e' visibile anche riconnettendo la dashboard con calma). */
static void send_crash(void) {
  if (!crash_pending || client_pcb == NULL) {
    return;
  }
  if (HAL_GetTick() - crash_last_ms < 1000U) {
    return;
  }
  crash_last_ms = HAL_GetTick();
  char fb[240];
  int fl = snprintf(
      fb, sizeof(fb),
      "FAULT HFSR=0x%08lX CFSR=0x%08lX BFAR=0x%08lX MMFAR=0x%08lX PC=0x%08lX "
      "LR=0x%08lX STK=%08lX,%08lX,%08lX,%08lX\r\n",
      (unsigned long)crash.hfsr, (unsigned long)crash.cfsr,
      (unsigned long)crash.bfar, (unsigned long)crash.mmfar,
      (unsigned long)crash.pc, (unsigned long)crash.lr,
      (unsigned long)crash.stk[0], (unsigned long)crash.stk[1],
      (unsigned long)crash.stk[2], (unsigned long)crash.stk[3]);
  if (fl > 0 && tcp_sndbuf(client_pcb) >= (uint16_t)fl) {
    tcp_write(client_pcb, fb, fl, TCP_WRITE_FLAG_COPY);
    tcp_output(client_pcb);
    if (++crash_count >= 20U) {
      crash_pending = 0U;
    }
  }
}

void Proto_Service(void) {
  send_crash();
  send_trace();
  send_log();
}
