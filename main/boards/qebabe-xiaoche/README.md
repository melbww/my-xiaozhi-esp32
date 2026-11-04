# Qebabe Xiaoche (ESP32-S3 robot car)

Vendor robot car based on an ESP32-S3-WROOM-1 N16R8 development board. This
board definition reconstructs the hardware from the vendor wiring diagram and
BOM; the vendor's original `compact_wifi_board.cc` was not available.

## Hardware

| Function | Pin / part |
|---|---|
| MCU | ESP32-S3-WROOM-1 N16R8, 16 MB flash, 8 MB PSRAM |
| USB | CH343 UART port + native USB-C port |
| Microphone | INMP441, I2S |
| Amplifier | MAX98357, I2S |
| Speaker | 2415 8 Ω 1 W cavity speaker |
| Display | SSD1306 OLED 128x64, I2C |
| Buttons | BOOT, Volume+, Volume- (optional panel) |
| Motor driver | L298N, 4 inputs |
| Motors | 2x N20 3 V 30 rpm gear motors |
| LED/lamp | 3 V LED on GPIO8 |
| Power | 1S battery + protection + 3-pin switch + 5 V boost |

## Pin map

| Signal | GPIO |
|---|---|
| INMP441 `WS` | 4 |
| INMP441 `SCK` | 5 |
| INMP441 `SD` | 6 |
| MAX98357 `DIN` | 7 |
| MAX98357 `BCLK` | 15 |
| MAX98357 `LRC` | 16 |
| OLED `SDA` | 41 |
| OLED `SCL` | 42 |
| Button `BOOT` | 0 |
| Button `Volume+` | 40 |
| Button `Volume-` | 39 |
| L298N `IN1` | 11 |
| L298N `IN2` | 12 |
| L298N `IN3` | 13 |
| L298N `IN4` | 14 |
| LED/lamp | 8 |
| On-board WS2812 | 48 |

The motor and LED pins are explicit in the vendor diagram/BOM. The audio,
OLED, and button pins are traced from the diagram; verify them on hardware
before assuming they are correct.

The firmware drives only the L298N `IN1`–`IN4` inputs, matching the vendor
firmware. On a stock L298N module the `ENA`/`ENB` jumper caps connect the
enable pins to 5 V; if the robot already moved with the vendor firmware, those
jumpers are already installed and no change is needed. If the motors do not
move after reflashing, check that the `ENA`/`ENB` jumpers are still in place
(or that both pins are tied high).

## Build

```sh
python3 scripts/build.py --list-boards
python3 scripts/build.py qebabe-xiaoche --name qebabe-xiaoche
```

Then flash over the CH343 UART port:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

## Partition layout

This board keeps the vendor partition layout, including the custom `id`
partition at `0x800000`:

| Partition | Offset | Size |
|---|---|---|
| `nvs` | `0x9000` | 16 KB |
| `otadata` | `0xd000` | 8 KB |
| `phy_init` | `0xf000` | 4 KB |
| `ota_0` | `0x20000` | ~4 MB |
| `ota_1` | `0x410000` | ~4 MB |
| `id` | `0x800000` | 4 KB |
| `assets` | `0x801000` | ~8 MB |

**Preserve the `id` partition.** It contains the vendor device ID used by the
activation/license service. `idf.py flash` writes individual images and does
not erase it. A merged image written at `0x0` may overwrite it; keep the full
16 MB backup from `my_exploration/` and restore the partition if needed.

The upstream XiaoZhi firmware does not implement the vendor's
`espdeviceverifyservice.ytqhz.com` license check, so this board code preserves
the `id` partition but does not read it. Use the vendor firmware/backup if you
need the original activation flow.

## Known limitations

- The vendor's full MCP tool set (`self.motor.sequence`, `self.car.act`
  sequences, `self.sensor.*`, etc.) is not replicated. This board provides
  basic `self.motor.*` tools and a simple `self.car.act`.
- The vendor firmware was built with ESP-IDF `v5.5.2-dirty`; this repo targets
  ESP-IDF 6.x. Expect minor API/behavior differences.
- The OLED orientation (`DISPLAY_MIRROR_X/Y`) may need adjusting for the
  actual enclosure.
- The 3 V LED on GPIO8 must not be driven beyond its rating; use a resistor or
  a proper driver.
- No LiDAR, VL53L0X, or camera is configured for this unit.
