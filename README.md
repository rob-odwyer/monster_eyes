# Monster Eyes on the Cheap Yellow Display (ESP32-2432S028R)

PlatformIO test project: Adafruit Monster Eyes 2.3.0 driving the CYD's ILI9341 over
esp_lcd/DMA. Compiles for `esp32dev` with the Arduino 3.2.1 core. Not yet run on hardware.

## What is patched (see cyd-ili9341.patch, applied in lib/Adafruit_Monster_Eyes)
- `Eyes_Assets.cpp`: USB drive mode compiled out when the chip has no native USB
  (`CFG_TUD_ENABLED == 0`, i.e. classic ESP32). Stock library fails to compile otherwise.
- `Eyes_ESPLCD`: new `EYES_ESPLCD_ILI9341` driver that sends Adafruit's ILI9341 init
  sequence and selects BGR colour order.

## Build and flash
    pio run -e cyd -t upload
    pio device monitor

## Eye graphics (no USB drive on this board)
The library reads `/config.eye` and BMPs from the `ffat` FAT partition
(`default_ffat.csv`: offset 0x290000, size 0x160000). All of Adafruit's eye designs are
in `assets/eyes/<name>/`. Edit `assets/eyes/<name>/config.eye` to tweak a design, then:

    ./build-eye-image.sh <name>           # writes assets/ffat-<name>.img
    ./build-eye-image.sh <name> --flash   # ...and writes it to the board

The script builds a raw FAT12 image (512-byte sectors, no wear levelling) with
newfs_msdos and flashes it with esptool at 0x290000. Override the serial port with
`PORT=/dev/cu.xxx`. Firmware is not touched; the board resets when the write finishes.

Do NOT use `pio run -t uploadfs` with `board_build.filesystem = fatfs`: that image has a
4 KB offset and wear-levelling metadata that Adafruit_SPIFlash/SdFat cannot read.
