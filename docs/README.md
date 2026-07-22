# Documentazione AntiSEL

Indice della documentazione di progetto. Per la struttura del firmware e le
istruzioni di build vedi il [README principale](../README.md).

## Guida rapida all'uso

1. **Hardware**: monta la board secondo [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md)
   (assegnazione pin) e, per il banco di collaudo con Arduino, gli schemi
   [Cablaggio_Arduino_R4Minima_NUCLEO.svg](Cablaggio_Arduino_R4Minima_NUCLEO.svg) (elettrico)
   e [Cablaggio_Arduino_R4Minima_NUCLEO_breadboard.svg](Cablaggio_Arduino_R4Minima_NUCLEO_breadboard.svg)
   (posizionamento su breadboard).
2. **Firmware**: genera/compila/flasha il progetto come descritto nel
   [README principale](../README.md#build-del-progetto).
3. **Emulatore INA301 (banco prova)**: per il collaudo senza hardware DUT
   reale, la mappatura pin verso un Arduino emulatore è descritta nella
   sezione Arduino di
   [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md#arduino-emulatore-ina301-solo-per-il-collaudo)
   (lo sketch di riferimento non è più presente nel repository).
4. **Client PC**: qualsiasi client TCP (porta 7755) che segua il protocollo
   comandi può pilotare il firmware; in precedenza il progetto includeva una
   dashboard Python di riferimento (`antisel_dashboard_eth.py`), non più
   presente nel repository.
5. **Protocollo comandi**: riferimento completo dei comandi TCP e dei
   messaggi asincroni in [AntiSEL_Protocollo_Comandi.md](AntiSEL_Protocollo_Comandi.md).
6. **Campagna di test**: procedura e parametri di collaudo in
   [Campagna_Test_AntiSEL.md](Campagna_Test_AntiSEL.md) / [Campagna_Test_AntiSEL.pdf](Campagna_Test_AntiSEL.pdf).

## Documenti di riferimento

| Documento | Contenuto |
|---|---|
| [AntiSEL_System_Description.pdf](AntiSEL_System_Description.pdf) | Specifica di sistema completa (requisiti, architettura) |
| [AntiSEL_Matrice_Tracciabilita.md](AntiSEL_Matrice_Tracciabilita.md) | Matrice requisito → modulo firmware → funzione → periferica → test |
| [AntiSEL_Protocollo_Comandi.md](AntiSEL_Protocollo_Comandi.md) | Protocollo TCP (comandi, messaggi asincroni, formati file CSV) |
| [Mappatura_Pin_AntiSEL.md](Mappatura_Pin_AntiSEL.md) | Assegnazione pin STM32 / connettori NUCLEO-H755ZI-Q |
| [Patch_INA301_Latched.md](Patch_INA301_Latched.md) | Note di progetto sul modo latched dell'INA301 e sulla policy di riarmo |
| [Campagna_Test_AntiSEL.md](Campagna_Test_AntiSEL.md) / [.pdf](Campagna_Test_AntiSEL.pdf) | Procedura e report della campagna di collaudo |
| [ina301.pdf](ina301.pdf) | Datasheet del componente INA301 |
| Schemi `Cablaggio_Arduino_R4Minima_NUCLEO*.svg` | Cablaggio del banco di collaudo con Arduino (emulatore INA301) |
