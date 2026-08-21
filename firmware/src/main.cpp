#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#if defined(ROLE_TX) || defined(ROLE_RX)
#include <FastLED.h>
// Las DevKitC-1 v1.0 llevan el LED RGB en el 48 y las v1.1 en el 38.
// Atacamos los dos con el mismo buffer: la placa que no lo tenga se queda
// con una salida al aire, y asi el mismo binario vale para las dos.
#define LED_PIN_A 48
#define LED_PIN_B 38
#define NUM_LEDS  1
CRGB leds[NUM_LEDS];
uint32_t ledOffMs = 0;
bool ledOn = false;

void ledFlash(CRGB color = CRGB::Blue) {
  leds[0] = color;
  FastLED.show();
  ledOn = true;
  ledOffMs = millis() + 100;
}

void ledUpdate() {
  if (ledOn && millis() >= ledOffMs) {
    leds[0] = CRGB::Black;
    FastLED.show();
    ledOn = false;
  }
}

// Mismo codigo de color en las dos placas: de un vistazo sabes que pulsaste
CRGB groupColor(uint8_t group) {
  switch (group) {
    case 1:  return CRGB::Blue;    // kits 1-6
    case 4:  return CRGB::Cyan;    // grupo de 5
    default: return CRGB::Purple;  // on/off
  }
}
#endif

#if defined(ROLE_RX)
#include "USB.h"
#include "esp32-hal-tinyusb.h"
#endif

#if !defined(ROLE_TX) && !defined(ROLE_RX)
#define ROLE_RX 1
#endif

// ---------------------------------------------------------------------------
// MAPA DE BOTONES  (compartido por TX y RX: si tocas esto, reflashea las dos)
//
// group 0 = on/off libre. group > 0 = excluyente dentro de ese grupo.
// pin solo lo usa el TX; el RX solo necesita midi/kind y el orden de la tabla.
// kind: KIND_CC = Control Change, KIND_NOTE = Note On/Off (numero MIDI).
//
// Pines descartados en el ESP32-S3 y por que:
//   0, 45, 46         strapping (condicionan el arranque)
//   19, 20            USB nativo D-/D+
//   26..32            flash SPI interna
//   33..37            PSRAM octal (placa H16R8)
//   38, 48            LED RGB de la placa
//   43, 44            UART0 (consola y flasheo por serie)
// GPIO 3 = ADC bateria (divisor 100k/100k desde B+). Es strapping suave;
// el divisor no lo fuerza a 0/3V3 al arrancar.
// ---------------------------------------------------------------------------
static constexpr uint8_t KIND_CC = 0;
static constexpr uint8_t KIND_NOTE = 1;

struct ButtonDef {
  uint8_t pin;
  uint8_t midi;   // CC number o note number
  uint8_t group;
  uint8_t kind;
};

// Telemetria de bateria (solo TX mide; RX no la manda por MIDI)
static constexpr uint8_t BAT_ADC_PIN = 3;
// -1 = no cableado (usuario omitio CHRG). Si luego sueldas CHRG, pon 46.
static constexpr int8_t BAT_CHRG_PIN = -1;
// Bateria YA NO se manda por MIDI CC: ensuciaba MIDI Learn / Link to controller
// (FL capturaba CC 110 en vez del boton). % se ve en Serial TX y LED.
static constexpr uint8_t BAT_LOW_PERCENT = 20;
static constexpr float BAT_DIVIDER_RATIO = 2.0f;  // 100k + 100k
static constexpr uint32_t BAT_SAMPLE_MS = 500;

// Notas MIDI TX (botones 7-11): numeracion FL (MIDI 60 = C5).
// Todas las notas de tono: On=127 al pulsar, Off=0 al soltar (mientras se mantiene).
// FL: C5=60 B4=59 C6=72 G6=79 A4=57
static constexpr ButtonDef BUTTONS[] = {
    // Botones 1-6: kits, excluyentes entre si (CC)
    {4, 30, 1, KIND_CC},  {5, 31, 1, KIND_CC},  {6, 32, 1, KIND_CC},
    {7, 33, 1, KIND_CC},  {15, 34, 1, KIND_CC}, {16, 35, 1, KIND_CC},
    // Botones 7-11: 5 notas momentaneas (sin grupo)
    {8, 60, 0, KIND_NOTE},   // FL C5
    {9, 59, 0, KIND_NOTE},   // FL B4
    {10, 72, 0, KIND_NOTE},  // FL C6
    {11, 79, 0, KIND_NOTE},  // FL G6
    {12, 57, 0, KIND_NOTE},  // FL A4
    // Botones 12-16: grupo de 5, excluyentes entre si (CC)
    {13, 25, 4, KIND_CC}, {1, 70, 4, KIND_CC},  {2, 71, 4, KIND_CC},
    {14, 72, 4, KIND_CC}, {17, 73, 4, KIND_CC},
    // Botones 17-23: on/off independientes (CC)
    {18, 74, 0, KIND_CC}, {21, 75, 0, KIND_CC}, {39, 76, 0, KIND_CC},
    {40, 77, 0, KIND_CC}, {41, 78, 0, KIND_CC}, {42, 79, 0, KIND_CC},
    {47, 80, 0, KIND_CC},
};

static constexpr uint8_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
static_assert(BUTTON_COUNT <= 32, "La mascara del paquete es de 32 bits");

static constexpr uint8_t MIDI_CHANNEL = 1;
static constexpr uint32_t DEBOUNCE_MS = 20;

// ---------------------------------------------------------------------------
// RX local (H16R8): 12 notas (5a/6a octava FL, sin las 5 del TX) + 12 CC
// Notas: momentaneas (pulsar=On / soltar=Off).
// CC: latch on/off, EXCEPTO CC 88 (momentaneo: pulsar On / soltar Off).
// Cable: GPIO -- boton -- GND.
// GPIO3: strapping suave; no lo mantengas pulsado al resetear.
// ---------------------------------------------------------------------------
#if defined(ROLE_RX)
struct RxPadDef {
  uint8_t pin;
  uint8_t midi;
  uint8_t kind;
};

// CC 88 = hold Play (Space): debe ser momentaneo aunque el resto de CC sea latch.
static constexpr uint8_t RX_CC_MOMENTARY = 88;

static inline bool rxCcIsMomentary(uint8_t midi) {
  return midi == RX_CC_MOMENTARY;
}

// Notas excluidas (ya en TX): C5=60 B4=59 C6=72 G6=79 A4=57
static constexpr RxPadDef RX_PADS[] = {
    // 12 notas FL: C#5..B5 + D6 (momentaneas)
    {1,  61, KIND_NOTE},  // C#5
    {2,  62, KIND_NOTE},  // D5
    {4,  63, KIND_NOTE},  // D#5
    {5,  64, KIND_NOTE},  // E5
    {6,  65, KIND_NOTE},  // F5
    {7,  66, KIND_NOTE},  // F#5
    {8,  67, KIND_NOTE},  // G5
    {9,  68, KIND_NOTE},  // G#5
    {10, 69, KIND_NOTE},  // A5
    {11, 70, KIND_NOTE},  // A#5
    {12, 71, KIND_NOTE},  // B5
    {13, 74, KIND_NOTE},  // D6
    // CC 81-87, 89-92 latch; CC 88 momentaneo
    {14, 81, KIND_CC},
    {15, 82, KIND_CC},
    {16, 83, KIND_CC},
    {17, 84, KIND_CC},
    {18, 85, KIND_CC},
    {21, 86, KIND_CC},
    {39, 87, KIND_CC},
    {40, 88, KIND_CC},  // momentaneo (Play hold)
    {41, 89, KIND_CC},
    {42, 90, KIND_CC},
    {47, 91, KIND_CC},
    {3,  92, KIND_CC},  // GPIO3: no pulsar al boot
};

static constexpr uint8_t RX_PAD_COUNT = sizeof(RX_PADS) / sizeof(RX_PADS[0]);
#endif

struct __attribute__((packed)) EspNowStatePacket {
  uint8_t version;
  uint8_t seq;
  uint8_t lastPressed;  // indice del ultimo boton pulsado, 0xFF si ninguno
  uint8_t pressCount;   // sube en CADA pulsacion, cambie o no el estado
  uint32_t mask;        // bit i = boton i de BUTTONS activo
  uint8_t batPercent;   // 0-100
  uint8_t batFlags;     // bit0=cargando, bit1=baja
  uint16_t batMv;       // milivoltios del pack (0 si no hay lectura)
};

static constexpr uint8_t BAT_FLAG_CHARGING = 0x01;
static constexpr uint8_t BAT_FLAG_LOW = 0x02;
static constexpr uint8_t PACKET_VERSION = 4;
static constexpr uint8_t ESP_NOW_CHANNEL = 1;
static constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if defined(ROLE_TX)
bool physicalPressed[BUTTON_COUNT] = {};
bool buttonState[BUTTON_COUNT] = {};
uint32_t lastChangeMs[BUTTON_COUNT] = {};
EspNowStatePacket txPacket = {PACKET_VERSION, 0, 0xFF, 0, 0, 0, 0, 0};
uint32_t lastTxMs = 0;
uint32_t lastUserActivityMs = 0;
uint32_t lastScanMs = 0;
uint32_t lastBatSampleMs = 0;
uint8_t lastPressedGroup = 0;
uint8_t lastPressedIndex = 0xFF;
uint8_t pressCount = 0;
uint8_t batPercent = 0;
uint8_t batFlags = 0;
uint16_t batMv = 0;
int8_t serialNoteHold = -1;  // simula toque corto por serie
static constexpr uint32_t TX_HEARTBEAT_MS = 1000;
// 0 = sin deep sleep. En vivo no interesa que se duerma a mitad de tema.
static constexpr uint32_t TX_IDLE_DEEP_SLEEP_MS = 0;
static constexpr uint32_t TX_SCAN_INTERVAL_MS = 5;

void initButtons() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTONS[i].pin, INPUT_PULLUP);
    physicalPressed[i] = (digitalRead(BUTTONS[i].pin) == LOW);
    buttonState[i] = false;
  }
}

// Li-ion 1S aproximado: 3.30 V = 0%, 4.20 V = 100%
uint8_t voltageToPercent(uint16_t mv) {
  if (mv <= 3300) return 0;
  if (mv >= 4200) return 100;
  static const uint16_t tableMv[] = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const uint8_t tablePct[] = {0, 5, 10, 20, 30, 50, 65, 80, 90, 100};
  for (uint8_t i = 1; i < 10; i++) {
    if (mv <= tableMv[i]) {
      const uint16_t span = tableMv[i] - tableMv[i - 1];
      const uint16_t pos = mv - tableMv[i - 1];
      const uint8_t dp = tablePct[i] - tablePct[i - 1];
      return tablePct[i - 1] + (uint8_t)((pos * dp) / span);
    }
  }
  return 100;
}

void initBattery() {
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  pinMode(BAT_ADC_PIN, INPUT);
  if (BAT_CHRG_PIN >= 0) {
    pinMode((uint8_t)BAT_CHRG_PIN, INPUT_PULLUP);
  }
}

void sampleBattery(uint32_t nowMs) {
  if (nowMs - lastBatSampleMs < BAT_SAMPLE_MS && lastBatSampleMs != 0) return;
  lastBatSampleMs = nowMs;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(BAT_ADC_PIN);
  }
  const uint16_t adcMv = (uint16_t)(sum / 8);
  batMv = (uint16_t)((float)adcMv * BAT_DIVIDER_RATIO + 0.5f);
  batPercent = voltageToPercent(batMv);
  batFlags = 0;
  if (BAT_CHRG_PIN >= 0 && digitalRead((uint8_t)BAT_CHRG_PIN) == LOW) {
    batFlags |= BAT_FLAG_CHARGING;
  }
  if (batPercent < BAT_LOW_PERCENT) {
    batFlags |= BAT_FLAG_LOW;
  }
}

// El TX no usa el USB, asi que la consola sale por USB-Serial-JTAG.
// Con timeout 0 no se bloquea si no hay nadie leyendo el puerto.
void initLog() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(400);
  Serial.println();
  Serial.println("=========================================");
  Serial.println(" TX MIDI ESP-NOW");
  Serial.printf(" MAC   : %s\n", WiFi.macAddress().c_str());
  Serial.printf(" Canal : %u\n", ESP_NOW_CHANNEL);
  Serial.printf(" Botones: %u\n", BUTTON_COUNT);
  Serial.printf(" Bateria ADC GPIO%u  CHRG=%d\n", BAT_ADC_PIN, (int)BAT_CHRG_PIN);
  Serial.println("=========================================");
  Serial.println(" n   GPIO  MIDI kind grupo  nivel");
  uint8_t wired = 0;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    const bool low = (digitalRead(BUTTONS[i].pin) == LOW);
    if (low) wired++;
    Serial.printf("%2u   %-4u  %-4u %-4s %-5u  %s\n", i + 1, BUTTONS[i].pin, BUTTONS[i].midi,
                  BUTTONS[i].kind == KIND_NOTE ? "NOTE" : "CC",
                  BUTTONS[i].group, low ? "LOW  <- pulsado o a masa" : "alto (en reposo)");
  }
  Serial.printf("Pines en LOW ahora mismo: %u\n", wired);
  Serial.println("Pulsa un boton: debe salir una linea por cada pulsacion.");
  Serial.println("Sin botones cableados: escribe el numero (1-23) y Enter.");
  Serial.println("-----------------------------------------");
}

// Aplica una pulsacion del boton i. Vale igual para un boton fisico que
// para uno simulado por consola.
bool applyPress(uint8_t i) {
  const uint8_t group = BUTTONS[i].group;
  bool changed = false;
  if (group == 0) {
    buttonState[i] = !buttonState[i];
    lastPressedGroup = 0;
    changed = true;
  } else if (!buttonState[i]) {
    // Excluyente: volver a pulsar el que ya esta activo no lo apaga
    for (uint8_t j = 0; j < BUTTON_COUNT; j++) {
      if (BUTTONS[j].group == group) buttonState[j] = (j == i);
    }
    lastPressedGroup = group;
    changed = true;
  }
  if (changed) {
    Serial.printf("  -> boton %u  %s%u = %u  (grupo %u)\n", i + 1,
                  BUTTONS[i].kind == KIND_NOTE ? "NOTE" : "CC", BUTTONS[i].midi,
                  buttonState[i] ? 127 : 0, group);
  } else {
    Serial.printf("  -> boton %u ya estaba activo, sin cambio\n", i + 1);
  }
  return changed;
}

bool scanButtons(uint32_t nowMs) {
  bool changed = false;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    const bool pressedNow = (digitalRead(BUTTONS[i].pin) == LOW);
    if (pressedNow == physicalPressed[i]) continue;
    if (nowMs - lastChangeMs[i] < DEBOUNCE_MS) continue;
    lastChangeMs[i] = nowMs;
    physicalPressed[i] = pressedNow;
    Serial.printf("GPIO%-2u boton %-2u %s\n", BUTTONS[i].pin, i + 1,
                  pressedNow ? "pulsado" : "soltado");

    if (BUTTONS[i].kind == KIND_NOTE) {
      // Momentanea: On al pulsar (127), Off al soltar (0)
      buttonState[i] = pressedNow;
      if (pressedNow) {
        ledFlash(CRGB::Green);
        lastPressedIndex = i;
        pressCount++;
      }
      Serial.printf("  -> boton %u  NOTE%u = %u\n", i + 1, BUTTONS[i].midi,
                    pressedNow ? 127 : 0);
      changed = true;
      continue;
    }

    if (!pressedNow) continue;
    ledFlash(groupColor(BUTTONS[i].group));
    applyPress(i);
    lastPressedIndex = i;
    pressCount++;
    changed = true;
  }
  return changed;
}

// Escribe el numero de boton y Enter para simularlo sin cablear nada
bool handleSerialCommands() {
  static char buf[8];
  static uint8_t len = 0;
  bool changed = false;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c >= '0' && c <= '9') {
      if (len < sizeof(buf) - 1) buf[len++] = c;
      continue;
    }
    if (c != '\r' && c != '\n') continue;
    if (!len) continue;
    buf[len] = 0;
    len = 0;
    const int n = atoi(buf);
    if (n >= 1 && n <= BUTTON_COUNT) {
      const uint8_t i = (uint8_t)(n - 1);
      Serial.printf("SIMULADO boton %d\n", n);
      if (BUTTONS[i].kind == KIND_NOTE) {
        ledFlash(CRGB::Green);
        buttonState[i] = true;
        serialNoteHold = (int8_t)i;
        lastPressedIndex = i;
        pressCount++;
        Serial.printf("  -> NOTE%u On (serie, soltara solo)\n", BUTTONS[i].midi);
      } else {
        ledFlash(groupColor(BUTTONS[i].group));
        applyPress(i);
        lastPressedIndex = i;
        pressCount++;
      }
      changed = true;
    } else {
      Serial.printf("Fuera de rango, usa 1..%u\n", BUTTON_COUNT);
    }
  }
  return changed;
}

void updatePacketFromState() {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    if (buttonState[i]) mask |= (1UL << i);
  }
  txPacket.version = PACKET_VERSION;
  txPacket.seq++;
  txPacket.lastPressed = lastPressedIndex;
  txPacket.pressCount = pressCount;
  txPacket.mask = mask;
  txPacket.batPercent = batPercent;
  txPacket.batFlags = batFlags;
  txPacket.batMv = batMv;
}

uint64_t buildWakeMaskFromButtons() {
  uint64_t wakeMask = 0;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    // Solo GPIO 0..21 son RTC en el S3, el resto no puede despertar
    if (BUTTONS[i].pin <= 21) wakeMask |= (1ULL << BUTTONS[i].pin);
  }
  return wakeMask;
}

void enterDeepSleepIfIdle(uint32_t nowMs) {
  if (TX_IDLE_DEEP_SLEEP_MS == 0) return;
  if (nowMs - lastUserActivityMs < TX_IDLE_DEEP_SLEEP_MS) return;
  uint64_t wakeMask = buildWakeMaskFromButtons();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  delay(5);
  esp_deep_sleep_start();
}
#endif

#if defined(ROLE_RX)
static uint16_t midiDescriptorCb(uint8_t *dst, uint8_t *itf) {
  uint8_t itfnum = *itf;
  *itf += 2;
  uint8_t epOut = tinyusb_get_free_duplex_endpoint();
  if (!epOut) return 0;
  uint8_t epIn = 0x80 | epOut;
  uint8_t desc[] = {TUD_MIDI_DESCRIPTOR(itfnum, 0, epOut, epIn, 64)};
  memcpy(dst, desc, sizeof(desc));
  return sizeof(desc);
}

__attribute__((constructor))
static void registerMidiInterface() {
  tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, midiDescriptorCb);
}

uint32_t rxMask = 0;
uint8_t rxLastPressCount = 0;
uint8_t rxBatPercent = 0xFF;
uint8_t rxBatFlags = 0xFF;
bool rxSeenFirstPacket = false;
bool rxEspNowReady = false;
portMUX_TYPE rxPacketMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool rxPacketPending = false;
EspNowStatePacket rxPendingPacket = {PACKET_VERSION, 0, 0xFF, 0, 0, 0, 0, 0};

static inline void sendMidi3(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t msg[3] = {status, d1, d2};
  tud_midi_stream_write(0, msg, 3);
}

void sendCC(uint8_t cc, uint8_t value) {
  sendMidi3(0xB0 | (MIDI_CHANNEL - 1), cc, value);
}

void sendNote(uint8_t note, bool on) {
  if (on) sendMidi3(0x90 | (MIDI_CHANNEL - 1), note, 127);
  else sendMidi3(0x80 | (MIDI_CHANNEL - 1), note, 0);
}

void sendButtonMidi(uint8_t i, bool on) {
  if (BUTTONS[i].kind == KIND_NOTE) sendNote(BUTTONS[i].midi, on);
  else sendCC(BUTTONS[i].midi, on ? 127 : 0);
}

void sendRxPadMidi(uint8_t i, bool on) {
  if (RX_PADS[i].kind == KIND_NOTE) sendNote(RX_PADS[i].midi, on);
  else sendCC(RX_PADS[i].midi, on ? 127 : 0);
}

bool rxPadPhysical[RX_PAD_COUNT] = {};
bool rxPadState[RX_PAD_COUNT] = {};
uint32_t rxPadLastChangeMs[RX_PAD_COUNT] = {};
uint32_t rxPadLastScanMs = 0;

void initRxPads() {
  for (uint8_t i = 0; i < RX_PAD_COUNT; i++) {
    pinMode(RX_PADS[i].pin, INPUT_PULLUP);
    rxPadPhysical[i] = (digitalRead(RX_PADS[i].pin) == LOW);
    rxPadState[i] = false;
  }
}

// Escanea pads locales del RX. Devuelve true si hubo actividad (para LED).
// Notas + CC 88 = momentaneos. Resto de CC = latch on/off al pulsar.
bool scanRxPads(uint32_t nowMs) {
  bool activity = false;
  for (uint8_t i = 0; i < RX_PAD_COUNT; i++) {
    const bool pressedNow = (digitalRead(RX_PADS[i].pin) == LOW);
    if (pressedNow == rxPadPhysical[i]) continue;
    if (nowMs - rxPadLastChangeMs[i] < DEBOUNCE_MS) continue;
    rxPadLastChangeMs[i] = nowMs;
    rxPadPhysical[i] = pressedNow;

    const bool momentary = (RX_PADS[i].kind == KIND_NOTE) ||
                           (RX_PADS[i].kind == KIND_CC && rxCcIsMomentary(RX_PADS[i].midi));

    if (momentary) {
      // Pulsar On, soltar Off
      rxPadState[i] = pressedNow;
      // Siempre intentar enviar: tud_midi_mounted() a veces falla en Win
      // aunque el dispositivo ya aparece como MIDI Trigger.
      sendRxPadMidi(i, pressedNow);
      activity = true;
      continue;
    }

    // CC latch: solo al pulsar; soltar no manda MIDI
    if (!pressedNow) continue;
    rxPadState[i] = !rxPadState[i];
    sendRxPadMidi(i, rxPadState[i]);
    activity = true;
  }
  return activity;
}

void sendAllRxPadsToMidi() {
  for (uint8_t i = 0; i < RX_PAD_COUNT; i++) {
    sendRxPadMidi(i, rxPadState[i]);
  }
}

// Panel de escritorio: entra por USB MIDI en canal 16 y se reenvia a FL como
// canal 1. FL nunca manda en canal 16, asi que no puede realimentarse.
static constexpr uint8_t PANEL_STATUS = 0xBF;
static constexpr uint8_t NO_CHANGE = 0xFF;
static constexpr uint8_t NOTE_LED = 0xFE;  // LED verde en RX para notas
static constexpr uint8_t RX_PAD_LED = 0xFD;  // LED cian pads locales RX

// Solo CCs del mapa (no notas: 71/72 chocan con CC del quinteto)
static inline uint8_t ccGroup(uint8_t cc) {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    if (BUTTONS[i].kind == KIND_CC && BUTTONS[i].midi == cc) return BUTTONS[i].group;
  }
  return NO_CHANGE;
}

uint8_t echoPanelMidi(const uint8_t *data, uint32_t len) {
  static uint8_t step = 0;
  static uint8_t cc = 0;
  uint8_t forwardedGroup = NO_CHANGE;
  for (uint32_t i = 0; i < len; i++) {
    const uint8_t b = data[i];
    if (b & 0x80) {
      step = (b == PANEL_STATUS) ? 1 : 0;
      continue;
    }
    if (step == 1) {
      cc = b;
      step = 2;
    } else if (step == 2) {
      const uint8_t group = ccGroup(cc);
      if (group != NO_CHANGE) {
        sendCC(cc, b);
        forwardedGroup = group;
      }
      step = 1;  // running status
    }
  }
  return forwardedGroup;
}

void sendAllStateToMidi() {
  if (!tud_midi_mounted()) return;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    sendButtonMidi(i, (rxMask & (1UL << i)) != 0);
  }
  sendAllRxPadsToMidi();
}

// Solo actualiza estado interno + LED; no emite CC (rompe MIDI Learn).
void trackBatteryFromPacket(const EspNowStatePacket &packet) {
  if (packet.batPercent == 0xFF) return;
  rxBatPercent = packet.batPercent > 100 ? 100 : packet.batPercent;
  rxBatFlags = packet.batFlags;
}

// Devuelve el grupo del boton implicado, NOTE_LED, o NO_CHANGE si es solo latido.
uint8_t applyPacketToMidi(const EspNowStatePacket &packet) {
  const uint32_t diff = packet.mask ^ rxMask;
  rxMask = packet.mask;

  bool huboPulsacion = false;
  if (!rxSeenFirstPacket) {
    rxSeenFirstPacket = true;
  } else if (packet.pressCount != rxLastPressCount) {
    huboPulsacion = true;
  }
  rxLastPressCount = packet.pressCount;

  if (!diff && !huboPulsacion) return NO_CHANGE;

  const bool canSend = tud_midi_mounted();

  if (diff) {
    uint8_t ledHint = NO_CHANGE;
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
      if (diff & (1UL << i)) {
        ledHint = (BUTTONS[i].kind == KIND_NOTE) ? NOTE_LED : BUTTONS[i].group;
        break;
      }
    }
    if (canSend) {
      // Primero los que se apagan (Note Off / CC 0)
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if ((diff & (1UL << i)) && !(packet.mask & (1UL << i))) sendButtonMidi(i, false);
      }
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if ((diff & (1UL << i)) && (packet.mask & (1UL << i))) sendButtonMidi(i, true);
      }
    }
    return ledHint;
  }

  // Pulsacion sin cambio de estado (solo CC excluyentes): reafirma
  const uint8_t i = packet.lastPressed;
  if (i >= BUTTON_COUNT) return NO_CHANGE;
  if (canSend) sendButtonMidi(i, (rxMask & (1UL << i)) != 0);
  return (BUTTONS[i].kind == KIND_NOTE) ? NOTE_LED : BUTTONS[i].group;
}
#endif

void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (esp_now_init() != ESP_OK) return;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    esp_now_add_peer(&peerInfo);
  }
}

#if defined(ROLE_RX)
// v3 = solo botones (TX viejo sin USB). v4 = botones + bateria.
static constexpr size_t PACKET_SIZE_V3 = 8;   // version..mask
static constexpr size_t PACKET_SIZE_V4 = sizeof(EspNowStatePacket);

void onEspNowRx(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  EspNowStatePacket packet = {};
  if (len == (int)PACKET_SIZE_V4) {
    memcpy(&packet, data, PACKET_SIZE_V4);
    if (packet.version != 4) return;
  } else if (len == (int)PACKET_SIZE_V3) {
    memcpy(&packet, data, PACKET_SIZE_V3);
    if (packet.version != 3) return;
    packet.batPercent = 0xFF;  // sin telemetria
    packet.batFlags = 0;
    packet.batMv = 0;
  } else {
    return;
  }
  portENTER_CRITICAL(&rxPacketMux);
  rxPendingPacket = packet;
  rxPacketPending = true;
  portEXIT_CRITICAL(&rxPacketMux);
}
#endif

void setup() {
#if defined(ROLE_TX) || defined(ROLE_RX)
  FastLED.addLeds<WS2812, LED_PIN_A, GRB>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812, LED_PIN_B, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(80);
  leds[0] = CRGB::Black;
  FastLED.show();
#endif

#if defined(ROLE_RX)
  USB.productName("ESP32-S3 MIDI Trigger");
  USB.manufacturerName("ESP32S3");
  USB.begin();
  initEspNow();
  esp_now_register_recv_cb(onEspNowRx);
  rxEspNowReady = true;
  initRxPads();
#else
  initEspNow();
#endif

#if defined(ROLE_TX)
  initButtons();
  initBattery();
  sampleBattery(millis());
  initLog();
  lastUserActivityMs = millis();
  updatePacketFromState();
  esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&txPacket), sizeof(txPacket));
  lastTxMs = millis();
#endif
}

void loop() {
#if defined(ROLE_TX)
  uint32_t now = millis();
  sampleBattery(now);
  if (now - lastScanMs >= TX_SCAN_INTERVAL_MS) {
    lastScanMs = now;
    const bool fromSerial = handleSerialCommands();
    if (scanButtons(now) || fromSerial) {
      lastUserActivityMs = now;
      updatePacketFromState();
      esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&txPacket), sizeof(txPacket));
      lastTxMs = now;
      // Tras simular NOTE On por serie, soltar en el siguiente ciclo
      if (serialNoteHold >= 0) {
        buttonState[(uint8_t)serialNoteHold] = false;
        serialNoteHold = -1;
        updatePacketFromState();
        esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&txPacket),
                     sizeof(txPacket));
        lastTxMs = millis();
      }
    }
  }
  if (now - lastTxMs >= TX_HEARTBEAT_MS) {
    updatePacketFromState();
    const esp_err_t rc = esp_now_send(BROADCAST_MAC,
                                      reinterpret_cast<const uint8_t *>(&txPacket),
                                      sizeof(txPacket));
    lastTxMs = now;
    static uint32_t lastLogMs = 0;
    if (now - lastLogMs >= 5000) {
      lastLogMs = now;
      Serial.printf("[vivo] mascara=0x%06lX  bat=%umV %u%% flags=0x%02X  envio=%s\n",
                    (unsigned long)txPacket.mask, (unsigned)batMv, (unsigned)batPercent,
                    (unsigned)batFlags, rc == ESP_OK ? "ok" : esp_err_to_name(rc));
    }
  }
  // Destello de carga cada 10 s si el LED esta libre (verde/amarillo/rojo)
  {
    static uint32_t lastBatLedMs = 0;
    if (!ledOn && now - lastBatLedMs >= 10000) {
      lastBatLedMs = now;
      CRGB c = CRGB::Green;
      if (batPercent < BAT_LOW_PERCENT) c = CRGB::Red;
      else if (batPercent < 60) c = CRGB::Yellow;
      ledFlash(c);
    }
  }
  ledUpdate();
  enterDeepSleepIfIdle(now);
  delay(1);
#else
  static bool wasMidiMounted = false;
  const bool midiMounted = tud_midi_mounted();
  const uint32_t now = millis();

  uint8_t midiIn[16];
  while (tud_midi_available()) {
    const uint32_t got = tud_midi_stream_read(midiIn, sizeof(midiIn));
    if (!got || !midiMounted) continue;
    const uint8_t group = echoPanelMidi(midiIn, got);
    if (group != NO_CHANGE) ledFlash(groupColor(group));
  }

  // Pads cableados en el RX (notas + CC momentaneos)
  if (now - rxPadLastScanMs >= 5) {
    rxPadLastScanMs = now;
    if (scanRxPads(now)) ledFlash(CRGB::Cyan);
  }

  // Si FL reabre el puerto USB MIDI, reenviar estado completo
  if (midiMounted && !wasMidiMounted) {
    sendAllStateToMidi();
    ledFlash(CRGB::Green);
  }
  wasMidiMounted = midiMounted;

  bool hasPendingPacket = false;
  EspNowStatePacket packetToApply;
  portENTER_CRITICAL(&rxPacketMux);
  if (rxPacketPending) {
    packetToApply = rxPendingPacket;
    rxPacketPending = false;
    hasPendingPacket = true;
  }
  portEXIT_CRITICAL(&rxPacketMux);

  if (hasPendingPacket) {
    trackBatteryFromPacket(packetToApply);
    // Solo parpadea si cambio un boton, no con cada latido del TX
    const uint8_t group = applyPacketToMidi(packetToApply);
    if (group != NO_CHANGE) {
      CRGB c = CRGB::Red;
      if (midiMounted) c = (group == NOTE_LED) ? CRGB::Green : groupColor(group);
      ledFlash(c);
    } else if ((packetToApply.batFlags & BAT_FLAG_LOW) && midiMounted) {
      // Destello naranja solo con pila baja (sin MIDI CC)
      static uint32_t lastLowFlashMs = 0;
      const uint32_t t = millis();
      if (t - lastLowFlashMs > 3000) {
        ledFlash(CRGB::Orange);
        lastLowFlashMs = t;
      }
    }
  }
  ledUpdate();
  delay(1);
#endif
}
