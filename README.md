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
    pio device monitor -b 115200

Wi-Fi and MQTT credentials live in `include/secrets.h` (gitignored; see
`include/secrets.h.example`). The Wi-Fi values were copied from the ESPHome secrets.

## MQTT control
Broker 10.0.0.13:1883, client id `monster-eyes-cyd`.

| topic                    | direction | payload                                   |
|--------------------------|-----------|-------------------------------------------|
| `monster_eyes/eye/set`   | to board  | `open` / `closed` (also on/off, 1/0)      |
| `monster_eyes/eye/state` | from board| `open` / `closed`, retained               |
| `monster_eyes/status`    | from board| `online` / `offline`, retained, LWT       |

`./eye.py open`, `./eye.py closed`, `./eye.py watch` use paho-mqtt from the
homeassistant pyenv to drive and observe it. A command eases the lids shut over
250 ms and holds them; `open` hands the lids back to the library's own blinking.

### Home Assistant discovery
Each board registers itself through MQTT device discovery, so no YAML is needed.
On connect, and whenever Home Assistant publishes `online` to `homeassistant/status`,
a board sends a non-retained config to `homeassistant/device/<id>/config`:

| device | entity | notes |
|--------|--------|-------|
| Monster eyes (`monster_eyes_primary`) | switch "Open" | on = open, off = closed; unavailable when the primary is offline |
| Monster eyes secondary (`monster_eyes_secondary`) | binary sensor "Online" | diagnostic, connectivity |
| | binary sensor "Sync locked" | diagnostic; on while the I2C link is carrying state |

Requires the MQTT integration's discovery option to be on (the default) with the
`homeassistant` prefix.

## Two synced eyes over I2C
One board is the primary: it animates, takes the MQTT commands above, and sends
gaze, pupil, blink and clock to the secondary every frame. The secondary renders what
it receives, and falls back to animating on its own if the link goes quiet.

Wiring, on the 4-pin CN1 connector (GND, IO22, IO27, 3.3V) of each board:

| primary | secondary | role |
|---------|-----------|------|
| GND     | GND       | ground (required) |
| IO27    | IO27      | SDA |
| IO22    | IO22      | SCL |
| 3.3V    | not connected | |

Flash each board with its role, and the same eye image on both:

    pio run -e cyd_primary   -t upload --upload-port /dev/cu.usbserial-AAAA
    pio run -e cyd_secondary -t upload --upload-port /dev/cu.usbserial-BBBB
    PORT=/dev/cu.usbserial-AAAA ./build-eye-image.sh demon --flash
    PORT=/dev/cu.usbserial-BBBB ./build-eye-image.sh demon --flash

The primary draws the character's right eye and the secondary the left, so facing the
character the primary sits on your left. Each prints `Eye size: N px` at boot; the two
must match. If they differ, pin both with `-DEYE_SIZE=N` in `platformio.ini`. The link runs
at 100 kHz: at 400 kHz every packet was NACKed, because the ESP32's internal pull-ups are
too weak for that speed. With 4.7k pull-ups from SDA and SCL to 3.3V on one board you
can try `-DSYNC_HZ=400000`.

Every five seconds the primary logs `sync: sent N, failed M` and the secondary logs
`sync: LOCKED` or `free-running`, with a hint when something is wrong.

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
