// Monster Eyes on ESP32-2432S028R (Cheap Yellow Display)
// One ILI9341 eye via esp_lcd, with MQTT control of open/closed.
//
// Build roles (platformio.ini environments):
//   cyd            standalone eye
//   cyd_primary    animates, takes MQTT commands, streams state over I2C
//   cyd_secondary  renders whatever the primary sends; free-runs if the
//                  link goes quiet
// I2C link on the CN1 connector: IO27 = SDA, IO22 = SCL, plus GND.
//
// MQTT topics (see TOPIC_* below):
//   monster_eyes/eye/set    <- "open" | "closed"  (also on/off, 1/0, true/false)
//   monster_eyes/eye/state  -> "open" | "closed"  (retained)
//   monster_eyes/status     -> "online" | "offline" (retained, LWT)
// The secondary reports monster_eyes/secondary/status and
// monster_eyes/secondary/sync ("locked" | "free"); commands go to the
// primary, and the lid position reaches the secondary over I2C.
//
// Home Assistant MQTT discovery: each board publishes one device-discovery
// message to homeassistant/device/<id>/config on connect, and again whenever
// Home Assistant announces "online" on homeassistant/status.
#include <Arduino.h>
#include <Adafruit_Monster_Eyes.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <esp_random.h>
#include <WiFi.h>
#include <driver/spi_master.h>
#if defined(EYES_ROLE_PRIMARY) || defined(EYES_ROLE_SECONDARY)
#include <Eyes_Sync.h>
#include <Wire.h>
#endif
#if defined(EYES_ROLE_PRIMARY) && defined(EYES_ROLE_SECONDARY)
#error "Pick one role"
#endif

#include "secrets.h"

// CYD display: ILI9341 on HSPI
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1   // tied to EN
#define TFT_BL   21
#define TFT_W    320  // landscape: axes swapped in MADCTL below
#define TFT_H    240

// I2C sync link (CN1 connector)
#define SYNC_SDA  27
#define SYNC_SCL  22
#ifndef SYNC_HZ
#define SYNC_HZ   100000  // 400 kHz NACKs on CN1 with only internal pull-ups
#endif
#define SYNC_ADDR 0x42

// Eye size in pixels. 0 = as large as RAM allows. Both boards must end up
// with the same size or the secondary's gaze is scaled wrongly; the startup
// log prints the size each board chose, and setting this pins it.
#ifndef EYE_SIZE
#define EYE_SIZE 0
#endif

#if defined(EYES_ROLE_SECONDARY)
#define MQTT_CLIENT_ID  "monster-eyes-cyd-secondary"
#define TOPIC_STATUS    "monster_eyes/secondary/status"
#define ROLE_NAME       "secondary"
#else
#define MQTT_CLIENT_ID  "monster-eyes-cyd"
#define TOPIC_STATUS    "monster_eyes/status"
#if defined(EYES_ROLE_PRIMARY)
#define ROLE_NAME       "primary"
#else
#define ROLE_NAME       "standalone"
#endif
#endif
#define TOPIC_SET       "monster_eyes/eye/set"
#define TOPIC_STATE     "monster_eyes/eye/state"
#define TOPIC_SYNC      "monster_eyes/secondary/sync"
#define TOPIC_HA_STATUS "homeassistant/status"
#if defined(EYES_ROLE_SECONDARY)
#define HA_DEVICE_ID    "monster_eyes_secondary"
#else
#define HA_DEVICE_ID    "monster_eyes_primary"
#endif
#define HA_DISCOVERY_TOPIC "homeassistant/device/" HA_DEVICE_ID "/config"
#define FW_VERSION      "1.1.0"

// How fast the lids travel on an MQTT command, in blink-phase units per
// second. 4.0 means a full close takes 250 ms.
#define LID_SPEED 4.0f

Eyes_ESPLCD display(EYES_ESPLCD_ILI9341, TFT_W, TFT_H, TFT_SCK, TFT_MOSI,
                    TFT_CS, TFT_DC, TFT_RST);
Adafruit_Monster_Eyes eyes(&display);

// The primary draws the character's RIGHT eye and the secondary the LEFT,
// which also selects the "right"/"left" blocks in config.eye. Facing the
// character, the primary is the board on YOUR left.
#if defined(EYES_ROLE_PRIMARY)
Eyes_SyncPrimary eyeSync(eyes, Wire, SYNC_ADDR);
#elif defined(EYES_ROLE_SECONDARY)
Eyes_SyncSecondary eyeSync(eyes, Wire, SYNC_ADDR);
#endif

WiFiClient net;
PubSubClient mqtt(net);

static bool eyeClosed = false;   // Commanded state
static float lidPhase = 0.0f;    // 0 open .. 1 shut, what we are showing now
static bool lidHeld = false;     // true while we own the lids via setBlink()

// ---------------------------------------------------------------------------
//  MQTT
// ---------------------------------------------------------------------------

// Discovery is sent a short random delay after connecting or after Home
// Assistant's birth message, and states follow a second later so they land
// after Home Assistant has created the entities. 0 = nothing pending.
static uint32_t discoveryDueMs = 0;
static uint32_t statesDueMs = 0;

static void scheduleDiscovery(uint32_t maxDelayMs) {
  discoveryDueMs = millis() + 100 + (esp_random() % (maxDelayMs + 1));
  if (!discoveryDueMs)
    discoveryDueMs = 1;
}

static void publishState(void) {
#if defined(EYES_ROLE_SECONDARY)
  return; // The primary owns the eye state
#endif
  if (mqtt.connected())
    mqtt.publish(TOPIC_STATE, eyeClosed ? "closed" : "open", true);
}

#if defined(EYES_ROLE_SECONDARY)
static bool syncLocked = false;
static void publishSync(void) {
  if (mqtt.connected())
    mqtt.publish(TOPIC_SYNC, syncLocked ? "locked" : "free", true);
}
#endif

static void publishStates(void) {
  mqtt.publish(TOPIC_STATUS, "online", true);
#if defined(EYES_ROLE_SECONDARY)
  publishSync();
#else
  publishState();
#endif
}

// One device-discovery message per board. Keys use Home Assistant's
// abbreviations to keep the payload well under the MQTT buffer.
static void publishDiscovery(void) {
  JsonDocument doc;
  JsonObject dev = doc["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add(HA_DEVICE_ID);
  dev["mf"] = "Adafruit Monster Eyes";
  dev["mdl"] = "ESP32-2432S028R";
  dev["sw"] = FW_VERSION;
  JsonObject o = doc["o"].to<JsonObject>();
  o["name"] = "monster-eyes-cyd";
  o["sw"] = FW_VERSION;
  JsonObject cmps = doc["cmps"].to<JsonObject>();

#if defined(EYES_ROLE_SECONDARY)
  dev["name"] = "Monster eyes secondary";
  dev["via_dev"] = "monster_eyes_primary";

  // Online: no availability, so it reads "off" (not "unavailable") when the
  // last-will message fires.
  JsonObject on = cmps["online"].to<JsonObject>();
  on["p"] = "binary_sensor";
  on["uniq_id"] = "monster_eyes_secondary_online";
  on["name"] = "Online";
  on["stat_t"] = TOPIC_STATUS;
  on["pl_on"] = "online";
  on["pl_off"] = "offline";
  on["dev_cla"] = "connectivity";
  on["ent_cat"] = "diagnostic";

  JsonObject sy = cmps["sync"].to<JsonObject>();
  sy["p"] = "binary_sensor";
  sy["uniq_id"] = "monster_eyes_secondary_sync";
  sy["name"] = "Sync locked";
  sy["stat_t"] = TOPIC_SYNC;
  sy["pl_on"] = "locked";
  sy["pl_off"] = "free";
  sy["ic"] = "mdi:link-variant";
  sy["ent_cat"] = "diagnostic";
  sy["avty_t"] = TOPIC_STATUS;
#else
  dev["name"] = "Monster eyes";
  doc["avty_t"] = TOPIC_STATUS; // Whole device greys out when offline

  JsonObject sw = cmps["eyes_open"].to<JsonObject>();
  sw["p"] = "switch";
  sw["uniq_id"] = "monster_eyes_open";
  sw["name"] = "Open";
  sw["cmd_t"] = TOPIC_SET;
  sw["stat_t"] = TOPIC_STATE;
  sw["pl_on"] = "open";
  sw["pl_off"] = "closed";
  sw["stat_on"] = "open";
  sw["stat_off"] = "closed";
  sw["ic"] = "mdi:eye";
#endif

  static char buf[900];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf) - 1) {
    Serial.println("MQTT: discovery payload too large");
    return;
  }
  // Not retained: Home Assistant recommends resending on its birth message
  // instead, which avoids ghost entities if this device is retired.
  const bool ok = mqtt.publish(HA_DISCOVERY_TOPIC, (const uint8_t *)buf, n, false);
  Serial.printf("MQTT: discovery %s (%u bytes)\n", ok ? "sent" : "FAILED",
                (unsigned)n);
}

static bool parseClosed(const char *s, bool *closed) {
  if (!strcasecmp(s, "closed") || !strcasecmp(s, "close") ||
      !strcasecmp(s, "off") || !strcmp(s, "0") || !strcasecmp(s, "false")) {
    *closed = true;
    return true;
  }
  if (!strcasecmp(s, "open") || !strcasecmp(s, "on") || !strcmp(s, "1") ||
      !strcasecmp(s, "true")) {
    *closed = false;
    return true;
  }
  return false;
}

static void onMessage(char *topic, byte *payload, unsigned int len) {
  char buf[16];
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = 0;
  // Trim whitespace/newlines some MQTT clients append
  while (len && isspace((unsigned char)buf[len - 1]))
    buf[--len] = 0;

  if (!strcmp(topic, TOPIC_HA_STATUS)) {
    if (!strcmp(buf, "online")) {
      Serial.println("MQTT: Home Assistant online, rediscovering");
      scheduleDiscovery(3000);
    }
    return;
  }

  bool closed;
  if (!strcmp(topic, TOPIC_SET) && parseClosed(buf, &closed)) {
    eyeClosed = closed;
    Serial.printf("MQTT: eye %s\n", closed ? "closed" : "open");
    publishState();
  } else {
    Serial.printf("MQTT: ignored '%s' on %s\n", buf, topic);
  }
}

// Non-blocking: called every loop, attempts a connect at most every 5 s.
static void mqttService(void) {
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (mqtt.connected()) {
    mqtt.loop();
    const uint32_t now = millis();
    if (discoveryDueMs && (int32_t)(now - discoveryDueMs) >= 0) {
      discoveryDueMs = 0;
      publishDiscovery();
      statesDueMs = now + 1000;
      if (!statesDueMs)
        statesDueMs = 1;
    }
    if (statesDueMs && (int32_t)(now - statesDueMs) >= 0) {
      statesDueMs = 0;
      publishStates();
    }
    return;
  }
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 5000)
    return;
  lastTry = millis();
  Serial.printf("MQTT: connecting to %s:%d... ", MQTT_HOST, MQTT_PORT);
  const char *user = MQTT_USER[0] ? MQTT_USER : NULL;
  const char *pass = MQTT_USER[0] ? MQTT_PASS : NULL;
  if (mqtt.connect(MQTT_CLIENT_ID, user, pass, TOPIC_STATUS, 0, true,
                   "offline")) {
    Serial.println("ok");
    mqtt.publish(TOPIC_STATUS, "online", true);
#if !defined(EYES_ROLE_SECONDARY)
    mqtt.subscribe(TOPIC_SET);
    publishState();
#endif
    mqtt.subscribe(TOPIC_HA_STATUS);
    scheduleDiscovery(1000);
  } else {
    Serial.printf("failed, rc=%d\n", mqtt.state());
  }
}

// ---------------------------------------------------------------------------
//  LIDS
// ---------------------------------------------------------------------------

// Ease the lids toward the commanded state. While closing or closed we own
// the lids; once fully open again the library's own blinking takes over.
static void lidService(void) {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const float dt = lastMs ? (now - lastMs) / 1000.0f : 0.0f;
  lastMs = now;

  const float target = eyeClosed ? 1.0f : 0.0f;
  if (lidPhase == target) {
    if (!eyeClosed && lidHeld) {
      eyes.releaseBlink();
      lidHeld = false;
    }
    return;
  }
  const float step = LID_SPEED * dt;
  if (lidPhase < target)
    lidPhase = (lidPhase + step > target) ? target : lidPhase + step;
  else
    lidPhase = (lidPhase - step < target) ? target : lidPhase - step;
  eyes.setBlink(lidPhase);
  lidHeld = true;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  eyes.setVerbose(Serial);
  Serial.printf("Role: %s\n", ROLE_NAME);
  if (EYE_SIZE > 0)
    eyes.setEyeSize(EYE_SIZE);

  // Bring Wi-Fi up BEFORE eyes.begin(): the library sizes the eye to the RAM
  // that is free at that moment, and the Wi-Fi stack takes a chunk of it.
  // The secondary joins Wi-Fi too, so both boards size the eye from the same
  // free RAM and report status over MQTT.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Lower latency; the display dominates power anyway
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Wi-Fi: connecting to %s\n", WIFI_SSID);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(1024); // Default 256 is too small for discovery

  display.setSPIHost(SPI2_HOST);          // HSPI on classic ESP32
  display.setSPISpeed(40000000);
  // invert=false (ILI9341), swapXY=true for landscape. If the eye is upside
  // down for how you mount the board, set the last two to true, true.
  display.setOrientation(false, true /* swapXY */, false, false);
  display.setBacklightPin(TFT_BL);
  if (!eyes.begin()) {
    Serial.print("Monster Eyes failed: ");
    Serial.println(eyes.errorString());
    while (1) delay(1000);
  }
  Serial.printf("Eye size: %d px (%s)\n", eyes.config().displaySize, ROLE_NAME);

#if defined(EYES_ROLE_PRIMARY) || defined(EYES_ROLE_SECONDARY)
  // Wire defaults to SDA = IO21, the backlight. Point it at CN1 first; the
  // sync classes call Wire.begin() without pins.
  Wire.setPins(SYNC_SDA, SYNC_SCL);
#if defined(EYES_ROLE_PRIMARY)
  eyeSync.begin(SYNC_HZ);
#else
  eyeSync.begin();
#endif
#endif
}

void loop() {
#if defined(EYES_ROLE_SECONDARY)
  eyeSync.poll(); // Owns gaze, pupil and lids while packets arrive
#else
  lidService();
#endif
  eyes.animate();
#if defined(EYES_ROLE_PRIMARY)
  eyeSync.send();
#endif
  mqttService();
#if defined(EYES_ROLE_SECONDARY)
  if (eyeSync.live() != syncLocked) {
    syncLocked = eyeSync.live();
    publishSync();
  }
#endif

  static uint32_t last = 0;
  static bool wasConnected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected)
      Serial.printf("Wi-Fi: connected, IP %s\n", WiFi.localIP().toString().c_str());
    else
      Serial.println("Wi-Fi: disconnected");
  }
  if (millis() - last >= 5000) {
    last = millis();
    Serial.printf("%.0f fps, largest block %u, wifi %s, mqtt %s, eye %s\n",
                  eyes.frameRate(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  connected ? "up" : "down", mqtt.connected() ? "up" : "down",
                  eyeClosed ? "closed" : "open");
#if defined(EYES_ROLE_PRIMARY)
    Serial.printf("sync: sent %lu, failed %lu\n", (unsigned long)eyeSync.sent(),
                  (unsigned long)eyeSync.failed());
    eyeSync.resetCounts();
#elif defined(EYES_ROLE_SECONDARY)
    eyeSync.report(Serial);
    eyeSync.resetCounts();
#endif
  }
}
