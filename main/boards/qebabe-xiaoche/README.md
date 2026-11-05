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
| Lamp | No separate lamp; the 7-LED WS2812 strip is the only light |
| Status LEDs | 7x WS2812 strip on GPIO38 |
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
| L298N `IN1` | 12 |
| L298N `IN2` | 13 |
| L298N `IN3` | 14 |
| L298N `IN4` | 21 |
| Lamp output (vendor firmware, not populated) | 18 |
| WS2812 strip (7 LEDs) | 38 |

These pins were verified by disassembling the working vendor `xiaozhi` 2.1.0
application image (`InitMotorPwm` configures LEDC channels 0-3 on GPIO 12, 13,
14, 21; `LampController` is constructed with GPIO 18) and by the vendor boot
log (`WS2812 RGB LED (GPIO38) ... 7 LEDs`). The vendor diagram/BOM's
`IN1=GPIO11`, `LED=GPIO8` mapping does not match this hardware revision.

The vendor firmware uses 20 kHz LEDC PWM at 10-bit resolution with a 50 ms
ramp; the board code uses the same frequency and resolution.

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
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX monitor
```

If `idf.py` reports a `python` vs `python3` environment mismatch, flash the
individual images directly. This also preserves NVS and the `id` partition:

```sh
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 230400 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x20000 build/xiaozhi.bin \
  0x801000 build/generated_assets.bin
```

Do not flash `build/merged-binary.bin` at `0x0`; it can overwrite the `id`
partition and NVS.

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

- The S3 2.1.0 motor tool names are implemented: `self.motor.move_forward`,
  `move_backward`, `spin_around`, `turn_left`, `turn_right`, `quick_forward`,
  `quick_backward`, `wiggle`, `dance`, `stop`, plus `self.motor.drive` and a
  simple `self.car.act`.
- The newer C3 2.2.x `self.motor.sequence` / `self.car.act` action-sequence
  system is not present in the S3 2.1.0 dump and is not replicated here.
- Fall detection for `move_forward` / `quick_forward` is not implemented because
  this unit has no edge/distance sensor.
- The vendor's `self.rgb_led.*` tools are not replicated; the on-board WS2812 is
  used as the status LED.
- The vendor firmware was built with ESP-IDF `v5.5.2-dirty`; this repo targets
  ESP-IDF 6.x. Expect minor API/behavior differences.
- The OLED orientation (`DISPLAY_MIRROR_X/Y`) may need adjusting for the
  actual enclosure.
- There is no separate lamp on this unit. The `self.lamp.*` and
  `self.rgb_led.*` tools drive the 7-LED WS2812 strip; the vendor's GPIO18 lamp
  output is still driven for compatibility.
- Motors do not turn below roughly 60 % duty with the L298N on this rail, so
  non-zero speed commands are mapped into 60-100 % duty.
- No LiDAR, VL53L0X, or camera is configured for this unit.
