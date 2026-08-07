# Proposta front-end di acquisizione a basso rumore — ADC esterno isolato (AD7176-2)

Stato: proposta di progetto, non ancora implementata. Riguarda il percorso di
misura I_sense (uscita INA301 → ADC), non il resto del firmware AntiSEL, che
resta invariato.

> Nota di revisione: la versione precedente di questo documento proponeva
> l'AD7124-8 con architettura a doppio percorso (ADC1 interno per la traccia
> veloce + AD7124-8 per la telemetria a basso rumore), per aggirare il fatto
> che l'AD7124-8 non supera 19,2 kSa/s. Dopo un confronto tra alternative
> (§2) si è individuato l'**AD7176-2**, che copre sia la velocità richiesta
> sia il rumore basso con un solo convertitore, semplificando l'architettura
> a un percorso unico. L'AD7124-8/ADS1262 restano citati come alternative
> per il solo caso in cui serva il rumore minimo assoluto accettando di
> tornare al doppio percorso (§2, nota finale).

## 1. Contesto e motivazione

Il percorso di misura attuale è interamente interno all'STM32H755:

```
INA301 OUT (PA3) → ADC1_INP15 (16 bit, single-ended) → DMA → antisel_acquisition.c
```

Vedi [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md) per l'assegnazione
pin corrente e [CM7/Src/adc.c](../CM7/Src/adc.c) per la configurazione ADC.

Il problema segnalato è un rumore di misura dell'ordine del mA/mV, incompatibile
con l'uso previsto (discriminazione HCE/SEL su piccole variazioni di corrente).
La causa più probabile non è il modello di MCU in sé, ma la condivisione fisica
di alimentazione, massa e clock tra un core digitale a 480 MHz con SMPS
integrato e l'ADC che misura il segnale INA301: qualunque MCU general-purpose
nella stessa condizione (Arduino, Raspberry Pi) avrebbe lo stesso problema o
peggiore.

La soluzione proposta mantiene l'STM32H755 come controllore/protocollo e
sposta la misura di precisione su un ADC esterno sigma-delta a 24 bit,
isolato galvanicamente dal dominio digitale.

## 2. Vincolo di progetto e scelta del convertitore

`antisel_acquisition.c` acquisisce **a 100 kSa/s** (`ACQ_SAMPLE_RATE_HZ`,
TIM6 TRGO + DMA circolare) per catturare la forma d'onda della corrente
attorno a un evento (pre-trigger + traccia congelata, §4.1 spec). Qualunque
sostituto dell'ADC1 interno deve reggere questa velocità, oltre a offrire un
rumore molto inferiore a quello attuale.

Gli ADC sigma-delta a basso rumore in genere scambiano velocità per rumore
(più lenta la data rate, più basso il rumore): un ADC ottimizzato solo per il
rumore minimo assoluto (come l'AD7124-8, valutato per primo) resta ben sotto
i 100 kSa/s richiesti. Confronto dei candidati valutati:

| Componente | Data rate max | Rumore / risoluzione | Idoneità ai 100 kSa/s |
|---|---|---|---|
| **AD7176-2** (ADI) — **scelto** | **250 kSPS** | 17,2 bit noise-free a 250 kSPS; 22 bit noise-free a 5 SPS (stesso chip, data rate riconfigurabile via SPI) | Sì, con margine |
| AD7175-2 (ADI) | 50 kSPS | Prestazioni di rumore simili ad AD7176-2 | No |
| AD7124-8 (ADI) | 19,2 kSPS | Fino a 22 bit noise-free (guadagno 1); ~20–24 nV RMS a guadagno 128 e data rate minima | No |
| ADS1262/ADS1263 (TI) | 38,4 kSPS | 32 bit, tra i rumori più bassi della categoria a bassa data rate | No |
| ADS131M04 (TI) | 64 kSPS (revisione corrente) | Pensato per shunt/CT, campionamento simultaneo multicanale | No |

**Scelta: AD7176-2.** È l'unico candidato che copre sia la cattura veloce
della traccia evento sia la misura a basso rumore **con lo stesso
convertitore**, riconfigurando la data rate via SPI invece di dover
duplicare l'hardware. Questo elimina la necessità del doppio percorso
(ADC1 + ADC esterno) ipotizzato nella prima versione di questa proposta.

**Nota per il caso "rumore minimo assoluto" (non l'ipotesi di base
di questo documento):** se in futuro il rumore ottenuto con l'AD7176-2 in
modalità lenta (22 bit noise-free a 5 SPS) non fosse sufficiente, l'AD7124-8
o l'ADS1262/1263 restano un'opzione, ma solo reintroducendo l'architettura a
doppio percorso del §3 (un ADC lento a rumore minimo per la telemetria +
l'ADC1 interno, o un secondo ADC veloce, per la traccia evento), perché
nessuno dei due regge 100 kSa/s da solo.

## 3. Architettura proposta

```
                    ┌─────────────────────────────┐
                    │        INA301 (esistente)     │
                    │  OUT ── analog ────────────────┼──► AIN (AD7176-2)
                    │  ALARM ── digitale (invariato, EXTI13/PE13)
                    │  RST   ── digitale (invariato, PE14)
                    └──────────────┬────────────────┘
                                   ▼
                        ┌───────────────────────────┐
                        │   AD7176-2 (nuovo, 24-bit) │
                        │   dominio analogico isolato │
                        │   data rate riconfigurabile: │
                        │   ~100–250 kSPS (traccia) ↔  │
                        │   ~5–100 SPS (telemetria)   │
                        └──────────────┬────────────┘
                                       │ SPI isolato (4 segnali)
                              ┌────────▼─────────┐
                              │ ISOW7841 (isolatore│
                              │ + DC/DC integrato) │
                              └────────┬──────────┘
                                       │ SPI non isolato
                                       ▼
                              STM32H755 CM7 (SPIx)
```

Punti chiave:
- **INA301, ALARM, RST, V_LIMIT (DAC) restano esattamente come oggi.** La
  protezione hardware (latch INA301) non dipende dal software né dall'ADC:
  non viene toccata da questa modifica.
- **Un solo ADC esterno** copre sia la cattura veloce dell'evento sia la
  telemetria a basso rumore: il firmware seleziona la data rate (registro
  filtro dell'AD7176-2) in base allo stato della macchina AntiSEL — data
  rate alta durante la finestra di cattura traccia (equivalente all'uso
  attuale di ADC1/TIM6), data rate bassa nel resto del tempo (modo
  transparent, monitoraggio continuo).
- **ADC1 interno (PA3, `antisel_acquisition.c`) può restare presente ma
  disattivato come fallback** durante la fase di validazione (si veda §8),
  per non dover fidarsi "al buio" del nuovo percorso fin dal primo bring-up;
  va rimosso solo dopo aver verificato che l'AD7176-2 cattura correttamente
  la traccia a 100 kSa/s nelle condizioni reali.
- Un solo package (**ISOW7841**) fornisce sia l'isolamento dati SPI sia
  l'alimentazione isolata lato ADC — stessa interfaccia a 4 fili
  (SCLK/DIN/DOUT-RDY/CS) sia per l'AD7176-2 sia per l'AD7124-8, quindi la
  scelta del convertitore non cambia questa parte del progetto.

## 4. Distinta componenti

| Rif. | Componente | Ruolo | Specifiche rilevanti (verificate da datasheet) |
|---|---|---|---|
| U1 | **AD7176-2** (Analog Devices) | ADC sigma-delta 24 bit, basso rumore, alta velocità | Data rate 5 SPS–250 kSPS; 17,2 bit noise-free a 250 kSPS, 22 bit noise-free a 5 SPS; interfaccia seriale 3/4 fili compatibile SPI; alimentazione AVDD1 5 V, AVDD2/IOVDD 2–5 V (singola alimentazione) oppure alimentazione split AVDD1/AVSS ±2,5 V |
| U2 | **ISOW7841** (Texas Instruments) | Isolatore digitale quad-channel (3 fwd / 1 rev) con DC/DC isolato integrato | Isolamento rinforzato 5000 V RMS; DC/DC integrato fino a 650 mW; 3 canali diretti (SCLK, CS, DIN) + 1 canale inverso (DOUT/RDY) — combacia con le 4 linee SPI dell'AD7176-2 (DOUT e RDY condividono lo stesso pin, come nella famiglia AD7124) |
| — | AD7124-8 (ADI) o ADS1262/ADS1263 (TI) | Alternative, **non la proposta di base** | Da usare solo se si torna all'architettura a doppio percorso per un rumore ancora inferiore sul solo ramo telemetria (§2, nota finale) |

Nota: i part number sono verificati sul catalogo/datasheet del produttore al
momento della stesura; controllarne la disponibilità/lead time prima
dell'ordine, poiché possono cambiare nel tempo.

## 5. Segnali e collegamento

| Segnale | Lato AD7176-2 | Attraverso | Lato STM32H755 (CM7) |
|---|---|---|---|
| SCLK | SCLK | ISOW7841 canale diretto | SPIx_SCK |
| CS | CS̄ (active low) | ISOW7841 canale diretto | SPIx_NSS (GPIO gestito da firmware) |
| DIN (MOSI) | DIN | ISOW7841 canale diretto | SPIx_MOSI |
| DOUT/RDY (MISO) | DOUT/RDY (pin condiviso: dati in uscita + flag "conversione pronta") | ISOW7841 canale inverso | SPIx_MISO |
| Alimentazione analogica (AVDD1/AVDD2) | dominio isolato | uscita DC/DC integrata ISOW7841 | non collegata a GND/VDD digitali dell'MCU |
| Alimentazione digitale ADC (IOVDD/DGND) | dominio isolato | uscita DC/DC integrata ISOW7841 | non collegata a GND/VDD digitali dell'MCU |
| Ingresso analogico (AINx) | ingresso single-ended (coerente con l'uso attuale single-ended su ADC1_INP15) | derivazione ad alta impedenza dall'uscita INA301 | — |

**Non ancora definiti in questo documento (da completare in fase di
schematico/CubeMX, per non introdurre numeri non verificati):**
- Piedinatura fisica esatta (numero pin) dell'AD7176-2: il pinout va preso
  dalla Figura "Pin Configuration" del datasheet corrente al momento del
  disegno dello schematico.
- Periferica SPI e pin GPIO specifici dell'STM32H755 da usare: nessuna SPI è
  oggi configurata nel progetto ([CM7/Inc/main.h](../CM7/Inc/main.h) non
  definisce pin SPI), quindi non ci sono conflitti noti con i pin già
  mappati in [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md) — ma
  l'assegnazione finale va fatta in STM32CubeMX per verificare le funzioni
  alternate (AF) e la disponibilità sui connettori fisici della
  NUCLEO-H755ZI-Q, non a tavolino.
- Configurazione dell'alimentazione dell'AD7176-2 (singola vs. split) e
  della tensione di uscita del DC/DC integrato nell'ISOW7841: l'AD7176-2
  supporta più modalità di alimentazione (datasheet, sezione "Power
  Supplies"), la combinazione va scelta in fase di schematico.

## 6. Linee guida di layout (per ridurre il rumore al minimo)

1. **Piani di massa separati**: massa analogica (lato AD7176-2/INA301) e
   massa digitale (lato STM32) fisicamente distinte, unite in un solo punto
   (star point) o non unite affatto se l'isolamento è totale (preferibile,
   dato l'uso dell'ISOW7841).
2. **Nessuna traccia digitale sopra il piano analogico**: SCLK/DIN/CS/DOUT
   isolati vanno instradati lontano dal piano di massa analogico.
3. **Decoupling locale**: condensatori di bypass (100 nF + 10 µF tipico)
   il più vicino possibile ai pin di alimentazione dell'AD7176-2, su
   entrambi i domini (AVDD, IOVDD).
4. **Filtro anti-aliasing** passivo (RC, banda da definire in base alla
   data rate scelta) tra l'uscita INA301 e l'ingresso AIN dell'AD7176-2.
5. **Schermatura** del percorso INA301 → AD7176-2 se la lunghezza di pista
   non è trascurabile rispetto al livello di segnale atteso (µV/mV).

## 7. Integrazione firmware — scheletro driver SPI AD7176-2

Lo scheletro sotto descrive la sequenza funzionale standard per un ADC di
precisione ADI a interfaccia SPI (reset, configurazione canale/filtro,
lettura dato), basata sulla struttura nota di questa classe di convertitori.
**I nomi dei registri, gli indirizzi e i campi bit esatti della famiglia
AD7175/AD7176 (diversa, come denominazioni interne, dalla famiglia AD7124
citata nella versione precedente di questo documento: qui si usano ad es.
ADC_MODE, IFMODE, CHMAP, SETUPCON, FILTCON invece di ADC_CONTROL/CH/CFG/FILT)
vanno confermati sul datasheet AD7176-2 corrente, sezione "Register Details",
prima di scrivere il driver definitivo.**

```c
/* ad7176.h — scheletro, NON verificato bit-a-bit contro il datasheet.
 * Prima dell'uso: confermare indirizzi/campi in AD7176-2 datasheet,
 * sezione "Register Details" (Comms/Status, ADC_MODE, IFMODE, Channel Map,
 * Setup Config, Filter Config, Offset, Gain, ID). */

#include <stdint.h>
#include <stdbool.h>

/* Nomi di registro noti a livello di famiglia — INDIRIZZI DA CONFERMARE */
typedef enum {
  AD7176_REG_COMMS_STATUS = 0x00, /* R/W: comms (scrittura) / status (lettura) */
  AD7176_REG_ADC_MODE     = 0x01,
  AD7176_REG_IFMODE       = 0x02,
  AD7176_REG_DATA         = 0x04,
  AD7176_REG_ID           = 0x07,
  AD7176_REG_CHMAP0       = 0x10, /* mappa canale -> AIN, abilita/disabilita */
  AD7176_REG_SETUPCON0    = 0x20, /* guadagno/PGA, unipolare/bipolare, riferimento */
  AD7176_REG_FILTCON0     = 0x28, /* data rate / filtro (velocità <-> rumore) */
} ad7176_reg_t;

/* API di alto livello che il resto del firmware userebbe al posto di
 * HAL_ADC_Start_DMA()/lettura ADC1 per il percorso di misura. */
bool AD7176_Init(void);                 /* reset, verifica ID, config canale/filtro */
bool AD7176_SetFastMode(bool fast);     /* riconfigura FILTCON0: ~100-250 kSPS
                                            (traccia evento) vs ~5-100 SPS
                                            (telemetria a basso rumore) */
bool AD7176_IsDataReady(void);          /* legge DOUT/RDY o STATUS.RDY */
bool AD7176_ReadRaw(int32_t *out_code); /* lettura registro DATA (24 bit, sign-extend) */
float AD7176_CodeToVoltage(int32_t code, float vref, bool bipolar);

/* Sequenza di init prevista (a livello funzionale):
 * 1. Reset del dispositivo (>= 64 cicli SCLK con CS basso — verificare se
 *    questa parte prevede anche un bit di reset alternativo in IFMODE).
 * 2. Lettura registro ID e confronto col valore atteso da datasheet, per
 *    verificare la comunicazione SPI prima di proseguire.
 * 3. Scrittura ADC_MODE: sorgente di clock, power mode, modalità di
 *    conversione (continuous/single).
 * 4. Scrittura CHMAP0: abilita il canale, mappa AIN, associa a SETUPCON0.
 * 5. Scrittura SETUPCON0: guadagno, unipolare/bipolare (qui: unipolare,
 *    coerente con l'uscita INA301), sorgente di riferimento.
 * 6. Scrittura FILTCON0: data rate — questo è il registro che
 *    AD7176_SetFastMode() riscrive per passare da modo traccia a modo
 *    telemetria e viceversa.
 * 7. A regime: polling di DOUT/RDY (o interrupt su GPIO se instradato)
 *    e lettura di DATA a ogni conversione pronta.
 */
```

Nel firmware esistente, il punto di innesto naturale è accanto a
`INA301_AdcToVoltage()`/`INA301_AdcToCurrent()` in
[CM7/Src/ina301.c](../CM7/Src/ina301.c): oggi queste funzioni convertono un
codice `uint32_t adc_raw` a 16 bit proveniente da ADC1; con l'AD7176-2
diventerebbe un codice a 24 bit con proprio fondo scala
(`AD7176_...` invece di `INA301_ADC_FULL_SCALE`). A differenza della prima
versione di questa proposta, qui **non serve tenere due percorsi di
conversione separati nel firmware**: la stessa funzione di lettura serve sia
per la traccia veloce sia per la telemetria, cambia solo la data rate
impostata a monte con `AD7176_SetFastMode()`.

## 8. Piano di verifica

Prima di considerare il lavoro concluso:

1. **Misura di rumore a vuoto**: con ingresso AD7176-2 cortocircuitato/a
   massa, misurare il rumore RMS in uscita sia in modo veloce sia in modo
   lento, confrontandolo col valore atteso da datasheet (§2).
2. **Verifica cattura traccia a 100 kSa/s**: validare che l'AD7176-2 in
   modo veloce catturi correttamente la forma d'onda dell'evento con la
   stessa fedeltà (o migliore) dell'ADC1 interno attuale, prima di
   rimuovere il percorso ADC1/TIM2/TIM6 esistente. Fino a verifica
   completata, mantenere ADC1 come fallback disattivabile via `#define`
   (analogamente a `DUAL_CORE_BOOT_SYNC_SEQUENCE` già presente nel
   firmware) invece di eliminarlo subito.
3. **Misura di rumore in condizioni reali**: con INA301 collegato e sistema
   in funzione, confrontare il rumore RMS del nuovo percorso con quello
   attuale su ADC1 (stessa condizione di test, stesso R_shunt/gain).
4. **Verifica indipendenza dal core digitale**: confrontare il rumore con
   CM7 in idle vs. sotto carico (LwIP attivo, invio telemetria) per
   confermare che l'isolamento elimina l'accoppiamento osservato oggi.
5. **Solo se il punto 1/3 non soddisfa il target di rumore**: rivalutare
   l'opzione a doppio percorso con AD7124-8/ADS1262 (§2, nota finale).

## 9. Fonti consultate

- [AD7176-2 — Product page, Analog Devices](https://www.analog.com/en/products/ad7176-2.html)
- [AD7176-2 — 24-Bit, 250 kSPS Sigma-Delta ADC with 20 µs Settling, Datasheet (PDF)](https://www.analog.com/media/en/technical-documentation/data-sheets/ad7176-2.pdf)
- [AD7175-2 — Product page, Analog Devices](https://www.analog.com/en/products/ad7175-2.html)
- [AD7124-8 — Product page, Analog Devices](https://www.analog.com/en/products/ad7124-8.html)
- [ADS1262, ADS1263 — Data Sheet (PDF), Texas Instruments](https://www.ti.com/lit/ds/symlink/ads1263.pdf)
- [ADS131M04 — 4-Channel Simultaneously-Sampling 24-Bit Delta-Sigma ADC, Data Sheet (PDF), Texas Instruments](https://www.ti.com/lit/ds/symlink/ads131m04.pdf)
- [ISOW7841 — Product page, Texas Instruments](https://www.ti.com/product/ISOW7841)
- [ISOW7841 — Datasheet, Alldatasheet](https://www.alldatasheet.com/html-pdf/913123/TI1/ISOW7841/360/6/ISOW7841.html)
