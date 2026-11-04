# Qebabe Xiaoche ESP32-S3 — Project Context & Firmware Log

**Date:** 2026-11-04

**Filename note:** This file started as a firmware-backup log. It now covers the
full bring-up: hardware identification, backup, board definition, build, flash,
first boot, and testing. The path is kept for continuity.

## Current status

- Device identified and fully backed up.
- Vendor hardware mapped from the BOM and the accurate wiring diagram.
- Custom board definition added at `main/boards/qebabe-xiaoche/`.
- Built with ESP-IDF 6.1; app version `2.5.3`.
- Flashed over the CH343 UART port; boot log confirms display, audio, Wi-Fi,
  MQTT activation, wake word, and a spoken reply.
- NVS and the vendor `id` partition were preserved.
- Remaining: visually confirm the OLED, test motors/LED, adjust direction/PWM
  if needed, and optionally verify the backup with `verify-flash`.

## Quick reference

| Item | Value |
|---|---|
| Chip | ESP32-S3 (QFN56), revision v0.2 |
| MAC | `1c:29:04:3e:ce:50` |
| Flash | 16 MB, quad, 3.3 V |
| PSRAM | 8 MB embedded (octal, AP_3v3) |
| USB-UART | QinHeng CH343 (`1a86:55d3`) |
| Serial port | `/dev/cu.usbmodem5D300345091` |
| Board | `qebabe-xiaoche` (vendor robot car) |
| Board hardware | ESP32-S3-WROOM-1 N16R8, Freenove-compatible header layout |
| Vendor firmware | XiaoZhi `2.1.0`, ESP-IDF `v5.5.2-dirty` |
| Custom firmware | Repo `2.5.3`, ESP-IDF `6.1.0` |
| Backup | `backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin` |
| Backup SHA-256 | `f09041302f1949795d666766e4b2bea9c664314eb06bff8881303a856ee0019a` |
| Device ID | `RUK5IES0L128X` in the `id` partition |
| Board UUID | `0e574ec5-5b44-479d-9293-40a52ccd5b43` (from vendor NVS) |

## Device identification

```text
ESP32-S3 (QFN56) (revision v0.2)
Features: Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency: 40MHz
MAC: 1c:29:04:3e:ce:50
Flash: 16MB, quad (4 data lines), 3.3V
USB-UART: CH343 1a86:55d3 "USB Single Serial"
```

The board is a vendor robot car, not an upstream devkit. The firmware’s own
strings identify the board as `qebabe-xiaoche`:

- `./main/boards/qebabe-xiaoche/compact_wifi_board.cc`
- MCP server name `qebabe-xiaoche`
- status/board string `qebabe-xiaoche`

## USB ports (dual USB-C)

The board matches the Freenove ESP32-S3-WROOM dual-USB arrangement:

| Port | Chip | Connected to | Use |
|---|---|---|---|
| UART / COM | WCH CH343 (`1a86:55d3`) | ESP32-S3 UART0, TX GPIO43 / RX GPIO44 | Flashing, backup, serial console |
| USB / OTG | native ESP32-S3 USB | D- GPIO19 / D+ GPIO20 | USB-Serial/JTAG, USB device/host |

They are **not the same**. Use the CH343 port for flashing and backup; it stays
available even when the native USB is used for something else. The native port
may enumerate as an Espressif device (`303a:...`) instead of a CH343 device.

Some boards use a single USB-C port plus a CH334 hub; this one appears to have
the two-port Freenove layout.

## Security state (eFuse summary)

| Feature | State |
|---|---|
| Flash encryption | **Disabled** (`SPI_BOOT_CRYPT_CNT = 0`) |
| Secure boot | **Disabled** (`SECURE_BOOT_EN = 0`) |
| UART download mode | **Enabled** (`DIS_DOWNLOAD_MODE = 0`) |
| USB download modes | Enabled (`DIS_USB_*_DOWNLOAD_MODE = 0`) |
| JTAG | Not hard-disabled (`DIS_PAD_JTAG = 0`) |

The flash is readable, and unsigned custom firmware boots. No encryption or
secure-boot key is needed.

## Backup

**Path:**

```text
/Users/wenwu/work/my-xiaozhi-esp32/backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

**Result:**

- Size: `16,777,216` bytes (exactly 16 MB)
- Read time: 762.8 seconds at 230400 baud
- SHA-256: `f09041302f1949795d666766e4b2bea9c664314eb06bff8881303a856ee0019a`
- 921600 and 460800 aborted with serial corruption; 230400 completed cleanly.
- The partition table and app descriptor inside the dump parse correctly.

Optional extra verification:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  verify-flash 0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

The dump contains NVS, which includes Wi-Fi credentials, activation data, and
settings. Treat the `.bin` as a secret.

## Actual partition layout

Parsed from the dump at `0x8000`:

| Label | Type | Offset | Size | Notes |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x009000` | `0x004000` (16 KB) | Wi-Fi and settings |
| `otadata` | data/ota | `0x00d000` | `0x002000` (8 KB) | active OTA slot |
| `phy_init` | data/phy | `0x00f000` | `0x001000` (4 KB) | PHY calibration |
| `ota_0` | app/ota_0 | `0x020000` | `0x3f0000` (~4 MB) | active application |
| `ota_1` | app/ota_1 | `0x410000` | `0x3f0000` (~4 MB) | empty in the vendor dump |
| `id` | data/0x06 | `0x800000` | `0x001000` (4 KB) | vendor device ID |
| `assets` | data/spiffs | `0x801000` | `0x7ff000` (~8 MB) | voice/assets |

The vendor dump’s app descriptor:

- Project: `xiaozhi`
- Version: `2.1.0`
- Built: `Apr 8 2026 17:28:09`
- ESP-IDF: `v5.5.2-dirty`

The `id` partition contains the ASCII device ID `RUK5IES0L128X`, padded with
`0xFF`.

## Vendor files received

| File | What it is |
|---|---|
| `my_exploration/from_vendor/小智AI机器人配件清单.txt` | BOM: ESP32-S3 N16R8, L298N, INMP441, MAX98357, 0.96" OLED, 1S protection, 5V boost, 3-pin switch, 2415 speaker, 2x N20 motors, 2x 16340 cells |
| `my_exploration/from_vendor/小智AI版机器人接线图.jpg` | Accurate wiring diagram for this robot |
| `my_exploration/from_vendor/2.5ai_voice_display_oled_dog.ino.merged.bin` | 16 MB Arduino merged image for a different project (robot dog); partition table `nvs/app/spiffs/coredump`, IDF v5.4.1. **Not compatible with this OTA-partitioned S3 robot; do not flash.** |
| `my_exploration/ESP32-S3 CAM开发板资料.rar` | Generic Freenove ESP32-S3 CAM board materials (tutorials, libraries, datasheets, pinout). Useful for board hardware, not the vendor’s qebabe source. |

The first diagram (`IMG_2133.JPG`) included LiDAR/VL53L0X and was **not
accurate** for this unit. The accurate diagram shows no LiDAR, no VL53L0X, and
no camera.

## Accurate hardware / pin map

From the accurate diagram + BOM:

| Function | Pins / notes |
|---|---|
| Motors (L298N) | `IN1=GPIO11`, `IN2=GPIO12`, `IN3=GPIO13`, `IN4=GPIO14` — explicit in the diagram |
| LED/lamp | `GPIO8` — BOM says “正极接到8号引脚” |
| OLED SSD1306 | `SDA=GPIO41`, `SCL=GPIO42` — traced |
| Buttons (optional) | `Volume+ = GPIO40`, `Volume- = GPIO39`, `Boot = GPIO0` — traced |
| INMP441 mic | `WS=GPIO4`, `SCK=GPIO5`, `SD=GPIO6`, `L/R=GND`, `VDD=3V3`, `GND=GND` |
| MAX98357 amp | `DIN=GPIO7`, `BCLK=GPIO15`, `LRC=GPIO16`, `SD=3V3/5V`, `GAIN=GND`, `VIN=5V`, `GND=GND` |
| Speaker | 2415 8 Ω 1 W cavity speaker on the amp `+`/`-` |
| Power | 1S battery -> protection -> 3-pin switch -> 5V boost -> ESP32 5V and L298N |
| Sensors | No LiDAR, no VL53L0X, no camera on this unit |

Caveats:

- Motor and LED pins are explicit in the vendor diagram/BOM.
- OLED, button, mic, and amp pins are traced; verify on hardware.
- The L298N `ENA`/`ENB` jumper caps are already installed (the robot moved with
  the vendor firmware). If the motors stop after reflashing, check those
  jumpers.
- The GPIO8 LED is 3 V; use a resistor or proper driver.

## Board definition created in this repo

Added `main/boards/qebabe-xiaoche/`:

| File | Purpose |
|---|---|
| `config.h` | I2S, OLED, buttons, L298N, LED pins |
| `config.json` | ESP32-S3, 16 MB, 128x64 OLED, custom partition CSV |
| `partitions.csv` | Vendor layout including `id` at `0x800000` |
| `compact_wifi_board.cc` | Wi-Fi board + SSD1306 + INMP441/MAX98357 + L298N PWM + MCP tools |
| `README.md` | Hardware/build/flash notes |

Also added:

- `CONFIG_BOARD_TYPE_QEBABE_XIAOCHE` in `main/Kconfig.projbuild`
- `BOARD_DIR "qebabe-xiaoche"` branch in `main/CMakeLists.txt`
- `BOARD_TYPE_QEBABE_XIAOCHE` in the `DISPLAY_OLED_TYPE` dependency list
  (required for `CONFIG_OLED_SSD1306_128X64`)

### MCP tools implemented

Vendor S3 2.1.0 tool names:

```text
self.motor.move_forward
self.motor.move_backward
self.motor.spin_around
self.motor.turn_left
self.motor.turn_right
self.motor.quick_forward
self.motor.quick_backward
self.motor.wiggle
self.motor.dance
self.motor.stop
```

Additional convenience tools:

```text
self.motor.drive
self.motor.forward
self.motor.backward
self.car.act
self.lamp.get_state
self.lamp.turn_on
self.lamp.turn_off
```

Not implemented:

- The newer C3 2.2.x `self.motor.sequence` / advanced `self.car.act`
  action-sequence system (not present in the S3 2.1.0 dump).
- Fall detection for `move_forward` / `quick_forward` (no edge/distance sensor).
- The vendor `self.rgb_led.*` tools; the on-board WS2812 is used as the status
  LED.

## Build environment

- ESP-IDF `release/v6.1` at `/Users/wenwu/esp/esp-idf`
- ESP-IDF version: `6.1.0`
- Xtensa GCC: `15.2.0`
- CMake: `4.4.5`
- Ninja: `1.13.2`

```sh
. /Users/wenwu/esp/esp-idf/export.sh
cd /Users/wenwu/work/my-xiaozhi-esp32
python3 scripts/build.py qebabe-xiaoche --name qebabe-xiaoche
```

Build result:

- App version `2.5.3`
- `xiaozhi.bin` size `0x256fd0` (~2.4 MB)
- App partition `0x3f0000`; ~41% free
- Bootloader and partition table generated successfully
- `build/merged-binary.bin` generated, but **do not flash it at `0x0`** because
  it can overwrite the `id` partition/NVS. Use individual images.

## Flash procedure

`idf.py flash` hit a Python-path mismatch (`python` vs `python3`) in this
environment, so the images were written directly with `esptool`:

```sh
cd /Users/wenwu/work/my-xiaozhi-esp32
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 -b 230400 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x20000 build/xiaozhi.bin \
  0x801000 build/generated_assets.bin
```

The write verified successfully. This command writes only:

- bootloader `0x0`
- partition table `0x8000`
- `otadata` `0xd000`
- app `0x20000`
- assets `0x801000`

It does **not** write NVS (`0x9000`–`0xD000`) or `id` (`0x800000`).

## First boot result

Boot log confirms:

- 8 MB PSRAM, 16 MB flash
- `Board: UUID=0e574ec5-5b44-479d-9293-40a52ccd5b43 SKU=qebabe-xiaoche`
- SSD1306 OLED driver installed and LVGL started
- `NoAudioCodec: Simplex channels created`
- 27 MCP tools registered; all `self.motor.*`, `self.car.act`, and `self.lamp.*`
- Wi-Fi connected to the saved network; got IP `192.168.1.13`
- OTA: “Current is the latest version”, running `ota_0`
- MQTT connected and “Activation done”
- Wake word `Hi,ESP` detected
- A spoken question was answered with a spoken reply
- No panic or reboot loop

This confirms the display init, audio output, microphone/wake word, Wi-Fi, MQTT,
and the MCP registration path.

## Why Wi-Fi, UUID, and the name survived the reflash

We did **not** copy the backup over. The NVS partition was preserved because
the flash command wrote around it:

| Region | Offset | Written? |
|---|---|---|
| Bootloader | `0x0` | Yes |
| Partition table | `0x8000` | Yes |
| `otadata` | `0xd000` | Yes |
| App | `0x20000` | Yes |
| NVS | `0x9000`–`0xD000` | **No** |
| `id` | `0x800000` | **No** |
| Assets | `0x801000` | Yes |

Consequences:

- Wi-Fi credentials, UUID, activation/MQTT config, and settings came from the
  vendor’s existing NVS.
- The hostname `Xiaozhi-CE50` is derived from the Wi-Fi MAC (`...CE50`), not
  stored in NVS.
- The board type `qebabe-xiaoche` comes from our `config.json`/`BOARD_TYPE`.
- The device ID `RUK5IES0L128X` remains in the `id` partition.

### Erase only NVS (optional)

If a clean provisioning state is wanted:

```sh
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 \
  erase-region 0x9000 0x4000
```

This removes Wi-Fi, UUID, and saved settings, and the robot should enter the
`Xiaozhi-XXXX` Wi-Fi config hotspot. The `id` partition and assets remain.

Do **not** use `erase-flash` unless a full restore is intended.

## Testing log

Confirmed:

- OLED driver/LVGL init (visual confirmation still useful)
- Audio codec init, wake word, spoken reply
- Wi-Fi, MQTT, OTA check, activation
- MCP tools registered

Pending:

- Visually confirm the OLED UI and orientation
- Motors: forward, backward, turn left/right, spin, stop
- LED/lamp on GPIO8: on/off
- Optional buttons: BOOT, Volume+, Volume-
- Optional `verify-flash` of the 16 MB backup

Suggested voice commands:

```text
Hi, ESP
Move forward for one second.
Turn left for half a second.
Stop.
Turn on the lamp.
Turn off the lamp.
```

Expected motor mapping:

| Command | L298N |
|---|---|
| `IN1` | GPIO11 |
| `IN2` | GPIO12 |
| `IN3` | GPIO13 |
| `IN4` | GPIO14 |

If a motor direction is wrong, swap its two `IN` pins in `config.h` or swap its
two output wires. If left/right are swapped, swap the two motor pairs. If the
motors do not move, check the L298N power, common ground, and `ENA`/`ENB`
jumpers.

## Recovery

Enter download mode (hold **BOOT**, tap **RESET**, release BOOT), then reflash
the individual images. If the bootloader/partition table is damaged, restore
the full dump:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  write-flash 0x0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

Do not use `idf.py erase-flash` unless you intend to erase NVS, the `id`
partition, and assets.

## Open questions / next steps

1. Visually confirm the OLED and adjust `DISPLAY_MIRROR_X/Y` if needed.
2. Test the motors and LED; adjust direction/PWM if needed.
3. Optional: `verify-flash` the backup or take a second full read.
4. Decide whether to reimplement the vendor’s action-sequence/license/OTA
   behavior; the vendor’s S3 source or `PROJECT.md` is still the best reference.
5. Check NVS encryption before relying on cross-chip restore. There is no
   `nvs_keys` partition and the eFuse key slots are `USER`, so NVS is likely
   unencrypted; confirm before assuming.
6. Test a full restore only after the backup is verified; keep a second copy
   off-device.

## Commands used

```sh
# Detect chip and flash
uvx --from esptool esptool -p /dev/cu.usbmodem5D300345091 -b 460800 flash-id

# Inspect eFuses (read-only)
uvx --from esptool espefuse -p /dev/cu.usbmodem5D300345091 summary

# Full backup (successful baud rate)
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  read-flash 0 0x1000000 \
  backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin

# Build
. /Users/wenwu/esp/esp-idf/export.sh
python3 scripts/build.py qebabe-xiaoche --name qebabe-xiaoche

# Flash individual images (preserves NVS and id)
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 -b 230400 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x20000 build/xiaozhi.bin \
  0x801000 build/generated_assets.bin

# Restore full dump (same chip/flash size)
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  write-flash 0x0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin

# Erase only NVS (optional clean provisioning)
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 \
  erase-region 0x9000 0x4000
```

## References

- `partitions/v2/16m.csv` — 16 MB partition layout used by many S3 boards here
- `sdkconfig.defaults` — OTA rollback setting
- `docs/custom-board.md` — board/variant build chain
- `main/boards/qebabe-xiaoche/README.md` — board-specific notes
- `main/boards/m5stack/cardputer/README.md` — merged image overwrites NVS
- `main/boards/seeed/xiao-esp32s3-respeaker-xvf3800/README.md` — 8 MB S3 example
- Vendor C3 page: <https://330p035.my/share/e3d0d2fbc7d160e1> (different variant)
- Freenove dual-USB documentation: <https://github.com/betaflight/bridge/blob/master/README.md>
