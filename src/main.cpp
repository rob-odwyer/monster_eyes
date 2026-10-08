// Monster Eyes on ESP32-2432S028R (Cheap Yellow Display) -- compile test
#include <Arduino.h>
#include <Adafruit_Monster_Eyes.h>
#include <driver/spi_master.h>

// CYD display: ILI9341 on HSPI
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1   // tied to EN
#define TFT_BL   21
#define TFT_W    320  // landscape: axes swapped in MADCTL below
#define TFT_H    240

Eyes_ESPLCD display(EYES_ESPLCD_ILI9341, TFT_W, TFT_H, TFT_SCK, TFT_MOSI,
                    TFT_CS, TFT_DC, TFT_RST);
Adafruit_Monster_Eyes eyes(&display);

void setup() {
  Serial.begin(115200);
  eyes.setVerbose(Serial);
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
  eyes.animate();
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.printf("%.0f fps, largest block %u\n", eyes.frameRate(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
}
