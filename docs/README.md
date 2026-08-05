# Documentazione AntiSEL

Indice della documentazione di progetto. Per la struttura del firmware e le
istruzioni di build vedi il [README principale](../README.md).

## Guida rapida all'uso

1. **Hardware**: monta la board secondo [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md)
   (assegnazione pin).
2. **Firmware**: genera/compila/flasha il progetto come descritto nel
   [README principale](../README.md#build-del-progetto).
3. **Client PC**: qualsiasi client TCP (porta 7755) che segua il protocollo
   comandi può pilotare il firmware; in precedenza il progetto includeva una
   dashboard Python di riferimento (`antisel_dashboard_eth.py`), non più
   presente nel repository.
4. **Protocollo comandi**: riferimento completo dei comandi TCP e dei
   messaggi asincroni in [AntiSEL_Protocollo_Comandi.md](AntiSEL_Protocollo_Comandi.md).

## Documenti di riferimento

| Documento | Contenuto |
|---|---|
| [AntiSEL_System_Description.pdf](AntiSEL_System_Description.pdf) | Specifica di sistema completa (requisiti, architettura) |
| [AntiSEL_Matrice_Tracciabilita.md](AntiSEL_Matrice_Tracciabilita.md) | Matrice requisito → modulo firmware → funzione → periferica → test |
| [AntiSEL_Protocollo_Comandi.md](AntiSEL_Protocollo_Comandi.md) | Protocollo TCP (comandi, messaggi asincroni, formati file CSV) |
| [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md) | Assegnazione pin STM32 / connettori NUCLEO-H755ZI-Q |
| [ina301.pdf](ina301.pdf) | Datasheet del componente INA301 |
