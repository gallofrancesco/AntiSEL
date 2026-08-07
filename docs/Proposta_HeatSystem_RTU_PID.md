# Proposta Heat System (RTU/PID) — due alternative architetturali

Stato: proposta di progetto, **approvata nel principio ma non ancora
implementata** — resta da scegliere tra le due opzioni architetturali
descritte qui (§2) prima di procedere. In entrambi i casi il CM7 e la GUI
(`antisel_dashboard_eth.py`) non sono toccati da questa proposta.

## Prompt pronto per una sessione AI futura (esecuzione)

Quando si deciderà di procedere, incollare il seguente prompt in una nuova
sessione (Claude Code o altro agente con accesso al filesystem):

```
Implementa il piano di integrazione dell'Heat System (RTU/PID) descritto in
docs/Proposta_HeatSystem_RTU_PID.md (repo AntiSEL). Il documento presenta
DUE opzioni architetturali alternative (§2: Opzione A = CM4+W5500 sulla
stessa NUCLEO-H755ZI-Q; Opzione B = secondo NUCLEO-H753ZI con Ethernet
nativa). Se l'utente non ha già indicato quale scegliere, chiedilo prima di
iniziare a scrivere codice — non assumere.

Contesto essenziale (comune a entrambe le opzioni):
- Repo firmware principale: c:\Users\darthrooster\Documents\STM_Dev\AntiSEL
  (STM32CubeIDE, dual-core STM32H755 su NUCLEO-H755ZI-Q, progetti CM7/ e
  CM4/). Il CM7 gestisce la protezione SEL/HCE (INA301, ADC, TCP porta 7755,
  LwIP) e NON va toccato da questo lavoro, in nessuna delle due opzioni.
- Repo GUI: C:\Users\darthrooster\Documents\STM_Dev\AntiSel_GUI
  (antisel_dashboard_eth.py, Python/customtkinter) — contiene già un
  pannello "PID CTRL + RTU" placeholder e funzionante che si connette a
  192.168.1.101:7756 e parla GET TEMP / GET PID / SET SETPOINT_C. Non
  modificarlo, salvo eventuali aggiustamenti minori di parsing scoperti in
  fase di validazione con hardware reale (§5 del documento).
- Il lavoro da fare consiste nell'implementare il dispositivo che deve
  ascoltare su 192.168.1.101:7756, leggere un MAX31865 (RTD) e pilotare in
  PWM proporzionale le resistenze di riscaldamento — sull'Opzione A questo
  vive sul CM4 della stessa scheda, sull'Opzione B su un secondo Nucleo
  fisicamente separato.
- Prima di scrivere codice: verificare in CubeMX le istanze SPI/TIM/pin
  libere sulla scheda target scelta (per l'Opzione A, sul CM4 della
  NUCLEO-H755ZI-Q; per l'Opzione B, sulla NUCLEO-H753ZI). Verificare gli
  indirizzi di registro esatti di W5500 (solo Opzione A) e MAX31865 (in
  entrambe) sui rispettivi datasheet/librerie ufficiali prima di finalizzare
  i driver — non inventare valori di registro.
- Segui la struttura file, i nomi di modulo/funzione e le decisioni di
  sicurezza (cutoff termico locale, watchdog di disconnessione) descritte
  nel documento. Chiedi conferma sulle "Decisioni ancora aperte" (§7) prima
  di procedere con i dettagli che richiedono una scelta dell'utente (wiring
  RTD, tuning PID, soglia cutoff, timeout watchdog, e per l'Opzione B dove
  collocare il nuovo progetto firmware).
```

## 1. Contesto

`docs/AntiSEL_System_Description.pdf` (GST-TSP-13, Fig. 1) descrive l'Heat
System come controllato da un PID controller + RTU **esterni**, collegati
alla stessa LAN di Nucleo e PC ma indipendenti dalla scheda di controllo.

Esplorando `C:\Users\darthrooster\Documents\STM_Dev\AntiSel_GUI\antisel_dashboard_eth.py`
è emerso che questo lato è **già stato implementato lato GUI** (commit
`5e05c20`, `97b1864`, `4778393`, `1c17688`): esiste un secondo client TCP,
completamente indipendente dal collegamento alla Nucleo
(`RTU_HOST="192.168.1.101"`, `RTU_PORT=7756`, socket/thread/coda propri,
righe 30-38 e 756-892), con un pannello "PID CTRL + RTU" (grafico
temperatura/setpoint, metriche PWM/stato) e un protocollo testuale placeholder
(`GET TEMP`, `GET PID`, `SET SETPOINT_C <°C>` → risposte `TEMP=`, `PWM=`,
`STATE=`). Il commento in testa al file lo dichiara esplicitamente
provvisorio, in attesa dell'hardware reale.

**Quello che manca è l'altro capo**: il dispositivo che deve ascoltare su
`192.168.1.101:7756`, leggere un **MAX31865** (RTD) e pilotare in **PWM
proporzionale** le resistenze di riscaldamento.

**Decisioni già confermate, valide per entrambe le opzioni**: attuatore
**PWM proporzionale** (non on/off); **un solo** canale RTD/MAX31865; alla
perdita del client TCP (GUI disconnessa) in modo AUTO il riscaldatore **si
spegne** (watchdog locale, non dipendente dalla rete/dal resto del sistema —
stessa filosofia della protezione INA301 R-01: sicurezza locale, non
GUI-dipendente).

## 2. Le due opzioni a confronto

| | **Opzione A — CM4 + W5500** | **Opzione B — secondo NUCLEO-H753ZI** |
|---|---|---|
| Hardware aggiuntivo | Modulo W5500 con RJ45 integrato + MAX31865 + stadio PWM | Scheda NUCLEO-H753ZI intera (~$40) + MAX31865 + stadio PWM |
| Scheda/e coinvolte | Una sola (la NUCLEO-H755ZI-Q esistente) | Due schede fisicamente separate |
| Riuso hardware esistente | Sì — CM4 oggi inattivo | No — nuova scheda dedicata |
| Riuso software esistente | Parziale — nuova libreria (ioLibrary_Driver) da integrare, protocollo/socket diverso da LwIP | **Alto** — stesso pattern LwIP + server TCP raw già scritto e collaudato in `CM7/Src/lwip.c`/`antisel_protocol.c` |
| Toolchain | Stesso progetto STM32CubeIDE, nuovo core (CM4) | Nuovo progetto STM32CubeIDE separato, stesso HAL/famiglia H7 |
| Isolamento fisico dal banco SEL/HCE | Logico (core separato), fisicamente sullo stesso PCB/alimentazione | **Reale** — scheda e alimentazione indipendenti |
| Complessità di bring-up | Porting di ioLibrary_Driver su HAL SPI, non documentato per questa scheda specifica | Nessun porting nuovo: stesso stack Ethernet già funzionante in questo repo |
| Fedeltà a RD04 (Fig. 1) | Rispettata logicamente (nodo di rete indipendente), ma implementata come co-locazione fisica | Rispettata **anche fisicamente** — dispositivo separato come nel disegno originale |

Entrambe le opzioni condividono tutto il resto: stesso MAX31865, stesso
attuatore PWM, stesso protocollo verso la GUI, stessa logica di sicurezza
(§4). Cambia solo *dove* vive questo codice e *come* ottiene l'Ethernet.

## 3. Opzione A — CM4 + W5500 sulla stessa NUCLEO-H755ZI-Q

### 3.1 Hardware

- **W5500 con porta Ethernet fisica integrata** (modulo Ethernet-su-SPI,
  stack TCP/IP hardware integrato — 8 socket hardware, niente bisogno di
  portare LwIP sul CM4): serve un modulo con **connettore RJ45 con
  magnetics integrate** (non il solo IC/breakout SMD). Candidati:
  [Ethernet Controller W5500 Board — Soldered Electronics](https://soldered.com/products/ethernet-controller-w5500-board),
  [Ethernet Module W5500 — ProtoSupplies](https://protosupplies.com/product/ethernet-module-w5500/).
  Header standard 0.1": **GND, 3V3, SS/CS, SCK, MISO, MOSI, RST, INT** —
  servono una SPI CM4 + pin CS + pin RST + opzionale INT. SPI garantita
  fino a 33 MHz (datasheet WIZnet, picco teorico 80 MHz).
- **MAX31865**: una seconda SPI CM4 + pin CS (+ opzionale DRDY).
- **Canale PWM** su un TIM CM4 libero → driver MOSFET/SSR → resistenze.

Verificato (non da riderivare): SPI non abilitata da nessuna parte nel
progetto (libera su CM4); CM7 possiede solo TIM2/TIM6 (configurati via
registro diretto in `antisel_acquisition.c`) — tutti gli altri TIM liberi;
**zero pin sono oggi assegnati al CortexM4** in `AntiSEL.ioc`/
`CM4/Inc/main.h`. Istanze SPI/TIM esatte e pin da assegnare in CubeMX
verificando i connettori NUCLEO-H755ZI-Q raggiungibili dal CM4 (TBD, non
inventare in questa fase).

### 3.2 Firmware — nuovi file (autonomo, nessuna modifica al CM7)

- **`CM4/Src/w5500.c` / `.h`**: configurazione MAC/IP/gateway/subnet
  statica `192.168.1.101`, apertura socket TCP server porta `7756`,
  compreso reset hardware via pin RST all'init. **Non scrivere il driver
  SPI/registro da zero**: usare la libreria ufficiale WIZnet
  [ioLibrary_Driver](https://github.com/Wiznet/ioLibrary_Driver)
  (HAL-agnostica, indirizzi di registro già corretti), integrandola in
  `CM4/Src`/`CM4/Inc` e implementando solo le funzioni di porting (SPI
  read/write, controllo CS, delay) sopra l'HAL SPI dell'STM32H7.
  Riferimenti di porting concreti:
  [stm32-w5500 (afiskon)](https://github.com/afiskon/stm32-w5500),
  [guida ufficiale WIZnet al porting su STM32](https://oldmaker.wiznet.io/2022/02/26/porting-the-latest-iolibrary_driver-to-w5500-based-on-stm32f103rct6/).
  Nessuna guida esiste specifica per la NUCLEO-H755ZI-Q — il procedimento è
  identico su qualunque STM32 con HAL SPI.
- **`CM4/Src/max31865.c` / `.h`**, **`CM4/Src/heater_ctrl.c` / `.h`**,
  **`CM4/Src/rtu_protocol.c` / `.h`**: vedi §4 (comuni a entrambe le
  opzioni).
- **`CM4/Src/main.c`**: il `while(1)` vuoto diventa: init W5500 + TCP
  server, init MAX31865, init PWM, loop di servizio.

## 4. Opzione B — secondo NUCLEO-H753ZI con Ethernet nativa

### 4.1 Hardware

- **[NUCLEO-H753ZI](https://www.digikey.com/en/products/detail/stmicroelectronics/NUCLEO-H753ZI/21348937)**
  — verificato in stock su DigiKey (2.391 unità, ~$40.81). Single-core
  Cortex-M7 (stessa classe di core del CM7 dell'H755 già usato in questo
  progetto), Ethernet MAC nativa via RMII + PHY on-board, RJ45 integrato,
  stesso HAL/famiglia STM32H7. In alternativa
  [NUCLEO-H723ZG](https://www.mouser.com/ProductDetail/STMicroelectronics/NUCLEO-H723ZG)
  (più recente, disponibilità/lead time da verificare al momento
  dell'ordine). La revisione "NUCLEO-H743ZI" (senza "2") risulta obsoleta
  secondo DigiKey — da evitare.
- **MAX31865** su una SPI della nuova scheda.
- **Canale PWM** su un TIM della nuova scheda → driver MOSFET/SSR →
  resistenze.
- Nessun modulo Ethernet aggiuntivo necessario (RJ45 già a bordo scheda).

### 4.2 Firmware — nuovo progetto STM32CubeIDE (separato da CM7/CM4)

- **Dove vive questo progetto** (decisione aperta, §7): una nuova cartella
  nel repo `AntiSEL` (es. `RTU_PID/`, accanto a `CM7/`/`CM4/`) oppure un
  repository a parte — da decidere prima di iniziare.
- **Ethernet/TCP**: riusa direttamente il pattern già scritto e collaudato
  in questo stesso progetto — `MX_LWIP_Init()` (vedi `CM7/Src/lwip.c`) per
  la configurazione IP statica (qui `192.168.1.101`), e lo stesso stile di
  server TCP raw LwIP + dispatch a `strncmp` di
  `CM7/Src/antisel_protocol.c`, adattato alla porta `7756` e al protocollo
  GET/SET di §4.3. Nessun porting di librerie esterne necessario (a
  differenza dell'Opzione A con ioLibrary_Driver).
- **`Src/max31865.c` / `.h`**, **`Src/heater_ctrl.c` / `.h`**,
  **`Src/rtu_protocol.c` / `.h`**: identici concettualmente ai moduli
  dell'Opzione A (§3.2) — vedi §4.3 sotto, sono condivisibili come codice
  quasi verbatim tra le due opzioni, cambia solo il livello di trasporto
  (socket W5500 vs socket LwIP raw).
- **`Src/main.c`**: init Ethernet/LwIP + TCP server, init MAX31865, init
  PWM, loop di servizio — stessa struttura del CM4 dell'Opzione A.

## 5. Parti comuni a entrambe le opzioni

### 5.1 Driver MAX31865 (`max31865.c` / `.h`)

Stessa forma di `CM7/Src/ina301.c` — `MAX31865_Cal_t` (tipo RTD, R_REF,
wiring 2/3/4 fili — TBD), conversioni pure `CodeToOhms`/`OhmsToCelsius`,
interfaccia HW `Init`/`ReadRtd`/`ReadFaultStatus`/`ClearFault`.

### 5.2 Controllo riscaldatore (`heater_ctrl.c` / `.h`)

PID + scrittura duty sul registro CCR del TIM PWM; `Heater_SafetyCheck()`
indipendente dal PID (fault MAX31865, T > cutoff hardware, RTD stantia →
forza PWM a 0% e latcha fault); modi OFF/MANUAL/AUTO; **watchdog locale**:
se non arriva un comando/keepalive dal client TCP entro un timeout mentre
si è in AUTO, forza `mode = OFF` — interamente locale al dispositivo
(CM4 o secondo Nucleo), nessun bisogno di coordinarsi con altro hardware.

### 5.3 Protocollo (`rtu_protocol.c` / `.h`)

Implementa il protocollo già atteso dalla GUI (`GET TEMP`, `GET PID`,
`SET SETPOINT_C <v>` → `TEMP=`, `PWM=`, `STATE=`), stesso pattern di
dispatch a `strncmp` di `CM7/Src/antisel_protocol.c`. Esteso secondo
necessità (mode, fault) restando compatibile col parsing `_parse_kv()` già
presente in `antisel_dashboard_eth.py`.

### 5.4 GUI

**Nessuna modifica prevista in nessuna delle due opzioni** — il placeholder
in `antisel_dashboard_eth.py` parla già esattamente questo protocollo e si
connette già a `192.168.1.101:7756`. Da validare a hardware reale: che il
formato esatto delle righe emesse dal dispositivo (terminatore, spaziatura)
sia compatibile col parsing `_parse_kv()` esistente (righe 862 e dintorni).

### 5.5 Documentazione

- Opzione A: nuova sezione in
  [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md), "Segnali CM4 —
  RTU/PID (W5500 + MAX31865 + PWM)".
- Opzione B: nuova mappatura pin dedicata per la scheda NUCLEO-H753ZI (file
  a parte, non `Mappatura_Pin_AntiSEL.md` che è specifico della
  NUCLEO-H755ZI-Q).
- In entrambi i casi: nuovo `docs/RTU_PID_Protocollo.md`, formalizza il
  protocollo GET/SET oggi implicito solo nei commenti del codice GUI,
  versionato sullo stesso modello di
  [AntiSEL_Protocollo_Comandi.md](AntiSEL_Protocollo_Comandi.md).
- **Nessuna revisione necessaria** di `AntiSEL_System_Description.pdf` in
  nessuna delle due opzioni: entrambe sono coerenti con quanto già
  descritto in RD04 (Heat System come nodo di rete indipendente) — l'Opzione
  B lo è anche fisicamente, l'Opzione A lo è logicamente (la co-locazione
  sullo stesso PCB è invisibile al protocollo/alla GUI).

## 6. Piano di verifica (comune, con un passo di bring-up diverso)

1. **Bring-up rete isolato**: Opzione A — ping a `192.168.1.101`, apertura
   porta `7756`, round-trip GET/SET grezzo (telnet/netcat) prima di
   collegare la GUI. Opzione B — stesso test, ma il bring-up Ethernet/LwIP
   è a rischio più basso perché riusa un pattern già funzionante nel repo.
2. Validazione MAX31865: resistenze note sugli ingressi RTD, confronto
   `CodeToOhms`/`OhmsToCelsius` con tabella di riferimento; forzatura di
   ciascun bit di fault → PWM a 0% entro un ciclo di controllo.
3. Watchdog locale: chiudere la connessione TCP lato GUI mentre il
   riscaldatore è in AUTO, verificare lo spegnimento entro il timeout.
4. Prova end-to-end con la GUI reale: pannello "PID CTRL + RTU", setpoint,
   lettura temperatura, metrica PWM/stato, grafico.
5. Conferma che la build e il comportamento del CM7 restano **invariati**
   (nessuna risorsa condivisa toccata, in nessuna delle due opzioni) —
   nessun impatto sui vincoli hard-real-time della protezione SEL/HCE.

## 7. Decisioni ancora aperte

- **Quale opzione, A o B** — la decisione principale, da prendere prima di
  procedere con l'implementazione.
- Wiring RTD 2/3/4 fili.
- Tuning PID (manuale, Ziegler-Nichols, autotune).
- Soglia esatta del cutoff termico hardware (margine sopra il target
  +85±2°C).
- Timeout esatto del watchdog di disconnessione.
- Solo Opzione A: istanza SPI/TIM esatte e pin su CM4 (CubeMX/connettori
  NUCLEO-H755ZI-Q); modulo/breakout W5500 specifico e velocità di clock SPI.
- Solo Opzione B: dove collocare il nuovo progetto firmware (cartella nel
  repo AntiSEL vs. repository separato); istanza SPI/TIM esatte sulla
  NUCLEO-H753ZI.

## File critici

**Opzione A:**
- `CM4/Src/main.c`, `CM4/Inc/main.h`
- `CM4/Src/w5500.c` / `.h` (nuovi)
- `CM4/Src/max31865.c` / `.h` (nuovi)
- `CM4/Src/heater_ctrl.c` / `.h` (nuovi)
- `CM4/Src/rtu_protocol.c` / `.h` (nuovi)
- [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md)

**Opzione B:**
- Nuovo progetto STM32CubeIDE (percorso da decidere, §7) con
  `Src/main.c`, `Src/lwip.c` (adattato da `CM7/Src/lwip.c`),
  `Src/max31865.c` / `.h`, `Src/heater_ctrl.c` / `.h`,
  `Src/rtu_protocol.c` / `.h`

**Comuni:**
- Nuovo `docs/RTU_PID_Protocollo.md`
- `C:\Users\darthrooster\Documents\STM_Dev\AntiSel_GUI\antisel_dashboard_eth.py` (riferimento, nessuna modifica attesa)
