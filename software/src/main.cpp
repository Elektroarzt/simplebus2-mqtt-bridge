#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Debounce.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include "web_ui.h"

#define FIRMWARE_VERSION "2.0.0"

// Forward declarations
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
void IRAM_ATTR function_ISR();

// ---------------------------------------------------------------------------
// Hardware pin definitions
// ---------------------------------------------------------------------------
#define LEDPIN            4
#define INTERCOM_READ_PIN 2
#define INTERCOM_WRITE_PIN 3
#define BUTTONPIN         8
#define SDA_1             6
#define SCL_1             7
#define SDA_2             9
#define SCL_2             10

// ---------------------------------------------------------------------------
// Hardware settings
// ---------------------------------------------------------------------------
#define MCP4017_MAX_RESISTANCE  100000
#define MCP4017_I2C_ADDRESS     0x2F
#define I2C_FREQ                100000
#define OPV_FIXED_GAIN_RESISTOR 100000

// ---------------------------------------------------------------------------
// SimpleBus2 message codes
// ---------------------------------------------------------------------------
#define MSG_CALL_TO_MAIN_SWITCHBOARD              8
#define MSG_CALL_CARETAKER_DOOR_ENTRY_PHONE       9
#define MSG_OPEN_DOOR                            16
#define MSG_HOOK_OFF                             17
#define MSG_HOOK_ON                              18
#define MSG_CALL_TO_SECONDARY_SWITCHBOARD        19
#define MSG_CAMERA_ON                            20
#define MSG_CALL_FROM_FLOOR_DOOR                 21
#define MSG_CALL_INTERCOM                        24
#define MSG_CALL_INTERCOM_RESPONSE               26
#define MSG_GENERIC_ACTUATOR                     29
#define MSG_HIGH_PRIO_CALL_TO_MAIN_SWITCHBOARD   30
#define MSG_CALL_FROM_SWITCHBOARD_1              32
#define MSG_UNKNOWN_1                            33
#define MSG_CALL_FROM_SWITCHBOARD_2              37
#define MSG_CALL_FROM_SWITCHBOARD_3              42
#define MSG_CALL_FROM_SWITCHBOARD_4              43
#define MSG_CALL_FROM_SWITCHBOARD_5_SCREEN_ON    45
#define MSG_CALL_FROM_ENTRY_DOOR                 48
#define MSG_SCREEN_OFF                           49
#define MSG_CALL_FROM_ENTRY_DOOR_SCREEN_ON       50
#define MSG_START_BLINKING_OPEN_DOOR_BTN         51
#define MSG_STOP_BLINKING_OPEN_DOOR_BTN          52

// ---------------------------------------------------------------------------
// Decoder timing constants (µs)
// ---------------------------------------------------------------------------
#define CARRIER_MAX_DELTA_US   100u
#define PREAMBLE_SILENCE_US  20000u
#define BIT_ZERO_SILENCE_US   6000u
#define BIT_ONE_SILENCE_US    9000u
#define TIMING_TOLERANCE       0.12f

// ---------------------------------------------------------------------------
// Deduplication / repeat-ring constants
// ---------------------------------------------------------------------------
#define DUPLICATE_WINDOW_MS   6000u
#define REPEAT_RING_THRESHOLD    3
#define REPEAT_RING_WINDOW_MS 5000u

// ---------------------------------------------------------------------------
// Settings (stored in EEPROM)
// ---------------------------------------------------------------------------
#define SETTINGS_VERSION 3

struct Settings {
  uint8_t version;
  char mqttServer[40];
  char mqttUser[40];
  char mqttPassword[40];
  int  gain;
  int  compVoltLvl;
  bool filter;
  bool ringAutomation;
  int  intercomAddress;
  char mqttPrefix[32];
  int  ringToOpenMinutes;
} mySettings;

// ---------------------------------------------------------------------------
// MQTT / WiFi globals
// ---------------------------------------------------------------------------
WiFiClient  espClient;
PubSubClient client(espClient);
WebServer    webServer(80);

// ---------------------------------------------------------------------------
// OTA state
// ---------------------------------------------------------------------------
static bool   otaPending = false;
static String otaUrl     = "";

// ---------------------------------------------------------------------------
// Home Assistant MQTT Auto Discovery
// ---------------------------------------------------------------------------
#define HA_DISCOVERY_PREFIX  "homeassistant"

static char _topicBuf[64];
const char* T(const char* suffix) {
  snprintf(_topicBuf, sizeof(_topicBuf), "%s/%s", mySettings.mqttPrefix, suffix);
  return _topicBuf;
}

// ---------------------------------------------------------------------------
// Debug counters
// ---------------------------------------------------------------------------
volatile uint32_t dbgIsrCount      = 0;
volatile uint32_t dbgDecodeOk      = 0;
volatile uint32_t dbgAddrMismatch  = 0;
volatile uint32_t dbgChecksumFail  = 0;
         uint32_t dbgMqttPublished = 0;
         uint8_t  dbgLastCode      = 0;
         uint8_t  dbgLastAddr      = 0;

// ---------------------------------------------------------------------------
// Ring-to-open state
// ---------------------------------------------------------------------------
bool         ringToOpenStarted    = false;
unsigned int ringToOpenMillis     = 600u * 60u * 1000u;
unsigned int ringToOpenMillisStart = 0;
unsigned long lastOpenDoorTime    = 0;
unsigned long lastHeartbeatMillis = 0;

// ---------------------------------------------------------------------------
// Teach-in state
// ---------------------------------------------------------------------------
bool          teachInActive        = false;
unsigned long teachInTimeActivated = 0;

// ---------------------------------------------------------------------------
// Button state
// ---------------------------------------------------------------------------
Debounce Button(BUTTONPIN);
int lastState   = LOW;
int currentState;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

// ===========================================================================
// ISR RING BUFFER
// ===========================================================================
#define ISR_BUFFER_SIZE 128

struct IsrEvent {
  uint32_t ts;
  uint8_t  level;
};

volatile IsrEvent isrBuffer[ISR_BUFFER_SIZE];
volatile uint16_t isrWriteIdx = 0;
         uint16_t isrReadIdx  = 0;

// ===========================================================================
// SIMPLEBUS2 STATE-MACHINE DECODER
// ===========================================================================
class SimpleBusDecoder {
public:
  enum State { IDLE, DECODE_BITS };

  struct Result {
    bool         valid;
    bool         isRepeatRing;
    unsigned int code;
    unsigned int addr;
    unsigned int checksum;
  };

  State        state         = IDLE;
  int          bitArray[18]  = {};
  int          bitPos        = 0;
  uint32_t     lastEdgeUs    = 0;
  bool         edgeSeenOnce  = false;

  unsigned long lastFullMsgMs  = 0;
  unsigned int  lastCode       = 0;
  unsigned int  lastAddr       = 0;
  int           shortBurstCount = 0;

  Result processEdge(uint32_t ts, uint8_t /*level*/) {
    Result res = {};

    if (!edgeSeenOnce) {
      lastEdgeUs  = ts;
      edgeSeenOnce = true;
      return res;
    }

    uint32_t delta = ts - lastEdgeUs;
    lastEdgeUs = ts;

    if (delta < CARRIER_MAX_DELTA_US) return res;

    if (within(delta, PREAMBLE_SILENCE_US)) {
      state          = DECODE_BITS;
      bitPos         = 0;
      shortBurstCount = 0;
      return res;
    }

    if (state == DECODE_BITS) {
      if (within(delta, BIT_ZERO_SILENCE_US)) {
        bitArray[bitPos++] = 0;
      } else if (within(delta, BIT_ONE_SILENCE_US)) {
        bitArray[bitPos++] = 1;
      } else {
        state = IDLE;
        return res;
      }

      if (bitPos == 18) {
        state = IDLE;
        unsigned int code     = bitsToInt(0, 6);
        unsigned int addr     = bitsToInt(6, 8);
        unsigned int checksum = bitsToInt(14, 4);
        lastCode       = code;
        lastAddr       = addr;
        lastFullMsgMs  = millis();
        shortBurstCount = 0;
        res = {true, false, code, addr, checksum};
      }

    } else {
      if (within(delta, BIT_ZERO_SILENCE_US) &&
          lastFullMsgMs > 0 &&
          (millis() - lastFullMsgMs) < REPEAT_RING_WINDOW_MS) {
        shortBurstCount++;
        if (shortBurstCount >= REPEAT_RING_THRESHOLD) {
          shortBurstCount = 0;
          lastFullMsgMs   = millis();
          res = {true, true, lastCode, lastAddr, 0};
        }
      } else if (!within(delta, BIT_ZERO_SILENCE_US)) {
        shortBurstCount = 0;
      }
    }
    return res;
  }

private:
  static bool within(uint32_t val, uint32_t target) {
    uint32_t lo = (uint32_t)(target * (1.0f - TIMING_TOLERANCE));
    uint32_t hi = (uint32_t)(target * (1.0f + TIMING_TOLERANCE));
    return val >= lo && val <= hi;
  }

  unsigned int bitsToInt(int start, int len) {
    unsigned int result = 0;
    for (int i = 0; i < len; i++) {
      if (bitArray[start + i]) result |= (1u << i);
    }
    return result;
  }
};

SimpleBusDecoder decoder;

// ---------------------------------------------------------------------------
// Deduplication state
// ---------------------------------------------------------------------------
struct {
  unsigned int  code;
  unsigned int  addr;
  unsigned long tsMs;
} lastDecoded = {};

// ---------------------------------------------------------------------------
// Decoded message queue
// ---------------------------------------------------------------------------
volatile bool         messageOk   = false;
volatile unsigned int messageCode = 0;

// ===========================================================================
// NON-BLOCKING DOORBELL STATE MACHINE
// ===========================================================================
struct DoorbellState {
  bool         pending;
  unsigned int msgCode;
  unsigned long triggerMs;
} doorbellPending = {};

// ===========================================================================
// Helper utilities
// ===========================================================================

// GPIO9 is both SDA_2 (I2C) and the ESP32-C3 BOOT strapping pin.
// Driving it LOW at reset puts the chip into USB_BOOT (download mode).
// Always call this instead of ESP.restart() to ensure GPIO9 is HIGH.
void safeRestart() {
  Wire.end();
  pinMode(SDA_2, INPUT_PULLUP);
  delay(10);
  ESP.restart();
}

void applyDefaults() {
  mySettings.version          = SETTINGS_VERSION;
  strcpy(mySettings.mqttPassword,  "");
  strcpy(mySettings.mqttServer,    "");
  strcpy(mySettings.mqttUser,      "");
  strcpy(mySettings.mqttPrefix,    "SimpleBus");
  mySettings.gain             = 20;
  mySettings.compVoltLvl      = 600;
  mySettings.filter           = true;
  mySettings.ringAutomation   = false;
  mySettings.intercomAddress  = 0;
  mySettings.ringToOpenMinutes = 600;
}

void saveSettings() {
  EEPROM.put(0, mySettings);
  EEPROM.commit();
}

void loadSettings() {
  EEPROM.get(0, mySettings);
  if (mySettings.version != SETTINGS_VERSION) {
    Serial.printf("EEPROM version %d != %d, re-initializing defaults\n",
                  mySettings.version, SETTINGS_VERSION);
    applyDefaults();
    saveSettings();
  }
  if (mySettings.mqttPrefix[0] == '\0') {
    strcpy(mySettings.mqttPrefix, "SimpleBus");
  }
  ringToOpenMillis = (unsigned int)mySettings.ringToOpenMinutes * 60u * 1000u;
  Serial.printf("MQTT server: %s, prefix: %s, addr: %d\n",
                mySettings.mqttServer, mySettings.mqttPrefix, mySettings.intercomAddress);
}

bool structInitialized() {
  return mySettings.version == SETTINGS_VERSION &&
         mySettings.compVoltLvl != 0 &&
         mySettings.gain != 0 &&
         strlen(mySettings.mqttServer) > 1;
}

boolean isNumeric(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) return false;
  }
  return true;
}

void setupI2C(int bus) {
  int sda = (bus == 1) ? SDA_1 : SDA_2;
  int scl = (bus == 1) ? SCL_1 : SCL_2;
  Wire.begin(sda, scl, I2C_FREQ);
  Wire.beginTransmission(0b111111111);
  Wire.endTransmission();
}

void setPotValue(byte val) {
  Wire.beginTransmission(MCP4017_I2C_ADDRESS);
  Wire.write(val & 0x7F);
  Wire.endTransmission();
  Wire.end();
  // GPIO9 is the ESP32-C3 BOOT strapping pin AND SDA_2. Reconfigure as
  // INPUT_PULLUP immediately after I2C so it reads HIGH on the next reset.
  pinMode(SDA_2, INPUT_PULLUP);
}

void setPotResistance(float resistance) {
  if (resistance < 0 || resistance > MCP4017_MAX_RESISTANCE) return;
  float pct = ((resistance - 325.0f) / MCP4017_MAX_RESISTANCE) * 100.0f;
  if (pct < 0 || pct > 100) return;
  setPotValue((byte)round((pct / 100.0f) * 127.0f));
}

void setOPVGain(int gain) {
  if (gain < 2) return;
  setupI2C(1);
  setPotResistance((float)OPV_FIXED_GAIN_RESISTOR / (gain - 1));
}

void setComparatorVoltageLimit(int voltage) {
  if (voltage < 100 || voltage > 1500) return;
  setupI2C(2);
  float r = ((float)voltage * OPV_FIXED_GAIN_RESISTOR) / (3300.0f - voltage);
  setPotResistance(r);
}

void sendPWM() {
  ledcWrite(0, 50);
  delay(3);
  ledcWrite(0, 0);
}

void intToBinary(unsigned int input, int startPos, int noOfBits, int* bits) {
  for (int i = 0; i < noOfBits; i++) {
    bits[startPos + i] = (input & (1u << i)) != 0;
  }
}

unsigned int binaryToInt(int startPos, int numOfBits, int binaryArray[]) {
  unsigned int result = 0;
  for (int i = 0; i < numOfBits; i++) {
    if (binaryArray[startPos + i]) result |= (1u << i);
  }
  return result;
}

void sendMessage(bool bit) {
  sendPWM();
  delay(bit ? 6 : 3);
}

void writeMessageToIntercom(int msgCode, int msgAddr) {
  detachInterrupt(INTERCOM_READ_PIN);
  if (msgCode == MSG_OPEN_DOOR) lastOpenDoorTime = millis();

  int msgArray[18] = {};
  int checksum = __builtin_popcount(msgCode) + __builtin_popcount(msgAddr);
  intToBinary(msgCode,    0, 6, msgArray);
  intToBinary(msgAddr,    6, 8, msgArray);
  intToBinary(checksum,  14, 4, msgArray);

  sendPWM();
  delay(17);
  for (int i = 0; i < 18; i++) sendMessage(msgArray[i]);
  sendPWM();

  attachInterrupt(INTERCOM_READ_PIN, function_ISR, CHANGE);
}

void startStopRingToOpen(bool start) {
  client.publish(T("RingToOpenStatus"), start ? "ON" : "OFF", true);
  ringToOpenStarted = start;
}

// ===========================================================================
// ISR
// ===========================================================================
static volatile uint32_t lastAnyEdgeTs = 0;

void IRAM_ATTR function_ISR() {
  uint32_t ts    = (uint32_t)micros();
  uint32_t delta = ts - lastAnyEdgeTs;
  lastAnyEdgeTs  = ts;
  dbgIsrCount++;

  if (delta >= CARRIER_MAX_DELTA_US) {
    uint16_t next = (isrWriteIdx + 1) & (ISR_BUFFER_SIZE - 1);
    if (next != isrReadIdx) {
      isrBuffer[isrWriteIdx].ts    = ts;
      isrBuffer[isrWriteIdx].level = 1;
      isrWriteIdx = next;
    }
  }
}

// ===========================================================================
// ISR buffer consumer + protocol decoder
// ===========================================================================
void processISRData(uint32_t ts, uint8_t level) {
  SimpleBusDecoder::Result r = decoder.processEdge(ts, level);
  if (!r.valid) return;

  if (!r.isRepeatRing) {
    int expected = __builtin_popcount(r.code) + __builtin_popcount(r.addr);
    if ((int)r.checksum != expected) { dbgChecksumFail++; return; }
    if ((int)r.addr != mySettings.intercomAddress) {
      dbgAddrMismatch++;
      dbgLastCode = r.code; dbgLastAddr = r.addr;
      if (teachInActive) {
        mySettings.intercomAddress = r.addr;
        saveSettings();
        teachInActive = false;
        digitalWrite(LEDPIN, LOW);
      }
      return;
    }
  } else {
    if ((int)r.addr != mySettings.intercomAddress) { dbgAddrMismatch++; return; }
  }

  if (lastDecoded.code == r.code &&
      lastDecoded.addr == r.addr &&
      (millis() - lastDecoded.tsMs) < DUPLICATE_WINDOW_MS) {
    return;
  }
  lastDecoded = {r.code, r.addr, millis()};

  dbgDecodeOk++;
  dbgLastCode = r.code; dbgLastAddr = r.addr;

  messageCode = r.code;
  messageOk   = true;
}

void consumeIsrBuffer() {
  while (isrReadIdx != isrWriteIdx) {
    IsrEvent ev = {isrBuffer[isrReadIdx].ts, isrBuffer[isrReadIdx].level};
    isrReadIdx = (isrReadIdx + 1) & (ISR_BUFFER_SIZE - 1);
    processISRData(ev.ts, ev.level);
  }
}

// ===========================================================================
// Message processing
// ===========================================================================
void processMessage(unsigned int msgCode) {
  switch (msgCode) {
    case MSG_CALL_FROM_ENTRY_DOOR:
    case MSG_CALL_FROM_ENTRY_DOOR_SCREEN_ON:
      doorbellPending.pending   = true;
      doorbellPending.msgCode   = msgCode;
      doorbellPending.triggerMs = millis();
      break;

    case MSG_CALL_FROM_FLOOR_DOOR:
      dbgMqttPublished++;
      client.publish(T("FloorDoor"), "ON");
      break;

    case MSG_OPEN_DOOR:
      lastOpenDoorTime = millis();
      break;

    default:
      break;
  }
}

void handleDoorbellProcessing() {
  if (!doorbellPending.pending) return;
  if (millis() < doorbellPending.triggerMs) return;

  doorbellPending.pending = false;
  lastDecoded.tsMs = millis();

  if (ringToOpenStarted) {
    client.publish(T("RingToOpenOpened"), "ON");
    writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);
    startStopRingToOpen(false);
  } else if (mySettings.ringAutomation &&
             lastOpenDoorTime > 0 &&
             millis() - lastOpenDoorTime < 60000) {
    client.publish(T("RingToOpenOpened"), "ON");
    writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);
  } else {
    dbgMqttPublished++;
    client.publish(T("EntryDoor"), "ON");
  }
}

// ===========================================================================
// Web server handlers
// ===========================================================================

void handleRoot() {
  webServer.send_P(200, "text/html", WEB_UI_HTML);
}

void handleStatus() {
  int rssi = WiFi.RSSI();
  const char* quality = "";
  if (WiFi.status() == WL_CONNECTED) {
    if      (rssi >= -60) quality = "Excellent";
    else if (rssi >= -70) quality = "Good";
    else if (rssi >= -80) quality = "Ok";
    else                  quality = "Poor";
  }
  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"rssi\":%d,"
    "\"wifi_quality\":\"%s\",\"mqtt\":%s,"
    "\"temp_c\":%.1f,"
    "\"free_heap\":%u,\"heap_total\":%u,"
    "\"flash_free\":%u,\"flash_total\":%u,"
    "\"dbg_isr\":%u,\"dbg_decode_ok\":%u,"
    "\"dbg_addr_mismatch\":%u,\"dbg_checksum_fail\":%u,"
    "\"dbg_mqtt_published\":%u,\"dbg_last_code\":%u,\"dbg_last_addr\":%u}",
    FIRMWARE_VERSION,
    millis() / 1000UL,
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    rssi,
    quality,
    client.connected() ? "true" : "false",
    temperatureRead(),
    ESP.getFreeHeap(),
    ESP.getHeapSize(),
    ESP.getFreeSketchSpace(),
    ESP.getFlashChipSize(),
    dbgIsrCount, dbgDecodeOk,
    dbgAddrMismatch, dbgChecksumFail,
    dbgMqttPublished, dbgLastCode, dbgLastAddr
  );
  webServer.send(200, "application/json", buf);
}

void handleGetConfig() {
  char buf[380];
  snprintf(buf, sizeof(buf),
    "{\"mqtt_host\":\"%s\",\"mqtt_user\":\"%s\",\"mqtt_prefix\":\"%s\","
    "\"address\":%d,\"gain\":%d,\"threshold\":%d,"
    "\"rto_minutes\":%d,"
    "\"wifi_ssid\":\"%s\"}",
    mySettings.mqttServer,
    mySettings.mqttUser,
    mySettings.mqttPrefix,
    mySettings.intercomAddress,
    mySettings.gain,
    mySettings.compVoltLvl,
    mySettings.ringToOpenMinutes,
    WiFi.SSID().c_str()
  );
  webServer.send(200, "application/json", buf);
}

void handlePostConfig() {
  bool mqttChanged   = false;
  bool prefixChanged = false;

  if (webServer.hasArg("mqtt_host")) {
    String h = webServer.arg("mqtt_host");
    if (h.length() > 0 && h != String(mySettings.mqttServer)) {
      strncpy(mySettings.mqttServer, h.c_str(), sizeof(mySettings.mqttServer) - 1);
      mqttChanged = true;
    }
  }
  if (webServer.hasArg("mqtt_user")) {
    strncpy(mySettings.mqttUser, webServer.arg("mqtt_user").c_str(), sizeof(mySettings.mqttUser) - 1);
    mqttChanged = true;
  }
  if (webServer.hasArg("mqtt_pass") && webServer.arg("mqtt_pass").length() > 0) {
    strncpy(mySettings.mqttPassword, webServer.arg("mqtt_pass").c_str(), sizeof(mySettings.mqttPassword) - 1);
    mqttChanged = true;
  }
  if (webServer.hasArg("mqtt_prefix")) {
    String p = webServer.arg("mqtt_prefix");
    if (p.length() > 0 && p.length() < 32 && p != String(mySettings.mqttPrefix)) {
      strncpy(mySettings.mqttPrefix, p.c_str(), sizeof(mySettings.mqttPrefix) - 1);
      prefixChanged = true;
    }
  }

  if (webServer.hasArg("update_device")) {
    if (webServer.hasArg("address")) {
      mySettings.intercomAddress = constrain(webServer.arg("address").toInt(), 0, 255);
    }
    if (webServer.hasArg("gain")) {
      int g = constrain(webServer.arg("gain").toInt(), 2, 40);
      mySettings.gain = g;
      setOPVGain(g);
    }
    if (webServer.hasArg("threshold")) {
      int t = constrain(webServer.arg("threshold").toInt(), 100, 1500);
      mySettings.compVoltLvl = t;
      setComparatorVoltageLimit(t);
    }
    if (webServer.hasArg("rto_minutes")) {
      int m = constrain(webServer.arg("rto_minutes").toInt(), 1, 1440);
      mySettings.ringToOpenMinutes = m;
      ringToOpenMillis = (unsigned int)m * 60u * 1000u;
      char rtoVal[8];
      snprintf(rtoVal, sizeof(rtoVal), "%d", m);
      client.publish(T("RingToOpenTime"), rtoVal, true);
    }
  }

  saveSettings();

  if (mqttChanged && !prefixChanged) {
    client.disconnect();
  }

  char resp[64];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"restart_required\":%s}",
           prefixChanged ? "true" : "false");
  webServer.send(200, "application/json", resp);
}

void handlePostWifi() {
  String ssid = webServer.arg("ssid");
  if (ssid.length() == 0) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }
  webServer.send(200, "application/json", "{\"ok\":true}");
  delay(100);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), webServer.arg("password").c_str());
  delay(200);
  safeRestart();
}

void handlePostOta() {
  String url = webServer.arg("url");
  if (url.length() < 8 || !url.startsWith("http")) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid url\"}");
    return;
  }
  webServer.send(200, "application/json", "{\"ok\":true}");
  otaUrl     = url;
  otaPending = true;
}

void handleOtaUploadFile() {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      webServer.send(200, "application/json", "{\"ok\":true}");
      delay(200);
      safeRestart();
    } else {
      webServer.send(500, "application/json", "{\"ok\":false,\"error\":\"flash failed\"}");
    }
  }
}

void handlePostRestart() {
  webServer.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  safeRestart();
}

void handlePostFactoryReset() {
  webServer.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  applyDefaults();
  saveSettings();
  WiFi.disconnect(true, true);
  { WiFiManager wm; wm.resetSettings(); }
  delay(500);
  safeRestart();
}

// ===========================================================================
// WiFiManager / config
// ===========================================================================

void setup_wifi(bool autoconnect = true) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(10);
  WiFiManager wm;
  wm.setBreakAfterConfig(true);
  wm.setConfigPortalTimeout(120);
  wm.setConnectTimeout(60);
  std::vector<const char*> menu = {"wifi", "sep", "exit"};
  wm.setMenu(menu);
  if (autoconnect) wm.autoConnect("Simplebus2-Bridge");
  else             wm.startConfigPortal("Simplebus2-Bridge");
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
}

// ===========================================================================
// MQTT
// ===========================================================================
void callback(char* topic, byte* message, unsigned int length) {
  String t(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)message[i];

  if (t == T("OpenDoor") && msg == "ON") {
    writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);

  } else if (t == T("RingToOpen")) {
    if (isNumeric(msg)) {
      int m = msg.toInt();
      ringToOpenMillis = constrain(m, 1, 1440) * 60u * 1000u;
      startStopRingToOpen(true);
      ringToOpenMillisStart = millis();
    } else if (msg == "ON") {
      startStopRingToOpen(true);
      ringToOpenMillisStart = millis();
    } else if (msg == "OFF") {
      startStopRingToOpen(false);
    }

  } else if (t == T("SetRingToOpenTime") && isNumeric(msg)) {
    int m = constrain(msg.toInt(), 1, 1440);
    mySettings.ringToOpenMinutes = m;
    ringToOpenMillis = (unsigned int)m * 60u * 1000u;
    saveSettings();
    char rtoVal[8];
    snprintf(rtoVal, sizeof(rtoVal), "%d", m);
    client.publish(T("RingToOpenTime"), rtoVal, true);

  } else if (t == T("SetComparatorVoltage") && isNumeric(msg)) {
    int v = msg.toInt();
    if (v >= 100 && v < 1500) {
      mySettings.compVoltLvl = v;
      saveSettings();
      setComparatorVoltageLimit(v);
    }

  } else if (t == T("SetGain") && isNumeric(msg)) {
    int g = msg.toInt();
    if (g >= 2 && g <= 40) {
      mySettings.gain = g;
      saveSettings();
      setOPVGain(g);
    }

  } else if (t == T("StartConfigMode") && msg == "ON") {
    digitalWrite(LEDPIN, HIGH);
    webServer.stop();
    setup_wifi(false);
    MDNS.end();
    MDNS.begin("Simplebus2-Bridge");
    MDNS.addService("http", "tcp", 80);
    webServer.begin();
    digitalWrite(LEDPIN, LOW);
    setOPVGain(mySettings.gain);
    setComparatorVoltageLimit(mySettings.compVoltLvl);

  } else if (t == T("StartTeachIn")) {
    if (msg == "ON") {
      teachInActive        = true;
      teachInTimeActivated = millis();
      for (int i = 0; i < 3; i++) {
        digitalWrite(LEDPIN, HIGH); delay(500);
        digitalWrite(LEDPIN, LOW);  delay(500);
      }
      digitalWrite(LEDPIN, HIGH);
    } else if (msg == "OFF") {
      teachInActive = false;
      digitalWrite(LEDPIN, LOW);
    }

  } else if (t == T("SetAddress") && isNumeric(msg)) {
    int a = msg.toInt();
    if (a >= 0 && a <= 255) {
      mySettings.intercomAddress = a;
      saveSettings();
    }

  } else if (t == T("Restart") && msg == "ON") {
    safeRestart();

  } else if (t == T("OtaUpdate") && msg.length() > 7) {
    otaUrl     = msg;
    otaPending = true;
    client.publish(T("OtaStatus"), "pending");
  }
}

void publishDiscovery() {
  char nid[13];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(nid, sizeof(nid), "%02x%02x%02x%02x%02x%02x",
           (uint8_t)mac,       (uint8_t)(mac >> 8),  (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24),(uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

  // No avty_t: entities are always visible in HA; no LWT is set.
  // Stale avty_t from older firmware is cleared by HA on next restart automatically.
  char dev[160];
  snprintf(dev, sizeof(dev),
    "\"device\":{"
      "\"ids\":[\"simplebus2_%s\"],"
      "\"name\":\"Simplebus2 Bridge\","
      "\"mdl\":\"ESP32-C3\","
      "\"mf\":\"Open Source\""
    "}", nid);

  char buf[640];
  char topic[96];

  // Remove obsolete entities
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/number/simplebus2_%s/gain/config", nid);
  client.publish(topic, "", true);

  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/number/simplebus2_%s/compvolt/config", nid);
  client.publish(topic, "", true);

  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/binary_sensor/simplebus2_%s/ringtoopenst/config", nid);
  client.publish(topic, "", true);

  // EntryDoor (outdoor doorbell)
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/binary_sensor/simplebus2_%s/entrydoor/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Entry Door\","
    "\"uniq_id\":\"simplebus2_%s_entrydoor\","
    "\"ic\":\"mdi:doorbell\","
    "\"stat_t\":\"%s/EntryDoor\","
    "\"pl_on\":\"ON\",\"off_dly\":2,%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // FloorDoor (indoor / floor doorbell)
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/binary_sensor/simplebus2_%s/floordoor/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Floor Door\","
    "\"uniq_id\":\"simplebus2_%s_floordoor\","
    "\"ic\":\"mdi:doorbell\","
    "\"stat_t\":\"%s/FloorDoor\","
    "\"pl_on\":\"ON\",\"off_dly\":2,%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // Heartbeat
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/binary_sensor/simplebus2_%s/heartbeat/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Heartbeat\","
    "\"uniq_id\":\"simplebus2_%s_heartbeat\","
    "\"dev_cla\":\"connectivity\","
    "\"stat_t\":\"%s/Heartbeat\","
    "\"pl_on\":\"ON\",\"off_dly\":360,%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // RingToOpenOpened
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/binary_sensor/simplebus2_%s/ringtoopenopened/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Ring To Open Opened\","
    "\"uniq_id\":\"simplebus2_%s_ringtoopenopened\","
    "\"stat_t\":\"%s/RingToOpenOpened\","
    "\"pl_on\":\"ON\",\"off_dly\":5,"
    "\"ic\":\"mdi:door-open\",%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // Open Door button
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/button/simplebus2_%s/opendoor/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Open Door\","
    "\"uniq_id\":\"simplebus2_%s_opendoor\","
    "\"cmd_t\":\"%s/OpenDoor\","
    "\"pl_prs\":\"ON\","
    "\"ic\":\"mdi:door-open\",%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // Start Config Mode button
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/button/simplebus2_%s/startconfigmode/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Start Config Mode\","
    "\"uniq_id\":\"simplebus2_%s_startconfigmode\","
    "\"cmd_t\":\"%s/StartConfigMode\","
    "\"pl_prs\":\"ON\","
    "\"ic\":\"mdi:cog-outline\",%s}", nid, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // RingToOpen switch
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/switch/simplebus2_%s/ringtoopen/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Ring To Open\","
    "\"uniq_id\":\"simplebus2_%s_ringtoopen\","
    "\"cmd_t\":\"%s/RingToOpen\","
    "\"stat_t\":\"%s/RingToOpenStatus\","
    "\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
    "\"ic\":\"mdi:refresh-auto\",%s}", nid, mySettings.mqttPrefix, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // Ring-to-Open duration number
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/number/simplebus2_%s/ringtoopentime/config", nid);
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Ring To Open Duration\","
    "\"uniq_id\":\"simplebus2_%s_ringtoopentime\","
    "\"cmd_t\":\"%s/SetRingToOpenTime\","
    "\"stat_t\":\"%s/RingToOpenTime\","
    "\"min\":1,\"max\":1440,\"step\":1,"
    "\"unit_of_measurement\":\"min\","
    "\"ic\":\"mdi:timer-outline\",%s}", nid, mySettings.mqttPrefix, mySettings.mqttPrefix, dev);
  client.publish(topic, buf, true);

  // OTA Update URL — remove from HA
  snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/text/simplebus2_%s/otaurl/config", nid);
  client.publish(topic, "", true);
}

void subscribeAll() {
  client.subscribe(T("OpenDoor"));
  client.subscribe(T("RingToOpen"));
  client.subscribe(T("SetRingToOpenTime"));
  client.subscribe(T("SetComparatorVoltage"));
  client.subscribe(T("SetGain"));
  client.subscribe(T("Restart"));
  client.subscribe(T("StartTeachIn"));
  client.subscribe(T("StartConfigMode"));
  client.subscribe(T("OtaUpdate"));
  client.subscribe(T("SetAddress"));
}

void handleOtaUpdate() {
  if (!otaPending) return;
  otaPending = false;

  Serial.printf("OTA from: %s\n", otaUrl.c_str());
  client.publish(T("OtaStatus"), "downloading");
  client.loop();

  detachInterrupt(INTERCOM_READ_PIN);

  WiFiClient cl;
  httpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return ret = httpUpdate.update(cl, otaUrl);

  attachInterrupt(INTERCOM_READ_PIN, function_ISR, CHANGE);

  switch (ret) {
    case HTTP_UPDATE_OK:
      client.publish(T("OtaStatus"), "ok_rebooting");
      client.loop();
      delay(500);
      safeRestart();
      break;
    case HTTP_UPDATE_FAILED:
      client.publish(T("OtaStatus"), ("failed: " + String(httpUpdate.getLastErrorString())).c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      client.publish(T("OtaStatus"), "no_update_needed");
      break;
  }
}

void reconnect() {
  if (client.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  client.setServer(mySettings.mqttServer, 1883);
  if (client.connect("ESP32Doorbell", mySettings.mqttUser, mySettings.mqttPassword)) {
    Serial.println("connected to MQTT");

    client.loop();
    delay(50);

    // 2. Stale retained RingToOpen Command löschen
    client.publish(T("RingToOpen"), (const uint8_t*)nullptr, 0, true);

    // 3. Subscriptions
    subscribeAll();

    // 4. Discovery
    publishDiscovery();

    // 5. Aktuellen State publishen
    char addrBuf[8];
    snprintf(addrBuf, sizeof(addrBuf), "%d", mySettings.intercomAddress);
    client.publish(T("Address"), addrBuf, true);
    client.publish(T("RingToOpenStatus"), ringToOpenStarted ? "ON" : "OFF", true);
    char rtoVal[8];
    snprintf(rtoVal, sizeof(rtoVal), "%d", mySettings.ringToOpenMinutes);
    client.publish(T("RingToOpenTime"), rtoVal, true);
    client.publish(T("status"), "subscribed");

    // 6. Reboot-Grund beim ersten Connect nach Power-On
    static bool firstConnect = true;
    if (firstConnect) {
      firstConnect = false;
      char rebootMsg[32];
      snprintf(rebootMsg, sizeof(rebootMsg), "ON rst=0x%x", (unsigned)esp_reset_reason());
      client.publish(T("Reboot"),    rebootMsg);
      client.publish(T("Heartbeat"), "ON");
    }
  } else {
    Serial.printf("MQTT failed rc=%d, retry in 5s\n", client.state());
  }
}

// ===========================================================================
// setup()
// ===========================================================================
void setup() {
  digitalWrite(LEDPIN, HIGH);
  EEPROM.begin(500);
  Serial.begin(115200);
  delay(1000);
  loadSettings();

  setCpuFrequencyMhz(80);

  pinMode(INTERCOM_READ_PIN,  INPUT_PULLUP);
  pinMode(INTERCOM_WRITE_PIN, OUTPUT);
  pinMode(LEDPIN,             OUTPUT);
  pinMode(BUTTONPIN,          INPUT_PULLUP);

  ledcSetup(0, 25000, 8);
  ledcAttachPin(INTERCOM_WRITE_PIN, 0);

  attachInterrupt(INTERCOM_READ_PIN, function_ISR, CHANGE);

  if (!structInitialized()) {
    applyDefaults();
    saveSettings();
    setup_wifi(false);
  } else {
    setup_wifi();
  }

  {
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      delay(100);
    }
  }

  MDNS.begin("Simplebus2-Bridge");
  MDNS.addService("http", "tcp", 80);

  webServer.on("/",                    HTTP_GET,  handleRoot);
  webServer.on("/api/status",          HTTP_GET,  handleStatus);
  webServer.on("/api/config",          HTTP_GET,  handleGetConfig);
  webServer.on("/api/config",          HTTP_POST, handlePostConfig);
  webServer.on("/api/wifi",            HTTP_POST, handlePostWifi);
  webServer.on("/api/ota",             HTTP_POST, handlePostOta);
  webServer.on("/api/ota-upload",      HTTP_POST, []() {}, handleOtaUploadFile);
  webServer.on("/api/restart",         HTTP_POST, handlePostRestart);
  webServer.on("/api/factory-reset",   HTTP_POST, handlePostFactoryReset);
  webServer.begin();

  client.setServer(mySettings.mqttServer, 1883);
  client.setCallback(callback);
  client.setBufferSize(512);
  if (strlen(mySettings.mqttServer) > 0) {
    reconnect();
    client.loop();
  }

  setOPVGain(mySettings.gain);
  setComparatorVoltageLimit(mySettings.compVoltLvl);

  lastHeartbeatMillis = millis();
  lastState   = Button.read();
  pressedTime = millis();
  releasedTime = millis();
  digitalWrite(LEDPIN, LOW);
}

// ===========================================================================
// loop()
// ===========================================================================
void loop() {
  if (millis() - lastHeartbeatMillis >= 5ul * 60ul * 1000ul) {
    client.publish(T("Heartbeat"), "ON");
    lastHeartbeatMillis = millis();
  }

  currentState = Button.read();
  if (teachInActive && millis() - teachInTimeActivated > 180000) {
    teachInActive = false;
    digitalWrite(LEDPIN, LOW);
  }
  if (lastState == HIGH && currentState == LOW) {
    pressedTime = millis();
  } else if (lastState == LOW && currentState == HIGH) {
    releasedTime = millis();
    long dur = releasedTime - pressedTime;
    if (dur > 50 && dur < 1500) {
      teachInActive = !teachInActive;
      teachInTimeActivated = millis();
      if (teachInActive) {
        for (int i = 0; i < 3; i++) {
          digitalWrite(LEDPIN, HIGH); delay(500);
          digitalWrite(LEDPIN, LOW);  delay(500);
        }
        digitalWrite(LEDPIN, HIGH);
      } else {
        digitalWrite(LEDPIN, LOW);
      }
    } else if (dur >= 4000) {
      webServer.stop();
      setup_wifi(false);
      MDNS.end();
      MDNS.begin("Simplebus2-Bridge");
      MDNS.addService("http", "tcp", 80);
      webServer.begin();
      setOPVGain(mySettings.gain);
      setComparatorVoltageLimit(mySettings.compVoltLvl);
      digitalWrite(LEDPIN, LOW);
      if (!client.connected()) reconnect();
    }
    pressedTime = 0;
  }
  if (currentState == LOW && pressedTime > 0 && !teachInActive &&
      (millis() - pressedTime) >= 4000) {
    digitalWrite(LEDPIN, HIGH);
  }
  lastState = currentState;

  if (strlen(mySettings.mqttServer) > 0 && !client.connected()) reconnect();
  client.loop();

  webServer.handleClient();

  if (ringToOpenStarted && millis() - ringToOpenMillisStart > ringToOpenMillis) {
    startStopRingToOpen(false);
  }

  consumeIsrBuffer();

  if (messageOk) {
    processMessage(messageCode);
    messageOk = false;
  }

  handleDoorbellProcessing();

  handleOtaUpdate();
}
