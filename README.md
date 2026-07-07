# AntiSEL

Firmware dual-core per STM32H755ZITx.

La soluzione usa un core Cortex-M7 come principale per l’applicazione e un core Cortex-M4 secondario per funzionalità dedicate.

## Struttura del progetto

- `AntiSEL.ioc` - file di configurazione STM32CubeMX/STM32CubeIDE
- `CM7/` - progetto, sorgenti e makefile per il core Cortex-M7
- `CM4/` - progetto, sorgenti e makefile per il core Cortex-M4
- `Common/` - codice comune per il boot dual-core e la sincronizzazione tra core
- `Drivers/` - librerie HAL e BSP fornite da ST
- `Middlewares/` - componente LwIP e altri middleware

### Cartelle chiave

- `CM7/Src/` - codice applicativo CM7, inclusi `main.c`, il supporto Ethernet/LwIP e il DAC
- `CM4/Src/` - codice applicativo CM4
- `CM7/Startup/` e `CM4/Startup/` - startup file assembly per ogni core
- `CM7/Inc/` e `CM4/Inc/` - header locali del progetto
- `Common/Src/` - boot a doppio core e inizializzazione condivisa
- `Drivers/STM32H7xx_HAL_Driver/` - sorgenti HAL per STM32H7
- `Middlewares/Third_Party/LwIP/` - stack di rete LwIP

## Quali file sono tracciati

Tracciamo i file di configurazione e sorgente necessari al progetto.
Non vanno committati gli artefatti di compilazione, come i contenuti di:
- `CM7/Debug/`
- `CM7/Release/`
- `CM4/Debug/`
- `CM4/Release/`

Questi percorsi sono già esclusi da `.gitignore`.

## Build del progetto

### Flusso consigliato con STM32CubeMX e STM32CubeIDE

1. Apri `AntiSEL.ioc` con STM32CubeMX o con STM32CubeIDE se vuoi modificare la configurazione hardware.
2. Esegui le modifiche necessarie a pin, periferiche, clock o middleware.
3. Salva e rigenera il codice.
4. Apri STM32CubeIDE.
5. Importa il progetto `AntiSEL/CM7` e, se serve, `AntiSEL/CM4` tramite `File > Import > General > Existing Projects into Workspace`.
6. Seleziona il progetto nella vista Project Explorer.
7. Usa `Project > Clean...` per pulire eventuali build precedenti.
8. Costruisci con `Project > Build Project` o `Project > Build All`.

STM32CubeIDE userà il codice rigenerato da `AntiSEL.ioc` e gestirà il toolchain automaticamente.

## Aprire e modificare il progetto con STM32CubeIDE

1. Apri STM32CubeIDE.
2. Seleziona `File > Open Projects from File System...` oppure `File > Import > General > Existing Projects into Workspace`.
3. Scegli la cartella `AntiSEL/CM7` per il progetto CM7 e `AntiSEL/CM4` per il progetto CM4.
4. Importa entrambi i progetti nel workspace.

### Usare STM32CubeMX

- `AntiSEL.ioc` è il file di progetto CubeMX. Usalo per rigenerare la configurazione hardware se devi cambiare pin, periferiche o clock.
- Apri `AntiSEL.ioc` con STM32CubeIDE o con STM32CubeMX.
- Dopo aver modificato la configurazione, salva e rigenera il codice.

## Configurazione di Rete (LwIP) e Memoria su STM32H7

Su questo microcontrollore, l'Ethernet MAC DMA richiede che i buffer (RX/TX) e l'heap di LwIP si trovino in RAM_D2, un'area di memoria accessibile dal DMA.
Nel progetto, per evitare sovrapposizioni critiche tra `RX_POOL` (che occupa memoria fino a `0x30004980`) e l'heap LwIP, il parametro `LWIP_RAM_HEAP_POINTER` è stato impostato all'indirizzo `0x30005000` (vedi `CM7/Inc/lwipopts.h`).
È fondamentale non rimuovere questo puntatore o spostarlo in RAM_D1, altrimenti il modulo Ethernet MAC DMA smetterà di funzionare.

## Nota sui file di progetto

- `AntiSEL.ioc` contiene la configurazione hardware e le opzioni del progetto.
- `CM7/makefile.defs`, `CM7/makefile.init`, `CM7/makefile.targets` sono makefile generati da CubeIDE/CubeMX e servono a gestire il build del progetto.
- I file `.cproject` e `.settings/*` sono metadati dell’IDE e dovrebbero essere mantenuti solo se si desidera sincronizzare lo stesso ambiente di sviluppo tra macchine.

## Protocollo TCP AntiSEL (porta 7755)

Comandi (una riga per comando, terminata da CR/LF):

- `PING` → `PONG`
- `STATUS` → `OK STATUS=<IDLE|THOLD|TON|PERMANENT_OFF> RETRY=<n> SEL=<n> HCE=<n>`
- `DAC_GET` / `DAC_SET <0-4095>` — soglia I_TH (DAC 12 bit sul pin LIMIT dell'INA301)
- `THOLD_SET <ms>` / `TON_SET <ms>` — range 1.0–10.0 ms, risoluzione 0.1 ms (timing interno a 1 µs via TIM2)
- `DUT_ON` / `DUT_OFF` — override manuale (R-07). `DUT_ON` riarma anche la protezione
- `RESET` — riarma protezione e azzera contatori SEL/HCE
- `TH_LOAD <1|2|3> <counts>` / `TH_SELECT <1|2|3>` / `TH_GET` — soglie precaricate (§8.2): 3 valori DAC caricati prima del run e selezionabili da PC; `DAC_SET` manuale disattiva la preset (STATUS riporta `TH=<n>`)

Messaggi asincroni dal firmware:

- `LOG_10HZ TICK=<ms> I=<raw16bit> FRESH=<0|1> STATE=<n> RETRY=<n> SEL=<n> HCE=<n>` ogni 100 ms (R-08)
- Traccia evento (R-06): `TRACE_START <SEL|HCE> FS=<Sa/s> N=<campioni> THOLD_MS=<v> TON_MS=<v> DAC=<v> TICK=<ms>`, seguito da righe `indice,valore_raw` e da `TRACE_END`. La finestra copre pre-trigger (1 ms) + T_HOLD (+ T_ON per i SEL); ADC a 16 bit, 100 kSa/s (TIM6 → ADC1, buffer circolare DMA da 40 ms)

File prodotti dalla GUI (nomenclatura §6.3, `<DUT_id>_<LET>_<run_id>_<timestamp>`):

- `..._log10hz.csv` — log lento 10 Hz (R-08), un file per connessione
- `..._events.csv` — override manuali e cambi parametri con timestamp (§5.3)
- `..._trace_<SEL|HCE>.csv` — una traccia per evento, con metadati in testa

## Supporto e debugging

- Se la build fallisce su un’altra macchina, il primo controllo è il toolchain: assicurati di usare lo stesso GCC di STM32CubeIDE.
- Controlla che le cartelle `Debug/` e `Release/` non siano tracciate nel repository.
- Se apri il progetto in CubeIDE, usa la funzione `Project > Clean...` e poi `Build` per ricostruire daccapo.
