# Patch firmware — INA301 in modo LATCHED + policy di riarmo

Riferita alla versione **attuale** di `CM7/Src/main.c` (ADC timer-triggered, senza SIM).
Applicare in STM32CubeIDE e ricompilare il core **CM7**. Non compilato in questa sede.

## Progetto

- **INA301 in modo latched**: l'ALERT resta basso finché non si pulsa il pin RESET.
- Nuovo pin **`INA301_RST` = PE14** (CN10 pin 8), output push-pull,
  **a riposo HIGH = latched**; impulso **LOW ~5 µs** per azzerare il latch.
- **Discriminazione HCE/SEL via ADC** (l'ALERT latchato non è più indicativo): a fine
  T_HOLD si confronta la corrente ADC con la soglia (dal DAC).
- **Policy di riarmo**: si contano i **ri-scatti consecutivi** dopo il power-cycle;
  su recupero pulito (finestra `T_CLEAR`) il contatore si azzera. Dopo **N** ri-scatti
  consecutivi → `PERMANENT_OFF`. Questo distingue un SEL recuperabile (contato e
  recuperato, come atteso durante l'irraggiamento) da un **corto persistente**.
- Parametri configurabili da GUI: `N` (`RETRY_SET`), `T_CLEAR` (`TCLEAR_SET`),
  reset manuale del latch (`INA_RST`).

---

## 1) `CM7/Inc/main.h` — aggiungere il pin (dopo `INA301_ALERT_EXTI_IRQn`)

```c
#define INA301_ALERT_EXTI_IRQn EXTI15_10_IRQn
#define INA301_RST_Pin GPIO_PIN_14      /* <-- AGGIUNGERE */
#define INA301_RST_GPIO_Port GPIOE      /* <-- AGGIUNGERE */
```

## 2) `CM7/Src/gpio.c` — configurare PE14

Nella `MX_GPIO_Init`, dopo il write iniziale di LD2 aggiungere il livello di riposo:

```c
  /* INA301_RST a riposo HIGH = comparatore INA301 in modo LATCHED */
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_SET);
```

Dopo il blocco di configurazione di `INA301_ALERT_Pin` aggiungere:

```c
  /*Configure GPIO pin : INA301_RST_Pin (PE14) */
  GPIO_InitStruct.Pin = INA301_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(INA301_RST_GPIO_Port, &GPIO_InitStruct);
```

(Il clock di GPIOE è già abilitato.)

## 3) `AntiSEL.ioc` (opzionale, per coerenza CubeMX)

```
CortexM7.Pins=PE13,PB0,PB14,PG14,PE1,PE14
Mcu.PinsNb=29
Mcu.Pin28=PE14
PE14.ContextOwner=CortexM7
PE14.GPIOParameters=GPIO_Label,PinAttribute
PE14.GPIO_Label=INA301_RST
PE14.Locked=true
PE14.PinAttribute=CortexM7
PE14.Signal=GPIO_Output
```

---

## 4) `CM7/Src/main.c`

### 4a) Variabili globali (in `USER CODE BEGIN PV`, dopo `cooldown_high_t0`)

```c
/* Politica di riarmo (latched): ri-scatti CONSECUTIVI dopo il power-cycle,
 * azzerati su recupero pulito. */
static uint32_t sel_retry_max  = SEL_RETRY_MAX; /* N riarmi consecutivi max */
static uint32_t t_clear_ms     = 30U;           /* finestra "pulito" (ms) */
static uint8_t  recover_is_sel = 0U;            /* 1 = cooldown dopo SEL */
```

### 4b) Helper (in `USER CODE BEGIN 0`, subito dopo `ADC_WritePos()`)

```c
/* Ultimo campione ADC (corrente istantanea, 16 bit) scritto dal DMA. */
static inline uint16_t Adc_Latest(void) {
  if (!adc_running) return 0U;
  uint32_t idx = (ADC_WritePos() + ADC_BUF_SIZE - 1U) % ADC_BUF_SIZE;
  uint32_t addr = ((uint32_t)&adc_buffer[idx]) & ~31U;
  SCB_InvalidateDCache_by_Addr((uint32_t *)addr, 32);
  return adc_buffer[idx];
}

/* Soglia in conteggi ADC (16 bit) equivalente al V_LIMIT del DAC (12 bit):
 * stesso dominio di tensione -> thr = dac * 65535 / 4095. */
static inline uint32_t Threshold_Adc(void) {
  uint32_t dac = HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1);
  return (uint32_t)(((uint64_t)dac * 65535U) / 4095U);
}

/* In modo latched l'ALERT resta basso: la discriminazione HCE/SEL usa la
 * corrente reale letta dall'ADC. */
static inline uint8_t Current_Over_Threshold(void) {
  return (Adc_Latest() > Threshold_Adc()) ? 1U : 0U;
}

/* Impulso di reset del latch INA301: LOW ~5 us poi HIGH (ri-arma latched).
 * Pull basso = comparatore transparent momentaneo (azzera il latch); il
 * ritorno alto ri-arma: se la corrente e' ancora alta l'ALERT si ri-asserisce. */
static void Ina301_ResetLatch(void) {
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_RESET);
  uint32_t t0 = micros();
  while ((uint32_t)(micros() - t0) < 5U) { }
  HAL_GPIO_WritePin(INA301_RST_GPIO_Port, INA301_RST_Pin, GPIO_PIN_SET);
}
```

### 4c) Init (dopo `HAL_DAC_SetValue(..., 2048)`)

```c
  Ina301_ResetLatch();   /* latch pulito all'avvio */
```

### 4d) Macchina a stati — SOSTITUIRE i tre blocchi THOLD / TON / COOLDOWN

```c
    if (antisel_state == STATE_THOLD) {
      if ((uint32_t)(micros() - antisel_t0_us) >=
          (uint32_t)(t_hold_ms * 1000.0f)) {
        /* Fine T_HOLD: in modo LATCHED l'ALERT resta basso -> la
         * discriminazione HCE/SEL si basa sulla corrente ADC reale. */
        if (Current_Over_Threshold()) {
          /* SEL: corrente ancora oltre soglia -> sgancia il DUT.
           * Conta l'evento UNA volta (misura cross-section). */
          sel_count++;
          HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin,
                            GPIO_PIN_RESET);
          antisel_state = STATE_TON;
          antisel_t0_us = micros();
        } else {
          /* HCE: corrente rientrata entro T_HOLD. Nessuno sgancio. */
          hce_count++;
          sel_retry_count = 0;
          Trace_Freeze(2);        /* 2 = HCE */
          Ina301_ResetLatch();    /* sblocca il latch */
          recover_is_sel = 0U;
          antisel_state = STATE_COOLDOWN;
          cooldown_high_t0 = HAL_GetTick();
        }
      }
    } else if (antisel_state == STATE_TON) {
      if ((uint32_t)(micros() - antisel_t0_us) >=
          (uint32_t)(t_on_ms * 1000.0f)) {
        /* Fine T_ON: traccia = pre + T_HOLD + T_ON. Riaccendi il DUT e
         * sblocca il latch, poi verifica il recupero (finestra T_CLEAR). */
        Trace_Freeze(1);          /* 1 = SEL */
        HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_SET);
        Ina301_ResetLatch();
        recover_is_sel = 1U;
        antisel_state = STATE_COOLDOWN;
        cooldown_high_t0 = HAL_GetTick();
      }
    } else if (antisel_state == STATE_COOLDOWN) {
      /* Verifica recupero. Dopo un SEL: se la corrente torna sopra soglia
       * entro T_CLEAR -> ri-latch (recupero fallito) -> nuovo power-cycle;
       * dopo N ri-scatti consecutivi -> PERMANENT_OFF. Se resta pulito per
       * T_CLEAR -> recupero riuscito, azzera i tentativi e riarma. */
      if (recover_is_sel && Current_Over_Threshold()) {
        sel_retry_count++;
        HAL_GPIO_WritePin(DUT_SWITCH_GPIO_Port, DUT_SWITCH_Pin, GPIO_PIN_RESET);
        if (sel_retry_count >= sel_retry_max) {
          antisel_state = STATE_PERMANENT_OFF;   /* corto persistente */
        } else {
          antisel_state = STATE_TON;             /* nuovo power-cycle */
          antisel_t0_us = micros();
        }
      } else if (HAL_GetTick() - cooldown_high_t0 >= t_clear_ms) {
        sel_retry_count = 0;                      /* recupero riuscito */
        Ina301_ResetLatch();
        antisel_state = STATE_IDLE;
        __HAL_GPIO_EXTI_CLEAR_IT(INA301_ALERT_Pin);
        __DSB();
        (void)EXTI->PR1;
        HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
      }
    }
```

Note:
- `sel_count` (contatore SEL totali per la cross-section) si incrementa **una sola volta**
  all'ingresso in TON; i ri-tentativi sono power-cycle dello stesso evento.
- `sel_retry_count` è ora il **contatore dei ri-scatti consecutivi** (0..N). PERMANENT_OFF
  esattamente all'N-esimo (niente off-by-one).

### 4e) Comandi — inserire prima del ramo finale `else { ACK }`

```c
  } else if (strncmp(line, "INA_RST", 7) == 0) {
    Ina301_ResetLatch();
    tcp_write(tpcb, "INA_RST OK\r\n", 12, TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "RETRY_SET", 9) == 0) {
    int n = atoi(line + 10);
    if (n < 1) n = 1; if (n > 100) n = 100;
    sel_retry_max = (uint32_t)n;
    snprintf(buf, sizeof(buf), "RETRY_SET=%lu\r\n", (unsigned long)sel_retry_max);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
  } else if (strncmp(line, "TCLEAR_SET", 10) == 0) {
    int n = atoi(line + 11);
    if (n < 1) n = 1; if (n > 10000) n = 10000;
    t_clear_ms = (uint32_t)n;
    snprintf(buf, sizeof(buf), "TCLEAR_SET=%lu\r\n", (unsigned long)t_clear_ms);
    tcp_write(tpcb, buf, strlen(buf), TCP_WRITE_FLAG_COPY);
```

### 4f) `RESET` e `DUT_ON` — pulire il latch al riarmo manuale

In entrambi i gestori, dopo aver riacceso lo switch (`GPIO_PIN_SET`), aggiungere:

```c
    Ina301_ResetLatch();
```

### 4g) (Opzionale) `STATUS` — esporre N

Nella risposta `OK STATUS=...` si può aggiungere ` RMAX=%lu` con `sel_retry_max`,
così la GUI mostra `x/N` corretto. La GUI qui allegata lo interpreta se presente.

---

## 5) Impatto sul collaudo (IMPORTANTE)

Con la discriminazione **via ADC**, in prova reale non basta più pilotare solo
l'ALARM (PE13): serve anche imporre la **corrente** sull'ingresso ADC **PA3**
(INA301_OUT), perché è quella a decidere HCE vs SEL:

- **PE13** (fronte di discesa) fa **partire** il T_HOLD (EXTI).
- **PA3** (livello analogico 0–3,3 V) decide la classificazione a fine T_HOLD:
  sopra soglia = SEL, sotto = HCE; e in COOLDOWN se resta alta = corto persistente.

Quindi l'emulatore INA301 deve pilotare **entrambi**: impulso su PE13 + livello su PA3
(via DAC o PWM+RC+partitore ≤3,3 V). Un SEL "recuperabile" = PA3 alta durante T_HOLD/T_ON
poi bassa; un **corto persistente** = PA3 alta anche dopo il power-cycle (→ dopo N → PERMANENT_OFF).

## 6) Parametri

| Parametro | Comando | Default | Note |
|---|---|---|---|
| N (ri-scatti consecutivi) | `RETRY_SET n` | 3 | oltre → PERMANENT_OFF |
| T_CLEAR (ms) | `TCLEAR_SET n` | 30 | finestra "pulito" post-riaccensione |
| T_ON | `TON_SET x.x` | — | finestra di de-latch (tarare sul DUT) |
| Reset manuale latch | `INA_RST` | — | override da GUI |
