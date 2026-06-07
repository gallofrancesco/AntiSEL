# AntiSEL

Progetto STM32 dual-core basato su STM32H755ZITx con due core:
- `CM4/` per il processore Cortex-M4
- `CM7/` per il processore Cortex-M7

## Struttura del progetto

- `AntiSEL.ioc` - file di configurazione STM32CubeMX
- `CM4/` - sorgenti e configurazione per il core CM4
- `CM7/` - sorgenti e configurazione per il core CM7
- `Common/` - codice comune per il boot dual-core
- `Drivers/` - driver HAL e BSP
- `Middlewares/` - middleware LwIP e altro

## File principali

- `CM4/Src/main.c`
- `CM7/Src/main.c`
- `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c`
- `CM4/Startup/startup_stm32h755zitx.s`
- `CM7/Startup/startup_stm32h755zitx.s`

## Build

Il progetto usa makefile già presenti in `CM7/`.

Esempio:

```sh
cd CM7
make all
```

## Contenuto del repository

- Firmware e sorgenti STM32
- File di linker `STM32H755ZITX_FLASH.ld` e `STM32H755ZITX_RAM.ld`
- Driver HAL e middleware LwIP

## Note

- `requirements.txt` è incluso per convenzione; al momento non ci sono dipendenze Python specifiche.
- Aggiungi le dipendenze Python se il flusso di sviluppo ne richiederà in futuro strumenti di supporto.
