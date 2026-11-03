# ESP32-S3 Robot Firmware Backup — 2026-11-04

## Device identified

| Item | Value |
|---|---|
| Chip | ESP32-S3 (QFN56), revision v0.2 |
| MAC | `1c:29:04:3e:ce:50` |
| Flash | 16 MB, quad (4 data lines), 3.3 V |
| PSRAM | 8 MB embedded (AP_3v3) |
| Crystal | 40 MHz |
| USB-UART bridge | QinHeng CH343 (`1a86:55d3`), "USB Single Serial" |
| macOS port | `/dev/cu.usbmodem5D300345091` |
| Board | `qebabe-xiaoche` (vendor robot-car board; not present in this repo) |
| Firmware | XiaoZhi `2.1.0`, built Apr 8 2026, ESP-IDF `v5.5.2-dirty` |
| Display | SSD1306 OLED (`CompactWifiBoard`) |
| Motor control | ESP32 LEDC PWM, 4 channels (`InitMotorPwm`) |
| LED control | `LampController` + `led_config` patterns |
| Device identity | custom `id` partition at `0x800000` (4 KB) |

The board is a custom/unknown robot rather than an obviously named devkit. It can join home Wi-Fi, drive motors, and control LEDs.

## USB ports (dual USB-C)

The board matches the Freenove ESP32-S3-WROOM dual-USB arrangement (the 16 MB N16R8 variant uses the same layout):

| Port | Chip | Connected to | Use |
|---|---|---|---|
| UART / COM (currently used) | WCH CH343 (`1a86:55d3`) | ESP32-S3 UART0, TX GPIO43 / RX GPIO44 | Flashing, backup, serial console; recommended for `esptool` |
| USB / OTG | native ESP32-S3 USB | D- GPIO19 / D+ GPIO20 | USB-Serial/JTAG, USB device/host; can also flash in download mode |

They are **not the same**:

- The **CH343 port** is a USB-to-UART bridge. It is the traditional, reliable flashing/console port and remains available even if the firmware uses the native USB for something else.
- The **native USB port** is wired directly to the ESP32-S3. It supports USB-Serial/JTAG and USB-OTG; it may enumerate as an Espressif device (VID `303a`) instead of a CH343 device. If the firmware switches it to USB host/device mode, the serial console can disappear.

For backup and normal flashing, stay on the CH343 port (`/dev/cu.usbmodem5D300345091`). No need to move to the other port. The other port is useful for JTAG debugging or USB host/device work.

Some boards instead use a **single** USB-C port plus a CH334 hub, exposing two logical COM ports. Check the board labels/schematic if unsure. To identify a port, plug it in and check `ls /dev/cu.*` and the USB VID/PID:

- `1a86:55d3` = CH343 UART
- `303a:1001` = Espressif USB JTAG/serial debug unit (native)

Sources:

- Betaflight bridge README (Freenove dual-USB details): <https://github.com/betaflight/bridge/blob/master/README.md>
- Freenove XiaoZhi guide (CH343 and correct Type-C port): <https://docs.freenove.com/projects/fnk0102/en/latest/fnk0102/codes/xiaozhi/xiaozhi_EN/XiaoZhi_AI_User_Guide_%28Based_on_FNK0102%29.html>
- Espressif serial connection guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/establish-serial-connection.html>

## Security state (from `espefuse summary`)

| Feature | State |
|---|---|
| Flash encryption | **Disabled** (`SPI_BOOT_CRYPT_CNT = 0`) |
| Secure boot | **Disabled** (`SECURE_BOOT_EN = 0`) |
| UART download mode | **Enabled** (`DIS_DOWNLOAD_MODE = 0`) |
| USB download modes | Enabled (`DIS_USB_*_DOWNLOAD_MODE = 0`) |
| JTAG | Not hard-disabled (`DIS_PAD_JTAG = 0`) |

**Conclusion:** the flash is readable, and unsigned custom firmware can boot. No encryption/secure-boot key is needed. This is the best-case scenario for backup, rollback, and customization.

## Backup

**Path:**

```text
/Users/wenwu/work/my-xiaozhi-esp32/backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

**Command:**

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  read-flash 0 0x1000000 \
  backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

**Status: completed successfully.**

- Size: `16,777,216` bytes (exactly 16 MB)
- Read time: 762.8 seconds (~12m43s) at 230400 baud
- SHA-256: `f09041302f1949795d666766e4b2bea9c664314eb06bff8881303a856ee0019a`
- The earlier attempts at 921600 and 460800 aborted with serial corruption (`Invalid head of packet (0xFF)`, `Corrupt data, expected 0x1000 bytes...`). 230400 completed cleanly.

The partition table and app descriptor inside the dump parse correctly, which is a good sanity check. For extra confidence, a second full read or `verify-flash` can be run later:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  verify-flash 0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

The dump contains NVS, which may include Wi-Fi credentials, activation data, and server settings. **Treat the `.bin` as a secret.** It is covered by the repo's `*.bin` ignore rule, but it should still be stored somewhere private.

## Actual partition layout and firmware (from the dump)

Parsed from the partition table at `0x8000`:

| Label | Type | Offset | Size | Notes |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x009000` | `0x004000` (16 KB) | Wi-Fi credentials and settings |
| `otadata` | data/ota | `0x00d000` | `0x002000` (8 KB) | active OTA slot |
| `phy_init` | data/phy | `0x00f000` | `0x001000` (4 KB) | PHY calibration |
| `ota_0` | app/ota_0 | `0x020000` | `0x3f0000` (~4 MB) | active application |
| `ota_1` | app/ota_1 | `0x410000` | `0x3f0000` (~4 MB) | empty (`0xFF`) |
| `id` | data/0x06 | `0x800000` | `0x001000` (4 KB) | device identity/activation data (custom) |
| `assets` | data/spiffs | `0x801000` | `0x7ff000` (~8 MB) | voice prompts, models, fonts, images |

App descriptor in `ota_0`:

- Project: `xiaozhi`
- Version: `2.1.0`
- Built: `Apr 8 2026 17:28:09`
- ESP-IDF: `v5.5.2-dirty`
- `ota_1` is blank, so the device is currently running from `ota_0`.

**Important differences from this repo's default `partitions/v2/16m.csv`:**

- The repo's 16 MB table puts `assets` at `0x800000` with an 8 MB size and has no `id` partition.
- This device has a custom `id` partition at `0x800000` (4 KB) and `assets` at `0x801000` with size `0x7ff000`.
- Do not blindly flash a `merged-binary.bin` built with the repo's default 16 MB table: it will not match this layout and may overwrite or ignore the `id` partition. Use the exact dump for rollback, or create a matching partition table for custom builds.
- The firmware was built with **ESP-IDF v5.5.2-dirty**, while this repo targets ESP-IDF 6.0.1+. Rebuilding the vendor firmware may require the vendor's IDF version or a port.

### Board identity from app strings

- Board: `qebabe-xiaoche` — appears in `./main/boards/qebabe-xiaoche/compact_wifi_board.cc`, the MCP server name, and status output.
- Base class: `CompactWifiBoard`; display: SSD1306 OLED.
- Motors: `InitMotorPwm` uses an LEDC timer and 4 channels. MCP tools include `self.motor.turn_left`, `self.motor.turn_right`, `self.motor.quick_forward`, `self.motor.quick_backward`, `self.motor.spin`, etc.
- LEDs/lamp: `LampController` (GPIO) and `led_config` patterns. MCP tools include `self.lamp.get_state`, `self.lamp.turn_on`, `self.lamp.turn_off`.
- Device ID: the app logs “Device ID read from partition”; the custom `id` partition at `0x800000` stores it.
- Vendor URLs: OTA `https://api.tenclass.net/xiaozhi/ota/`; music control `https://xiaozhimusic.ytqhz.com/api/v1/control`.

This confirms the motor and LED behavior is on the ESP32 side and is included in the dump. There is no evidence of a separate motor/servo controller firmware; the motors are driven directly by ESP32 PWM.

## What the full dump contains

A full 16 MB read is a byte-for-byte image of the ESP32's external flash:

- bootloader
- partition table
- `otadata` (which OTA slot is active)
- the OTA application slot(s) defined by the partition table (typically `ota_0` and `ota_1`)
- any assets partition (voice prompts, wake-word models, fonts, images)
- the `nvs` partition, if present (typically Wi-Fi credentials, device settings, activation/pairing state, and calibration values stored by the app)
- the custom `id` partition on this device (device identity/activation data)
- PHY calibration data
- any other partitions defined by this device's partition table

So, for the ESP32 side, the dump includes **both the code and the saved memory** needed to reproduce the current behavior: Wi-Fi connection, motor-control logic, LED-control logic, and local settings.

## What the dump does NOT contain

- **Firmware inside other chips/controllers** — e.g. an STM32/RP2040 motion controller, a smart LED controller, or a camera module with its own MCU. Only the ESP32's side of the communication is included.
- **Smart/serial-bus servo memory** — servo IDs, offsets, and calibration stored inside each servo (e.g. Feetech/Waveshare ST3215) are not in the ESP32 flash.
- **SD card contents** — motion routines, audio, maps, etc.
- **eFuses** — MAC address, chip calibration, security keys. These stay on the physical chip. Restoring to the same board keeps them; a different board will not inherit them.
- **Cloud-side account/activation state** — the vendor server may bind activation to the MAC/serial, so a different board may need reactivation.
- **Volatile state** — current motor position, LED state, RAM contents. These are recreated at boot.
- **Source code** — the dump is compiled binary. It cannot be edited to change motor/LED behavior.

## Hardware implications

| Hardware | Included in ESP32 dump? | Notes |
|---|---|---|
| DRV8833 / TB6612 / L298N motor driver | Yes | Driver is a power stage; ESP32 generates the control signals |
| Plain GPIO/PWM LEDs | Yes | Code and pin config are in the app |
| WS2812 / NeoPixel strip | Yes | No firmware inside the LEDs |
| Serial-bus servo (Feetech, ST3215, etc.) | **No** | IDs/offsets/calibration live in each servo |
| Separate MCU motion controller | **No** | Only the ESP32's command code is included |
| Smart LED controller with its own MCU | **No** | Its firmware is separate |
| Camera module with its own firmware | **No** | Only the ESP32's camera driver/config |
| SD card with motion/audio files | **No** | Back up the card separately |

## Restore

Put the board in download mode (hold **BOOT/GPIO0**, tap **RESET** or replug USB), then:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  write-flash 0x0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

This restores the exact snapshot, including NVS and the OTA slots, if present.

Caveats:

- Use the same chip and flash size. With encryption disabled, the image can be written to another ESP32-S3 with 16 MB flash, but the MAC/eFuse calibration will differ.
- If NVS encryption is enabled (not yet confirmed), NVS may be tied to eFuse keys or an `nvs_keys` partition. The full dump captures the partition but not eFuse keys.
- A full dump restore also restores the old Wi-Fi credentials and activation state.

## Flashing custom firmware without losing Wi-Fi

Writing `build/merged-binary.bin` at `0x0` can overwrite the NVS partition, erasing Wi-Fi credentials and settings. The board READMEs in this repo explicitly warn about this (e.g. `main/boards/m5stack/cardputer/README.md`).

On this device the layout is also different: the custom `id` partition at `0x800000` holds the device ID, and `assets` starts at `0x801000`. A merged image built with the repo's default 16 MB table may not preserve that layout. Use a matching partition table and preserve the `id` partition.

Preferred options:

1. Flash individual images (bootloader, partition table, `ota_data_initial.bin`, app, assets) at their documented offsets. This preserves NVS.
2. Or use `idf.py -p PORT flash`, which normally writes bootloader/partition table/app without erasing NVS.
3. If using a merged image, accept that NVS may be wiped, then restore the NVS partition from the backup if needed.

## Customization path

The dump is useful for rollback and identification, not for editing. To change behavior, rebuild from source:

```sh
python3 scripts/build.py --list-boards
python3 scripts/build.py <board-directory> --name <variant>
```

`qebabe-xiaoche` is **not** in this repo, so `--list-boards` will not show it. You need the vendor's source or to add a new board variant. The repo has `CompactWifiBoard` and `main/boards/common/lamp_controller.h`, which this firmware appears to use, but the board-specific pins/config live in the vendor fork. The dump can identify the app version, partition layout, and feature strings, but it cannot recover the original source or pin definitions. The vendor firmware was also built with ESP-IDF `v5.5.2-dirty`, not the repo's IDF 6.x target.

Keep a known-good set of artifacts:

- full flash dump + SHA-256
- `flash_id` and `espefuse summary` output
- git commit hash
- exact build command and `sdkconfig`
- `build/merged-binary.bin` and/or `releases/v{version}_{name}.zip`

Repo rules relevant to this robot:

- Do not change pins of an existing board to fit different hardware; add a uniquely named board or variant. Board identity affects OTA compatibility.
- Core code depends on `Board` interfaces, not a concrete board class.
- `DECLARE_BOARD(...)` must be unique per build.
- OTA rollback is enabled by default in `sdkconfig.defaults` (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`); `Ota::MarkCurrentVersionValid()` calls `esp_ota_mark_app_valid_cancel_rollback()`. This protects against a bad OTA update, not a bad USB flash.

## OTA warning

The default OTA URL in `main/Kconfig.projbuild` is:

```text
https://api.tenclass.net/xiaozhi/ota/
```

If the robot still uses this, a vendor update can overwrite custom firmware. Point `CONFIG_OTA_URL` at your own server or disable the version check. Also, changing the board name/identity can make OTA serve the wrong firmware or reject the device.

## Open questions / next steps

Completed:

- Full 16 MB backup captured and size checked; SHA-256 recorded.
- Partition table parsed (see above).
- App version/project/IDF identified (`xiaozhi` 2.1.0, IDF `v5.5.2-dirty`).
- Board identified from app strings: `qebabe-xiaoche`.
- Motor/LED control confirmed on the ESP32 side: LEDC PWM + `LampController`; MCP tools present.

Still open:

1. Optional: run `verify-flash` or a second full read and compare SHA-256.
2. Obtain the vendor's `qebabe-xiaoche` source or reconstruct the board config (pins, display, motor channels) to build custom firmware. The board is not in this repo.
3. Check NVS encryption. No `nvs_keys` partition and eFuse key slots are `USER`, so NVS is likely unencrypted; confirm before relying on cross-chip restore.
4. Check for a separate motor/servo controller or SD card; none is evident, but inspect the hardware.
5. Test a restore only after verification; keep a second copy off-device.

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

# Restore
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  write-flash 0x0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

## References

- `partitions/v2/16m.csv` — 16 MB partition layout used by many S3 boards in this repo
- `sdkconfig.defaults` — OTA rollback setting
- `docs/custom-board.md` — board/variant build chain
- `main/boards/m5stack/cardputer/README.md` — warning about merged image overwriting NVS
- `main/boards/seeed/xiao-esp32s3-respeaker-xvf3800/README.md` — 8 MB S3 flashing example
