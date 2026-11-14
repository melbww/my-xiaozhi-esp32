# Desk Bot 01 — Hardware

**Last verified:** 2026-11-08
**Full history:** [../.archive/history/2026-11-bringup-log.md](../.archive/history/2026-11-bringup-log.md)

The board is a vendor robot car, not an upstream devkit. The custom board
definition lives at `main/boards/desk-bot-01/`.

## Device identification

```text
ESP32-S3 (QFN56) (revision v0.2)
Features: Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency: 40MHz
MAC: 1c:29:04:3e:ce:50
Flash: 16MB, quad (4 data lines), 3.3V
USB-UART: CH343 1a86:55d3 "USB Single Serial"
```

| Item | Value |
|---|---|
| Module | ESP32-S3-WROOM-1 N16R8 |
| Flash | 16 MB, quad, 3.3 V |
| PSRAM | 8 MB embedded (octal, AP_3v3) |
| Hostname | `Xiaozhi-CE50` (derived from the MAC, not stored in NVS); use `http://Xiaozhi-CE50/` for the LAN camera page. The numeric DHCP lease is not stable (was `192.168.1.13`, now `192.168.1.5`). |
| Vendor device ID | `RUK5IES0L128X` in the `id` partition |
| Board UUID | `0e574ec5-5b44-479d-9293-40a52ccd5b43` (vendor NVS) |

The firmware identifies the board as `desk-bot-01` (MCP server name and
status string). Before the 2026-11-09 rename it reported the old
`qebabe-xiaoche` identity; that only matters for logs captured before then.

## USB ports (dual USB-C)

The board matches the Freenove ESP32-S3-WROOM dual-USB arrangement:

| Port | Chip | Connected to | Use |
|---|---|---|---|
| UART / COM | WCH CH343 (`1a86:55d3`) | ESP32-S3 UART0, TX GPIO43 / RX GPIO44 | Flashing, backup, serial console |
| USB / OTG | native ESP32-S3 USB | D− GPIO19 / D+ GPIO20 | USB-Serial/JTAG, USB device/host |

They are **not the same**. Use the CH343 port for flashing and backup; it stays
available when the native USB is used for something else. The native port may
enumerate as an Espressif device (`303a:...`).

The USB/OTG port does **not** supply VBUS. A USB camera needs external 5 V
(powered hub or a 5 V/GND pigtail). See [../camera/README.md](../camera/README.md).

## Security state (eFuse)

| Feature | State |
|---|---|
| Flash encryption | **Disabled** (`SPI_BOOT_CRYPT_CNT = 0`) |
| Secure boot | **Disabled** (`SECURE_BOOT_EN = 0`) |
| UART download mode | **Enabled** (`DIS_DOWNLOAD_MODE = 0`) |
| USB download modes | Enabled (`DIS_USB_*_DOWNLOAD_MODE = 0`) |
| JTAG | Not hard-disabled (`DIS_PAD_JTAG = 0`) |

The flash is readable and unsigned custom firmware boots. No encryption or
secure-boot key is needed.

## Backup

```text
/Users/wenwu/work/my-xiaozhi-esp32/backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

| Item | Value |
|---|---|
| Size | `16,777,216` bytes (16 MB) |
| Read time | 762.8 s at 230400 baud |
| SHA-256 | `f09041302f1949795d666766e4b2bea9c664314eb06bff8881303a856ee0019a` |

- 921600 and 460800 aborted with serial corruption; 230400 completed cleanly.
- Treat the `.bin` as a secret: it contains Wi-Fi credentials, activation data,
  and settings.
- Optional verification:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  verify-flash 0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

## Partition layout

Parsed from the vendor dump at `0x8000`:

| Label | Type | Offset | Size | Notes |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x009000` | `0x004000` (16 KB) | Wi-Fi and settings |
| `otadata` | data/ota | `0x00d000` | `0x002000` (8 KB) | active OTA slot |
| `phy_init` | data/phy | `0x00f000` | `0x001000` (4 KB) | PHY calibration |
| `ota_0` | app/ota_0 | `0x020000` | `0x3f0000` (~4 MB) | active application |
| `ota_1` | app/ota_1 | `0x410000` | `0x3f0000` (~4 MB) | empty in the vendor dump |
| `id` | data/0x06 | `0x800000` | `0x001000` (4 KB) | vendor device ID |
| `assets` | data/spiffs | `0x801000` | `0x7ff000` (~8 MB) | voice/assets |

The vendor dump’s app descriptor: project `xiaozhi`, version `2.1.0`, built
`Apr 8 2026 17:28:09`, ESP-IDF `v5.5.2-dirty`.

The `id` partition contains ASCII `RUK5IES0L128X`, padded with `0xFF`.

## Verified pin map

The vendor diagram/BOM’s `IN1=GPIO11`, `LED=GPIO8` mapping does **not** match
this hardware revision. The real map was recovered by disassembling the vendor
2.1.0 application; see [../.archive/vendor/reverse-engineering.md](../.archive/vendor/reverse-engineering.md).

| Function | Pins / notes |
|---|---|
| Motors (L298N) | `IN1=GPIO12`, `IN2=GPIO13`, `IN3=GPIO14`, `IN4=GPIO21` |
| LED/lamp | `GPIO18` vendor lamp output (not populated on this unit); 7-LED strip is the only light |
| WS2812 status strip | `GPIO38`, 7 LEDs |
| OLED SSD1306 | `SDA=GPIO41`, `SCL=GPIO42` |
| Buttons (optional) | `Volume+ = GPIO40`, `Volume− = GPIO39`, `BOOT = GPIO0` |
| INMP441 mic | `WS=GPIO4`, `SCK=GPIO5`, `SD=GPIO6`, `L/R=GND`, `VDD=3V3`, `GND=GND` |
| MAX98357 amp | `DIN=GPIO7`, `BCLK=GPIO15`, `LRC=GPIO16`, `SD=3V3/5V`, `GAIN=GND`, `VIN=5V`, `GND=GND` |
| Speaker | 2415 8 Ω 1 W cavity speaker on the amp `+`/`−` |
| Power | 1S battery → protection → 3-pin switch → 5 V boost → ESP32 5 V and L298N |
| Sensors | No LiDAR/VL53L0X; no built-in camera |

Vendor direction/channel mapping (LEDC channels 0–3 = GPIO 12, 13, 14, 21):

| Motion | Channels |
|---|---|
| forward | 0 + 2 |
| backward | 1 + 3 |
| right turn / spin | 0 + 3 |
| left turn | 1 + 2 |

The vendor uses 20 kHz LEDC PWM, 10-bit resolution, and a 50 ms ramp; the board
code matches.

Caveats:

- Motor and LED pins are explicit in the vendor diagram/BOM and verified in the
  vendor binary.
- OLED, button, mic, and amp pins are traced; verify on hardware.
- The L298N `ENA`/`ENB` jumper caps are installed. If the motors stop after
  reflashing, check them.
- The N20 motors do not overcome static friction below ~60 % duty on this
  5 V/L298N rail. The board maps every non-zero speed into 60–100 % duty
  (`MOTOR_MIN_DRIVE_PERCENT`).

## Board definition

Added `main/boards/desk-bot-01/`:

| File | Purpose |
|---|---|
| `config.h` | I2S, OLED, buttons, L298N, LED pins |
| `config.json` | ESP32-S3, 16 MB, 128×64 OLED, custom partition CSV, local OTA URL |
| `partitions.csv` | Vendor layout including `id` at `0x800000` |
| `compact_wifi_board.cc` | Wi-Fi board + SSD1306 + INMP441/MAX98357 + L298N PWM + MCP tools |
| `README.md` | Hardware/build/flash notes |

Also added:

- `CONFIG_BOARD_TYPE_DESK_BOT_01` in `main/Kconfig.projbuild`
- `BOARD_DIR "desk-bot-01"` branch in `main/CMakeLists.txt`
- `BOARD_TYPE_DESK_BOT_01` in the `DISPLAY_OLED_TYPE` dependency list

This is a **reconstruction** from the vendor BOM/diagram, not the vendor’s
original source.
