# Mappatura pin AntiSEL — NUCLEO-H755ZI-Q

Aggiornata allo stato attuale del firmware: **INA301 in modo latched**, ALARM rimappato
su **PE13**, **RST implementato su PE14**, discriminazione HCE/SEL **via ADC**.
Posizioni connettori verificate su UM2408 (MB1363), Tabella 17.

## Segnali core AntiSEL

| Segnale (architettura) | Pin STM32 | Periferica / config | Connettore | Ruolo | Req |
|---|---|---|---|---|---|
| **ALARM** (INA301 → MCU) | **PE13** ¹ | EXTI13, fronte discesa, **pull-up interno** | **CN10 pin 10** (Arduino D3) | ingresso interrupt: avvia il T_HOLD (§4.1, §5.1) | R-01/R-05 |
| **RST** (MCU → INA301) | **PE14** ² | GPIO output PP, riposo **HIGH = latched** | **CN10 pin 8** (Arduino D4) | impulso LOW per azzerare il latch a fine T_ON (§4.3, §5.1 RECOVERY) | — |
| **V_LIMIT** (DAC → INA301 LIMIT) | **PA4** | DAC1_OUT1 | **CN7 pin 17** (D24) | soglia I_TH generata dal DAC (§4.3) | R-02 |
| **I_sense** (INA301 OUT → ADC) | **PA3** | ADC1_INP15 | **CN9 pin 1** (A0) | traccia corrente + **discriminazione HCE/SEL** (§4.3, §6.1) | R-06 |
| **EN** (MCU → TPS22810) | **PG14** | GPIO output | **CN10 pin 12** (Arduino D2) | power switch / power-cycle (§4.2) | R-07 |

¹ Rimappato da PC13 (pulsante blu, non accessibile) a PE13.
² Aggiunto con la patch "modo latched": prima non implementato.

## Infrastruttura / diagnostica (non-core)

| Segnale | Pin STM32 | Ruolo |
|---|---|---|
| LD1 verde | PB0 | heartbeat "firmware vivo" |
| LD2 giallo | PE1 | Ethernet link UP |
| LD3 rosso | PB14 | Ethernet link DOWN |
| Ethernet RMII | PA1, PA2, PA7, PC1, PC4, PC5, PB13, PG11, PG13 | rete locale + GUI via TCP (§4.4) → log 10 Hz R-08 |
| USART3 (VCP) | PD8 / PD9 | COM virtuale ST-Link (non usata dalla GUI) |
| USB OTG FS | PA9 / PA11 / PA12 | USB device |
| Clock / Debug | PH0, PH1, PC14, PC15 / PA13, PA14 (SWD) | oscillatori / debug |

## Arduino (emulatore INA301, solo per il collaudo)

Con la discriminazione via ADC: **PE13 avvia soltanto il T_HOLD**; a classificare
HCE/SEL è il **livello di corrente su PA3**. Quindi PA3 è **obbligatorio**, e la larghezza
dell'impulso su PE13 non è più rilevante per la classificazione.

| Pin Arduino | → STM32 (connettore) | Ruolo nel test |
|---|---|---|
| **D8** ─[1 kΩ]→ | **PE13** = CN10-10 (D3) | genera il **fronte di discesa** che avvia il T_HOLD (EXTI) |
| **uscita analogica** → | **PA3** = CN9-1 (A0 Nucleo) | **impone la corrente** (0–3,3 V): sopra soglia a fine T_HOLD = **SEL**, sotto = **HCE**; sopra anche dopo il power-cycle = **corto persistente** → PERMANENT_OFF |
| **D2** ← | **PG14** = CN10-12 (D2) | monitor apertura switch (DUT_SWITCH) |
| **A0** ← | **PA4** = CN7-17 | monitor V_LIMIT (DAC) |
| (opz.) ingresso ← | **PE14** = CN10-8 (D4) | legge l'impulso RST del MCU (per rilasciare il latch emulato) |
| **GND** ↔ | GND (es. CN10-5/11/17) | massa comune |

Attenzione al doppio "A0": l'**A0 dell'Arduino** (ingresso) legge PA4; l'**A0 della Nucleo** è PA3.
