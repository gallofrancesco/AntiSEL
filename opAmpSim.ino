/* ===========================================================================
 *  AntiSEL - "INA301 virtuale" su Arduino Uno (5 V)
 *  Emula l'INA301 + la corrente del DUT per collaudare il firmware LATCHED
 *  con discriminazione HCE/SEL via ADC (PA3).
 *
 *  Con il firmware attuale la classificazione dipende dal LIVELLO DI CORRENTE
 *  su PA3, non dall'impulso su PE13. Quindi l'Arduino:
 *   - genera il fronte di trigger su PE13 (avvia il T_HOLD);
 *   - pilota il livello di corrente su PA3 (alto = sopra soglia);
 *   - legge lo switch PG14 e "toglie corrente" quando il DUT viene sganciato,
 *     così un SEL recuperabile si recupera e un corto persistente no.
 *
 *  >>> LIVELLI (Uno 5 V, STM32 3,3 V) — NON superare 3,3 V sui pin STM32 <<<
 *
 *  Cablaggio:
 *     D8  --[1 kΩ]--> PE13  (INA301_ALERT / trigger, open-drain: solo LOW/Hi-Z)
 *     D9  --[220 Ω]--+--> PA3 (INA301_OUT / corrente emulata)
 *                    +--[100 Ω]--GND   (+ C=1 µF tra PA3 e GND)
 *     D2  <-------- PG14 (DUT_SWITCH)
 *     A0  <-------- PA4  (V_LIMIT, monitor)
 *     GND <-------> GND
 *  PA3 alto ~1,56 V ; basso 0 V.  Alimenta l'Arduino dalla SUA USB.
 *
 *  In GUI imposta T_HOLD = 5 ms, T_ON = 2 ms (coerente con HCE_MS qui sotto).
 *
 *  USO (Serial Monitor a 9600):
 *     h -> HCE (impulso di corrente < T_HOLD)
 *     s -> SEL recuperabile (si recupera dopo il power-cycle)
 *     p -> corto persistente (resta alto ad ogni riaccensione -> PERMANENT_OFF)
 *     x -> annulla il guasto (corrente a zero)
 *     m -> misura stato switch e V_LIMIT
 * =========================================================================== */

const uint8_t PIN_ALERT  = 8;    // --[1k]--> PE13 (trigger, open-drain)
const uint8_t PIN_IEMU   = 9;    // --[220/100]--> PA3 (livello corrente)
const uint8_t PIN_SWITCH = 2;    // <-- PG14 (HIGH = switch chiuso / DUT ON)
const uint8_t PIN_VLIMIT = A0;   // <-- PA4  (V_LIMIT)

const unsigned long HCE_MS = 2;  // durata impulso HCE (< T_HOLD della GUI)

// Modello di guasto
enum { FAULT_NONE = 0, FAULT_ONESHOT, FAULT_PERSIST };
uint8_t  fault = FAULT_NONE;
bool     recovered = false;
bool     prevClosed = true;

// ---------------------------------------------------------------------------
bool switchClosed() { return digitalRead(PIN_SWITCH) == HIGH; }
void currentHigh()  { digitalWrite(PIN_IEMU, HIGH); }   // corrente sopra soglia
void currentLow()   { digitalWrite(PIN_IEMU, LOW); }    // corrente ~0

float vlimit() { return analogRead(PIN_VLIMIT) * 5.0f / 1023.0f; }

// Fronte di discesa su PE13 (open-drain): avvia il T_HOLD nel firmware
void pulseTrigger() {
  pinMode(PIN_ALERT, OUTPUT);
  digitalWrite(PIN_ALERT, LOW);
  delayMicroseconds(150);
  pinMode(PIN_ALERT, INPUT);      // release -> il pull-up riporta PE13 alto
}

void armFault(uint8_t mode) {
  fault = mode;
  recovered = false;
  prevClosed = switchClosed();
  currentHigh();                  // corrente alta
  delayMicroseconds(400);         // lascia caricare PA3 (RC ~ qualche centinaio us)
  pulseTrigger();                 // fronte di trigger
  Serial.println(mode == FAULT_PERSIST ? F(">> Corto PERSISTENTE armato")
                                        : F(">> SEL recuperabile armato"));
}

void clearFault() {
  fault = FAULT_NONE; recovered = false;
  currentLow();
  Serial.println(F(">> Guasto annullato (corrente 0)"));
}

void doHCE() {
  Serial.println(F(">> HCE: impulso corto (< T_HOLD)"));
  currentHigh();
  delayMicroseconds(400);
  pulseTrigger();
  delay(HCE_MS);                  // rientra prima del T_HOLD -> HCE
  currentLow();
}

void report() {
  Serial.print(F("switch PG14 = "));
  Serial.print(switchClosed() ? F("CHIUSO(ON)") : F("APERTO(OFF)"));
  Serial.print(F("   V_LIMIT(PA4) = "));
  Serial.print(vlimit(), 3);
  Serial.print(F(" V   corrente = "));
  Serial.println(digitalRead(PIN_IEMU) ? F("ALTA") : F("bassa"));
}

// Aggiorna il livello di corrente su PA3 in base al modello di guasto e allo
// stato dello switch (emula il DUT: se lo switch e' aperto, corrente = 0).
void updateCurrent() {
  bool closed = switchClosed();

  // rileva il completamento di un power-cycle (aperto -> chiuso)
  if (fault == FAULT_ONESHOT && !recovered && closed && !prevClosed) {
    recovered = true;             // dopo un power-cycle il SEL recuperabile e' passato
    fault = FAULT_NONE;
    Serial.println(F("   (SEL recuperato dopo il power-cycle)"));
  }
  prevClosed = closed;

  bool high = false;
  if (fault == FAULT_PERSIST)               high = closed;   // sempre alto quando alimentato
  else if (fault == FAULT_ONESHOT && !recovered) high = closed;
  digitalWrite(PIN_IEMU, high ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  pinMode(PIN_IEMU, OUTPUT);
  currentLow();
  pinMode(PIN_ALERT, INPUT);      // trigger a riposo (Hi-Z)
  pinMode(PIN_SWITCH, INPUT);
  Serial.println(F("INA301 virtuale (latched/ADC) pronto."));
  Serial.println(F("Comandi:  h=HCE  s=SEL  p=corto persistente  x=annulla  m=misura"));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'h': doHCE();              break;
      case 's': armFault(FAULT_ONESHOT); break;
      case 'p': armFault(FAULT_PERSIST); break;
      case 'x': clearFault();         break;
      case 'm': report();             break;
      default: break;
    }
  }
  updateCurrent();   // pilotaggio continuo di PA3
}