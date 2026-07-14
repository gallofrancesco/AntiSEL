# AntiSEL — Matrice di tracciabilità (spec §21)

Mappatura **Requisito → modulo firmware → funzione → periferica → test di verifica**,
riferita all'implementazione Fase 2 (macchina a 11 stati, modulare).

Board: NUCLEO-H755ZI-Q (STM32H755ZITx), applicazione sul core **Cortex-M7**.

## Assegnazione pin (dal `.ioc`)

| Segnale logico | Pin | Funzione STM32 | Note |
|---|---|---|---|
| INA301 OUT | PA3 | ADC1_INP15 | ingresso ADC (corrente) |
| INA301 ALERT | PE13 | GPIO EXTI13, pull-up | active-low, fronte di discesa |
| INA301 RESET | PE14 | GPIO output | default **LOW** = transparent (spec §4) |
| INA301 LIMIT | PA4 | DAC1_OUT1 | soglia VLIMIT |
| TPS22810 EN | PG14 | GPIO output (DUT_SWITCH) | alimentazione DUT |
| Ethernet | RMII | ETH + LAN8742 | protocollo, porta TCP 7755 |

Timer: **TIM2** timebase 1 µs (32 bit); **TIM6** TRGO 100 kHz (pacing ADC); **DMA1_Stream0** ADC circolare.

---

## Requisiti obbligatori R-01 … R-08

| Req | Descrizione | Modulo | Funzione | Periferica | Test §19 |
|---|---|---|---|---|---|
| **R-01** | Monitoraggio e protezione a soglia della corrente | `ina301`, `antisel` | `INA301_IsAlertActive`, `AntiSel_Task` (IDLE→ALARM→HOLD_RUN), `INA301_SetLimitCurrent` | INA301 ALERT (EXTI13), DAC1, ADC1 | T1, T2 |
| **R-02** | Soglia I_TH regolabile 1–50 mA | `antisel`, `ina301` | `AntiSel_SetThresholdMa` (valida 1–50), `INA301_CurrentToLimitVoltage`, `INA301_LimitVoltageToDacCode` (satura) | DAC1_OUT1 (PA4) | T20 |
| **R-03** | T_HOLD 1–10 ms, risoluzione ≤100 µs | `antisel`, `antisel_acquisition` | `AntiSel_SetTholdUs` (1000–10000), `Acq_Micros` (TIM2 1 µs), stato HOLD_RUN | TIM2 | T3, T5, T23 |
| **R-04** | T_ON 1–10 ms, risoluzione ≤100 µs | `antisel`, `antisel_acquisition` | `AntiSel_SetTonUs` (1000–10000), stato TON_RUN, `Acq_Micros` | TIM2 | T8 |
| **R-05** | HCE (rientro entro T_HOLD) senza power-cycle | `antisel` | `AntiSel_Task` HOLD_RUN→HCE_SAVE via `INA301_IsAlertActive` (ritorno ALTO) | INA301 ALERT | T3, T4 |
| **R-06** | Traccia SEL/HCE con ≥1 ms pre-trigger | `antisel_acquisition` | `Acq_FreezeTrace`, `ACQ_PRE_MS`, buffer circolare 40 ms | ADC1 + DMA1 + TIM6 | T6, T17, T18 |
| **R-07** | Comando manuale ON/OFF del TPS22810 via Ethernet | `antisel_protocol`, `antisel` | `handle_command` (`SWITCH ON/OFF`), `AntiSel_ManualOn/Off` | ETH (TCP 7755), GPIO EN (PG14) | T13, T14, T15, T16 |
| **R-08** | Log continuo a 10 Hz durante tutto il test | `antisel_protocol` | `Proto_Service`→`send_log` (ogni 100 ms) | ETH | T21 |

---

## Requisiti funzionali derivati (spec §1/§6)

| Funzione richiesta | Modulo | Funzione/stato | Periferica | Test §19 |
|---|---|---|---|---|
| Soglia analogica programmabile via DAC | `ina301` | `INA301_SetLimitCurrent`, `INA301_LimitVoltageToDacCode` | DAC1 | T20 |
| Rilevamento superamento via ALERT (percorso rapido) | `antisel` | `AntiSel_OnAlertISR` (EXTI), stato ALARM | EXTI13 | T2, T22 |
| Classificazione SEL (ALERT basso a fine T_HOLD) | `antisel` | HOLD_RUN → CUTOFF | INA301 ALERT, TIM2 | T5, T6 |
| Apertura TPS22810 su SEL | `antisel` | stato CUTOFF (`switch_off`) | GPIO EN (PG14) | T7 |
| Riarmo sicuro e rialimentazione | `antisel` | RECOVERY → VERIFY → IDLE | GPIO EN, INA301 ALERT | T9, T10 |
| ALERT permanentemente basso dopo recovery | `antisel` | RECOVERY/VERIFY → FAULT (`EVENT_ALERT_STUCK_LOW`) | INA301 ALERT | T11 |
| Limite massimo di retry finito | `antisel` | `cfg.max_recovery_retries`, VERIFY | — | T12 |
| Override manuale OFF (priorità massima) | `antisel` | `AntiSel_ManualOff` → stato MANUAL_OFF | GPIO EN | T13–T15 |
| ON non scavalca SEL/FAULT | `antisel` | `AntiSel_ManualOn` (ritorna `ERR BUSY/FAULT/ALERT_LOW`) | — | T16 |
| Acquisizione ADC continua (timer+DMA) | `antisel_acquisition` | `Acq_Init`, `Acq_Start`, `Acq_WritePos` | ADC1, TIM6, DMA1 | T18 |
| Coerenza D-Cache sul buffer DMA | `antisel_acquisition` | `SCB_InvalidateDCache_by_Addr` in `Acq_Latest`/`Acq_FreezeTrace` | Cortex-M7 D-Cache | T18 |
| Record evento SEL/HCE | `antisel_storage` | `AntiSelEvent_t`, `Storage_AddEvent`, `Storage_UpdateLastEvent` | — | T4, T6 |
| Consultazione eventi da PC | `antisel_protocol` | `handle_command` `GET EVENT <id>` | ETH | T4, T6 |
| Configurazione parametrica validata | `antisel` | `AntiSel_SetGain/Rshunt/Vref*/ThresholdMa/…` | DAC1 | T20 |
| Timing non bloccante, no `HAL_Delay` | `antisel` | `AntiSel_Task` (polling `Acq_Micros`) | TIM2 | T25 |
| Protezione indipendente da Ethernet | `antisel` | FSM su ALERT/ADC, non su TCP | INA301 ALERT | T22 |
| Sicurezza: switch OFF su errore critico | `antisel` | stato FAULT (`switch_off`) | GPIO EN | T11, T24 |

---

## Copertura test §19

Legenda: **U** = unit test (host / logico), **HIL** = hardware-in-the-loop (scheda + iniezione corrente + oscilloscopio/logic analyzer).

| # | Test | Tipo | Stato |
|---|---|---|---|
| 1 | Corrente sotto soglia | HIL | da eseguire |
| 2 | Superamento breve della soglia | HIL | da eseguire |
| 3 | ALERT rilasciato prima di T_HOLD | HIL | da eseguire |
| 4 | Classificazione HCE corretta | HIL | da eseguire |
| 5 | Sovracorrente oltre T_HOLD | HIL | da eseguire |
| 6 | Classificazione SEL corretta | HIL | da eseguire |
| 7 | Apertura del TPS22810 | HIL | da eseguire |
| 8 | Rispetto di T_ON | HIL | da eseguire |
| 9 | Reset INA301 / ritorno transparent | HIL | da eseguire |
| 10 | Riaccensione sicura | HIL | da eseguire |
| 11 | ALERT ancora basso dopo recovery → FAULT | HIL | da eseguire |
| 12 | Limite massimo di retry | HIL | da eseguire |
| 13 | Override OFF in IDLE | HIL | da eseguire |
| 14 | Override OFF durante HOLD_RUN | HIL | da eseguire |
| 15 | Override OFF durante TON_RUN | HIL | da eseguire |
| 16 | Override ON durante FAULT (rifiutato) | HIL | da eseguire |
| 17 | Pre-trigger ≥1 ms | HIL | da eseguire |
| 18 | Wrap-around buffer DMA | HIL | da eseguire |
| 19 | Saturazione ADC | HIL | da eseguire |
| 20 | Saturazione DAC | **U** | ✅ (test conversioni gcc) |
| 21 | Log continuo a 10 Hz | HIL | da eseguire |
| 22 | Perdita Ethernet senza perdita protezione | HIL | da eseguire |
| 23 | Race ALERT vs scadenza T_HOLD | HIL | da eseguire |
| 24 | Reset MCU durante un evento | HIL | da eseguire |
| 25 | Verifica timing con oscilloscopio/LA | HIL | da eseguire |

Verifiche già eseguite in questo ambiente (senza hardware):
- Conversioni INA301 e validazione soglia/DAC (**T20**) — assert su gcc, passati.
- Ring buffer eventi `antisel_storage` — assert su gcc, passati.

---

## Checklist di conformità R-01 … R-08

| Req | Codice fornito | Test fornito | Note |
|---|---|---|---|
| R-01 | ✅ | parziale | protezione via ALERT+DAC implementata; test HIL da eseguire |
| R-02 | ✅ | ✅ (U) | range 1–50 mA validato, saturazione DAC testata |
| R-03 | ✅ | parziale | 1–10 ms, timebase 1 µs (ris. << 100 µs); HIL da eseguire |
| R-04 | ✅ | parziale | idem T_ON |
| R-05 | ✅ | parziale | HCE via ritorno ALERT, nessun power-cycle |
| R-06 | ✅ | parziale | pre-trigger 1 ms nel freeze; misura HIL da eseguire |
| R-07 | ✅ | parziale | SWITCH ON/OFF + priorità; HIL da eseguire |
| R-08 | ✅ | parziale | log 10 Hz in `Proto_Service`; verifica continuità HIL |

> Nota di onestà (spec §21): un requisito è marcato pienamente soddisfatto solo
> quando **codice e test** sono entrambi forniti. Qui il codice è completo e
> compilato; i test **T20** e il ring eventi sono verificati, il resto richiede
> la campagna **HIL** sulla scheda.
