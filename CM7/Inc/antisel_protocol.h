/**
 ******************************************************************************
 * @file    antisel_protocol.h
 * @brief   Protocollo Ethernet AntiSEL v5 (server TCP + telemetria).
 ******************************************************************************
 */
#ifndef ANTISEL_PROTOCOL_H
#define ANTISEL_PROTOCOL_H

void Proto_Init(void);    /* avvia il server TCP sulla porta 7755 */
void Proto_Service(void); /* da chiamare nel loop: log 10 Hz + invio traccia */

#endif /* ANTISEL_PROTOCOL_H */
