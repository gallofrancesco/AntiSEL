# Campagna di test — Sistema AntiSEL (AD8629 SEE)

Piano di validazione del banco AntiSEL: firmware NUCLEO-H755ZI-Q (modo **latched**,
discriminazione **ADC**), GUI `antisel_dashboard_eth.py`, hardware AntiSEL Board.
Riferimenti: GST-TSP-13 (§3 requisiti R-01…R-08, §5 algoritmo, §6 acquisizione, §8 condizioni).

---

## 1. Configurazioni di prova

**Setup A — Banco senza fascio (validazione funzionale).**
NUCLEO + emulatore INA301 su Arduino (5 V, open-drain), che pilota:
- **PE13** (D3) = fronte di discesa → avvia T_HOLD (EXTI);
- **PA3** (A0 Nucleo) = livello di corrente 0–3,3 V → decide HCE/SEL (ADC);
e monitora **PG14** (D2, switch) e **PA4** (V_LIMIT). GND comune. GUI su PC via Ethernet.
Serve a validare firmware + GUI + logica di protezione senza irraggiamento.

**Setup B — Sistema completo (campagna).**
AntiSEL Board reale (R_SHUNT + INA301 + TPS22810) + Irradiation Board (DUT AD8629
delidded, Heat System) sotto fascio. Il sense path reale genera gli eventi.

**Precondizioni comuni:** firmware CM7 flashato (versione latched), `pip install -r
requirements.txt`, PC su subnet 192.168.1.x, T_DUT = +85 °C, V_DD = 6 V, R_SHUNT e
gain INA301 impostati in GUI coerenti con l'hardware.

### 1.1 Mappatura pin (riferimento)

Posizioni connettori verificate su UM2408 (MB1363), Tab. 17. Firmware attuale:
INA301 **latched**, ALARM su **PE13**, RST su **PE14**, discriminazione **via ADC**.

**Segnali core AntiSEL**

| Segnale (architettura) | Pin STM32 | Config | Connettore | Ruolo | Req |
|---|---|---|---|---|---|
| **ALARM** (INA301 → MCU) | **PE13** | EXTI13, fronte discesa, pull-up | **CN10 pin 10** (D3) | ingresso interrupt: avvia il T_HOLD | R-01/R-05 |
| **RST** (MCU → INA301) | **PE14** | GPIO out, riposo HIGH = latched | **CN10 pin 8** (D4) | impulso LOW: azzera il latch a fine T_ON | §4.3 |
| **V_LIMIT** (DAC → INA301 LIMIT) | **PA4** | DAC1_OUT1 | **CN7 pin 17** (D24) | soglia I_TH dal DAC | R-02 |
| **I_sense** (INA301 OUT → ADC) | **PA3** | ADC1_INP15 | **CN9 pin 1** (A0) | traccia corrente + **discriminazione HCE/SEL** | R-06 |
| **EN** (MCU → TPS22810) | **PG14** | GPIO out | **CN10 pin 12** (D2) | power switch / power-cycle | R-07 |

**Emulatore Arduino (Setup A)** — la discriminazione è via ADC: PE13 avvia il T_HOLD, il
**livello su PA3** decide HCE/SEL.

| Pin Arduino | → STM32 (connettore) | Ruolo nel test |
|---|---|---|
| **D8** ─[1 kΩ]→ | **PE13** = CN10-10 (D3) | fronte di discesa → avvia T_HOLD |
| **uscita analogica** → | **PA3** = CN9-1 (A0 Nucleo) | impone la corrente (0–3,3 V): sopra soglia = SEL, sotto = HCE; sopra dopo il power-cycle = corto persistente |
| **D2** ← | **PG14** = CN10-12 (D2) | monitor apertura switch |
| **A0** ← | **PA4** = CN7-17 | monitor V_LIMIT |
| (opz.) ingresso ← | **PE14** = CN10-8 (D4) | legge l'impulso RST del MCU |
| **GND** ↔ | GND (es. CN10-5/11/17) | massa comune |

Diagnostica (non-core): LD1 verde PB0, LD2 giallo PE1, LD3 rosso PB14; Ethernet RMII
(PA1/PA2/PA7/PC1/PC4/PC5/PB13/PG11/PG13). Dettaglio completo in `Mappatura_Pin_AntiSEL.md`.

---

## 2. Determinazione delle soglie preset (§8.2) — OPZIONALE (rimandata)

> **Preset non usati in questa fase.** La soglia I_TH si imposta con lo **slider**
> (`DAC_SET`); vedi T2.1/T2.2. Questa procedura e il caso T2.3 vanno eseguiti
> **prima della campagna con fascio**, quando la §8.2 (tre soglie precaricate e
> commutabili dal PC senza interventi hardware) diventa requisito operativo.

| Passo | Azione | Atteso |
|---|---|---|
| P1 | DUT alimentato a 6 V, +85 °C, config di test; leggi la corrente sul grafico 10 Hz | valore Icc_nom stabile |
| P2 | Annota **Icc_nom** (baseline) | — |
| P3 | Slider I_TH = 1,5×Icc_nom → "Carica 1" (benchmark) | `TH_LOAD 1 ... OK` |
| P4 | Slider I_TH = 1,25×Icc_nom → "Carica 2" (stringente) | `TH_LOAD 2 ... OK` |
| P5 | Slider I_TH = 2×Icc_nom → "Carica 3" (permissivo) | `TH_LOAD 3 ... OK` |
| P6 | "Usa 1/2/3": verifica che la soglia sul grafico si sposti al valore atteso | traccia soglia allineata |

Valori esatti **TBD con TASI**; range ammesso 1–50 mA (R-02).

<div style="page-break-after: always;"></div>

## 3. Casi di test

Legenda esito: ☐ non eseguito · ✔ pass · ✘ fail (annotare).

### G1 — Connettività e comandi

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T1.1 | — | Connetti alla NUCLEO | Stato CONNESSO, RTT, file CSV di run creati | ☐ |
| T1.2 | — | PING | PONG con RTT | ☐ |
| T1.3 | — | STATUS | `OK STATUS=IDLE …` | ☐ |
| T1.4 | — | Comando ignoto | risposta `ACK` | ☐ |
| T1.5 | — | Disconnetti/riconnetti | chiusura pulita, nuovi CSV | ☐ |

### G2 — Soglia I_TH, DAC e preset (§8.2)

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T2.1 | R-02 | Muovi slider I_TH (1→50 mA) | un solo `DAC_SET` per fermata (debounce); "DAC read" coerente | ☐ |
| T2.2 | R-02 | Misura V_LIMIT su PA4 (multimetro) vs valore atteso | scarto entro tolleranza | ☐ |
| T2.3 | §8.2 | Carica 1/2/3 poi Usa 1/2/3 — **rimandato (preset non usati)** | da eseguire prima della campagna con fascio | N/A |
| T2.4 | R-02 | Estremi slider (1 e 50 mA) | nessun overflow, DAC saturato correttamente | ☐ |

### G3 — Temporizzazioni (R-03, R-04)

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T3.1 | R-03 | THOLD_SET a 1, 5, 10 ms | risposta coerente; valore usato dalla macchina a stati | ☐ |
| T3.2 | R-04 | TON_SET a 1, 5, 10 ms | risposta coerente | ☐ |
| T3.3 | R-03/04 | (Setup A) misura con scope la durata switch aperto vs T_ON impostato | scarto ≤ risoluzione (≤100 µs) | ☐ |

### G4 — Discriminazione HCE/SEL via ADC (R-05)

| ID | Req | Passi (Setup A) | Atteso | Esito |
|---|---|---|---|---|
| T4.1 | R-05 | T_HOLD=5 ms. Trigger su PE13 + PA3 **sotto soglia** a fine T_HOLD (rientro) | classificato **HCE**: Eventi HCE +1, SEL invariato, nessuno sgancio | ☐ |
| T4.2 | R-05 | Trigger PE13 + PA3 **sopra soglia** oltre T_HOLD | classificato **SEL**: Eventi SEL +1, switch si apre in T_ON | ☐ |
| T4.3 | R-05 | Sweep del livello PA3 attorno alla soglia | il confine HCE/SEL cade alla soglia I_TH impostata | ☐ |

### G5 — Protezione latched e policy di riarmo

| ID | Req | Passi (Setup A) | Atteso | Esito |
|---|---|---|---|---|
| T5.1 | R-01 | SEL singolo **recuperabile** (PA3 alta in T_HOLD/T_ON, poi bassa) | power-cycle, poi recupero pulito entro T_CLEAR → IDLE; Tentativi tornano 0 | ☐ |
| T5.2 | — | Molti SEL recuperabili in sequenza | Eventi SEL cresce; **nessun** PERMANENT_OFF (contatore ri-scatti resettato) | ☐ |
| T5.3 | — | **Corto persistente** (PA3 resta alta anche dopo il power-cycle) | ri-scatti consecutivi 1→N; all'**N-esimo** → PERMANENT_OFF; switch aperto | ☐ |
| T5.4 | — | Cambia N con `RETRY_SET` (es. 2 e 5) e ripeti T5.3 | PERMANENT_OFF esattamente all'N impostato | ☐ |
| T5.5 | — | Cambia `TCLEAR_SET` e verifica finestra di recupero | recupero giudicato dopo T_CLEAR | ☐ |
| T5.6 | §4.3 | In PERMANENT_OFF premi "Reset allarme" (INA_RST) e RESET | latch sbloccato; RESET → IDLE, DUT ON riabilitato | ☐ |

### G6 — Acquisizione dati e grafici (R-06, R-08, §6)

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T6.1 | R-08 | Lascia girare il log 10 Hz | grafico corrente aggiornato ~10 Hz; CSV `_log10hz` con timestamp | ☐ |
| T6.2 | R-06 | Genera un SEL e un HCE | `TRACE_START…END` per entrambi; CSV `_trace_*`; grafico "ultima traccia" mostra la forma | ☐ |
| T6.3 | §6.3 | Verifica nomi file `<DUT>_<LET>_<RUN>_<ts>_*` | nomenclatura corretta | ☐ |
| T6.4 | §5.3 | Verifica CSV eventi (override, cambi parametro, tracce) | riga per ogni comando con timestamp | ☐ |
| T6.5 | — | Pulsanti Pausa / Azzera dei grafici | Pausa congela, Azzera svuota; i dati riprendono dopo | ☐ |

### G7 — Override manuale e sicurezza (R-07)

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T7.1 | R-07 | DUT OFF poi DUT ON (da IDLE) | switch commuta; eventi loggati | ☐ |
| T7.2 | R-07 | In PERMANENT_OFF prova DUT ON | pulsante disabilitato; da comando manuale richiede conferma | ☐ |

### G8 — Robustezza

| ID | Req | Passi | Atteso | Esito |
|---|---|---|---|---|
| T8.1 | — | Stacca il cavo Ethernet durante un run | GUI rileva "Connessione persa", chiusura pulita | ☐ |
| T8.2 | — | Raffica di comandi rapidi (slider I_TH) | nessuna perdita di sync; DAC coerente | ☐ |
| T8.3 | — | Eventi ravvicinati (traccia in invio) | comportamento best-effort documentato; contatori corretti | ☐ |

---

## 4. Criteri di accettazione

- Tutti i requisiti R-01…R-08 coperti da almeno un caso con esito ✔.
- Discriminazione HCE/SEL corretta al confine soglia (T4.3).
- Policy di riarmo: SEL recuperabili non portano a PERMANENT_OFF (T5.2); corto
  persistente porta a PERMANENT_OFF all'N-esimo (T5.3/T5.4).
- Tracce e log 10 Hz completi e archiviati con nomenclatura §6.3.
- Nessuna anomalia bloccante in G8.

## 5. Note per la campagna con fascio (Setup B)

- Ripetere G4/G5/G6 con eventi **reali** indotti dal fascio (LET 2, 5, 15, 34, 60
  MeV·cm²/mg; fluence target 1×10⁷ cm⁻²; §8.1).
- Verificare che T_ON sia sufficiente al de-latch del DUT reale (tarare se necessario).
- Cross-section SEL = conteggio SEL / fluence; il log 10 Hz e le tracce forniscono i dati.
- Punti aperti da chiudere prima del TRR (§8.4): sampling rate/banda acquisizione veloce,
  R_SHUNT definitivo, calibrazione DAC, eventuale beam-shutter.
