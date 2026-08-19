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
    case 2:  return CRGB::Green;   // grupo de 2
    case 3:  return CRGB::Yellow;  // grupo de 3
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
// pin solo lo usa el TX; el RX solo necesita cc y el orden de la tabla.
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
struct ButtonDef {
  uint8_t pin;
  uint8_t cc;
  uint8_t group;
};

// Telemetria de bateria (solo TX mide; RX traduce a MIDI CC)
static constexpr uint8_t BAT_ADC_PIN = 3;
// -1 = no cableado (usuario omitio CHRG). Si luego sueldas CHRG, pon 46.
static constexpr int8_t BAT_CHRG_PIN = -1;
static constexpr uint8_t CC_BAT_PERCENT = 110;  // 0-127
static constexpr uint8_t CC_BAT_CHARGING = 111; // 0 / 127
static constexpr uint8_t CC_BAT_LOW = 112;      // 0 / 127 si < 20%
static constexpr uint8_t BAT_LOW_PERCENT = 20;
static constexpr float BAT_DIVIDER_RATIO = 2.0f;  // 100k + 100k
static constexpr uint32_t BAT_SAMPLE_MS = 500;

static constexpr ButtonDef BUTTONS[] = {
    // Botones 1-6: kits, excluyentes entre si
    {4, 30, 1},  {5, 31, 1},  {6, 32, 1},  {7, 33, 1},  {15, 34, 1}, {16, 35, 1},
    // Botones 7-8: grupo de 2, excluyentes entre si
    {8, 20, 2},  {9, 21, 2},
    // Botones 9-11: grupo de 3, excluyentes entre si
    {10, 22, 3}, {11, 23, 3}, {12, 24, 3},
    // Botones 12-16: grupo de 5, excluyentes entre si
    {13, 25, 4}, {1, 70, 4},  {2, 71, 4},  {14, 72, 4}, {17, 73, 4},
    // Botones 17-23: on/off independientes
    {18, 74, 0}, {21, 75, 0}, {39, 76, 0}, {40, 77, 0}, {41, 78, 0},
    {42, 79, 0}, {47, 80, 0},
};

static constexpr uint8_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
static_assert(BUTTON_COUNT <= 32, "La mascara del paquete es de 32 bits");

static constexpr uint8_t MIDI_CHANNEL = 1;
static constexpr uint32_t DEBOUNCE_MS = 20;

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
  Serial.println(" n   GPIO  CC   grupo  nivel");
  uint8_t wired = 0;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    const bool low = (digitalRead(BUTTONS[i].pin) == LOW);
    if (low) wired++;
    Serial.printf("%2u   %-4u  %-3u  %-5u  %s\n", i + 1, BUTTONS[i].pin, BUTTONS[i].cc,
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
    Serial.printf("  -> boton %u  CC%u = %u  (grupo %u)\n", i + 1, BUTTONS[i].cc,
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
    if (!pressedNow) continue;
    ledFlash(groupColor(BUTTONS[i].group));
    applyPress(i);
    // Se envia siempre, cambie el estado o no: la pulsacion en si ya es
    // informacion, y asi el RX puede confirmarla con su LED y su CC.
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
      Serial.printf("SIMULADO boton %d\n", n);
      ledFlash(groupColor(BUTTONS[n - 1].group));
      applyPress((uint8_t)(n - 1));
      lastPressedIndex = (uint8_t)(n - 1);
      pressCount++;
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

// Panel de escritorio: entra por USB MIDI en canal 16 y se reenvia a FL como
// canal 1. FL nunca manda en canal 16, asi que no puede realimentarse.
static constexpr uint8_t PANEL_STATUS = 0xBF;
static constexpr uint8_t NO_CHANGE = 0xFF;

// Grupo del boton que usa ese CC, o NO_CHANGE si el CC no es nuestro
static inline uint8_t ccGroup(uint8_t cc) {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    if (BUTTONS[i].cc == cc) return BUTTONS[i].group;
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
    sendCC(BUTTONS[i].cc, (rxMask & (1UL << i)) ? 127 : 0);
  }
  if (rxBatPercent != 0xFF) {
    const uint8_t ccPct = (uint8_t)((rxBatPercent * 127UL) / 100UL);
    sendCC(CC_BAT_PERCENT, ccPct);
    sendCC(CC_BAT_CHARGING, (rxBatFlags & BAT_FLAG_CHARGING) ? 127 : 0);
    sendCC(CC_BAT_LOW, (rxBatFlags & BAT_FLAG_LOW) ? 127 : 0);
  }
}

void sendBatteryMidi(const EspNowStatePacket &packet) {
  if (!tud_midi_mounted()) return;
  // 0xFF = paquete v3 sin bateria (TX aun no reflasheado)
  if (packet.batPercent == 0xFF) return;
  const uint8_t pct = packet.batPercent > 100 ? 100 : packet.batPercent;
  const uint8_t flags = packet.batFlags;
  const uint8_t ccPct = (uint8_t)((pct * 127UL) / 100UL);
  if (pct != rxBatPercent) {
    sendCC(CC_BAT_PERCENT, ccPct);
    rxBatPercent = pct;
  }
  if (flags != rxBatFlags) {
    sendCC(CC_BAT_CHARGING, (flags & BAT_FLAG_CHARGING) ? 127 : 0);
    sendCC(CC_BAT_LOW, (flags & BAT_FLAG_LOW) ? 127 : 0);
    rxBatFlags = flags;
  }
}

// Devuelve el grupo del boton implicado, o NO_CHANGE si el paquete es solo el
// latido que el TX repite cada segundo.
uint8_t applyPacketToMidi(const EspNowStatePacket &packet) {
  const uint32_t diff = packet.mask ^ rxMask;
  rxMask = packet.mask;

  // pressCount sube en cada pulsacion aunque el estado no cambie, para que
  // repetir el excluyente ya activo tambien de senal de vida.
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
    uint8_t changedGroup = 0;
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
      if (diff & (1UL << i)) {
        changedGroup = BUTTONS[i].group;
        break;
      }
    }
    if (canSend) {
      // Primero los que se apagan: al cambiar de kit evita que suenen dos a la vez
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if ((diff & (1UL << i)) && !(packet.mask & (1UL << i))) sendCC(BUTTONS[i].cc, 0);
      }
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if ((diff & (1UL << i)) && (packet.mask & (1UL << i))) sendCC(BUTTONS[i].cc, 127);
      }
    }
    return changedGroup;
  }

  // Pulsacion sin cambio de estado: reafirmamos el valor de ese boton
  const uint8_t i = packet.lastPressed;
  if (i >= BUTTON_COUNT) return NO_CHANGE;
  if (canSend) sendCC(BUTTONS[i].cc, (rxMask & (1UL << i)) ? 127 : 0);
  return BUTTONS[i].group;
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
  USB.productName("ESP32-S3 MIDI Controller");
  USB.manufacturerName("ESP32S3");
  USB.begin();
  initEspNow();
  esp_now_register_recv_cb(onEspNowRx);
  rxEspNowReady = true;
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
  ledUpdate();
  enterDeepSleepIfIdle(now);
  delay(1);
#else
  static bool wasMidiMounted = false;
  const bool midiMounted = tud_midi_mounted();

  uint8_t midiIn[16];
  while (tud_midi_available()) {
    const uint32_t got = tud_midi_stream_read(midiIn, sizeof(midiIn));
    if (!got || !midiMounted) continue;
    const uint8_t group = echoPanelMidi(midiIn, got);
    if (group != NO_CHANGE) ledFlash(groupColor(group));
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
    sendBatteryMidi(packetToApply);
    // Solo parpadea si cambio un boton, no con cada latido del TX
    const uint8_t group = applyPacketToMidi(packetToApply);
    if (group != NO_CHANGE) ledFlash(midiMounted ? groupColor(group) : CRGB::Red);
    else if ((packetToApply.batFlags & BAT_FLAG_LOW) && midiMounted) {
      // Destello corto rojo solo cuando llega alerta de pila baja (latido)
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
