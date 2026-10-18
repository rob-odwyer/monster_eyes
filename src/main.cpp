// Monster Eyes on ESP32-2432S028R (Cheap Yellow Display)
// One ILI9341 eye via esp_lcd, with MQTT control of open/closed.
//
// MQTT topics (see TOPIC_* below):
//   monster_eyes/eye/set    <- "open" | "closed"  (also on/off, 1/0, true/false)
//   monster_eyes/eye/state  -> "open" | "closed"  (retained)
//   monster_eyes/status     -> "online" | "offline" (retained, LWT)
#include <Arduino.h>
#include <Adafruit_Monster_Eyes.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <driver/spi_master.h>

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

#define MQTT_CLIENT_ID  "monster-eyes-cyd"
#define TOPIC_SET       "monster_eyes/eye/set"
#define TOPIC_STATE     "monster_eyes/eye/state"
#define TOPIC_STATUS    "monster_eyes/status"

// How fast the lids travel on an MQTT command, in blink-phase units per
// second. 4.0 means a full close takes 250 ms.
#define LID_SPEED 4.0f

Eyes_ESPLCD display(EYES_ESPLCD_ILI9341, TFT_W, TFT_H, TFT_SCK, TFT_MOSI,
                    TFT_CS, TFT_DC, TFT_RST);
Adafruit_Monster_Eyes eyes(&display);

WiFiClient net;
PubSubClient mqtt(net);

static bool eyeClosed = false;   // Commanded state
static float lidPhase = 0.0f;    // 0 open .. 1 shut, what we are showing now
static bool lidHeld = false;     // true while we own the lids via setBlink()

// ---------------------------------------------------------------------------
//  MQTT
// ---------------------------------------------------------------------------

static void publishState(void) {
  if (mqtt.connected())
    mqtt.publish(TOPIC_STATE, eyeClosed ? "closed" : "open", true);
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
    mqtt.subscribe(TOPIC_SET);
    publishState();
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

  // Bring Wi-Fi up BEFORE eyes.begin(): the library sizes the eye to the RAM
  // that is free at that moment, and the Wi-Fi stack takes a chunk of it.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Lower latency; the display dominates power anyway
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Wi-Fi: connecting to %s\n", WIFI_SSID);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);

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
}

void loop() {
  lidService();
  eyes.animate();
  mqttService();

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
  }
}
