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

**Status at time of writing:** still running at 230400 baud (~48% after ~6 minutes). The earlier attempts at 921600 and 460800 aborted with serial corruption (`Invalid head of packet (0xFF)`, `Corrupt data, expected 0x1000 bytes...`). 230400 has been stable so far. Do not treat the backup as valid until it completes and is verified.

**Verification to run after completion:**

```sh
ls -l backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin   # expect 16777216 bytes
shasum -a 256 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

Ideally read a second copy and compare hashes, or run:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  verify-flash 0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

The dump contains NVS, which may include Wi-Fi credentials, activation data, and server settings. **Treat the `.bin` as a secret.** It is covered by the repo's `*.bin` ignore rule, but it should still be stored somewhere private.

## What the full dump contains

A full 16 MB read is a byte-for-byte image of the ESP32's external flash:

- bootloader
- partition table
- `otadata` (which OTA slot is active)
- the OTA application slot(s) defined by the partition table (typically `ota_0` and `ota_1`)
- any assets partition (voice prompts, wake-word models, fonts, images)
- the `nvs` partition, if present (typically Wi-Fi credentials, device settings, activation/pairing state, and calibration values stored by the app)
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

1. Verify the completed backup: exact size, SHA-256, ideally a second read or `verify-flash`.
2. Parse the partition table from the dump at offset `0x8000` and confirm the actual layout (NVS size, OTA slots, assets).
3. Search the app binary for board name, firmware version, and `config.json` identifiers to identify the exact board/variant.
4. Check whether the device has an `nvs_keys` partition or NVS encryption enabled.
5. Identify whether motors/LEDs are driven directly by the ESP32 or by a separate controller; back up any separate controller/servo/SD-card state if present.
6. Test a restore only after the dump is verified; keep a second copy off-device.

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
