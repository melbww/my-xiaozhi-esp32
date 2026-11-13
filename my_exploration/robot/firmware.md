# Desk Bot 01 — Firmware Build, Flash, and Recovery

**Last verified:** 2026-11-13
**Hardware details:** [hardware.md](hardware.md)
**Server:** [../server/README.md](../server/README.md)

## Build environment

- ESP-IDF `release/v6.1` at `/Users/wenwu/esp/esp-idf`
- ESP-IDF `6.1.0`; Xtensa GCC `15.2.0`
- CMake `4.4.5`; Ninja `1.13.2`
- App version `2.5.3`

```sh
. /Users/wenwu/esp/esp-idf/export.sh
cd /Users/wenwu/work/my-xiaozhi-esp32
python3 scripts/build.py desk-bot-01 --name desk-bot-01
```

The build generates `build/xiaozhi.bin` (~2.4 MB), bootloader, partition table,
and assets. It also generates `build/merged-binary.bin`, but **do not flash it
at `0x0`** (see “Do not do”).

## Board configuration chain

`main/boards/desk-bot-01/config.json` sets:

```text
CONFIG_BOARD_TYPE_DESK_BOT_01=y
CONFIG_OLED_SSD1306_128X64=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="main/boards/desk-bot-01/partitions.csv"
CONFIG_OTA_URL="http://192.168.1.6:8002/xiaozhi/ota/"
CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y
CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=n
CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0
CONFIG_USB_UVC_INIT_TIMEOUT_MS=500
```

`CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0` closes the UVC stream as soon as a
photo is captured; `CONFIG_USB_UVC_INIT_TIMEOUT_MS=500` (the range minimum) caps
the boot-time wait for a camera that is not attached yet. See
[../camera/README.md](../camera/README.md) for the on-demand streaming and
hot-plug behavior.

The OTA URL points at the self-hosted manager-api. Change it if the server host
or LAN IP changes, then rebuild and reflash.

## Flash (preserves NVS and `id`)

`idf.py flash` hit a Python-path mismatch (`python` vs `python3`) in this
environment, so the images are written directly with `esptool`:

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

This writes only bootloader, partition table, `otadata`, app, and assets. It
does **not** write NVS (`0x9000`–`0xD000`) or `id` (`0x800000`).

## Serial monitor and boot capture

```sh
# Interactive monitor
idf.py -p /dev/cu.usbmodem5D300345091 monitor
```

Non-interactive capture with reset (IDF Python env has pyserial):

```sh
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 \
  my_exploration/tools/serial_capture.py /dev/cu.usbmodem5D300345091 20
```

Expected current boot path:

- `SKU=desk-bot-01`, SSD1306/LVGL initialized
- `NoAudioCodec: Simplex channels created`
- 30 MCP tools registered (23 visible to the model, 7 user-only)
- Wi-Fi from NVS; OTA from `192.168.1.6:8002`
- wake word `Hi,ESP`; WebSocket to `192.168.1.6:8000`
- TTS playback through the local `say` service

## MCP tools

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
self.rgb_led.set_color
self.rgb_led.turn_off
self.rgb_led.restore_default_idle
```

Not implemented:

- C3 2.2.x `self.motor.sequence` / advanced `self.car.act` action sequences
  (not present in the S3 2.1.0 dump). Vendor C3 release notes are in
  [../.archive/vendor/c3-2.2-release-notes.md](../.archive/vendor/c3-2.2-release-notes.md).
- Fall detection for `move_forward` / `quick_forward` (no edge/distance sensor).
- The vendor `self.rgb_led.*` names; this board exposes the WS2812 strip as
  `self.lamp.*` plus the added `self.rgb_led.*` tools.

### Tool parameters

| Tool | Parameters |
|---|---|
| `self.motor.drive` | `steer` −100..100; `throttle` −100..100; `duration_ms` 0..15000 (0 = continuous) |
| `self.motor.stop` | none |
| `self.motor.move_forward` / `move_backward` | `speed_percent` 0..100 (default 100); `duration_ms` default 5000 |
| `self.motor.spin_around` | `speed_percent` 0..100; `duration_ms` default 1000 |
| `self.motor.turn_left` / `turn_right` | `speed_percent` 0..100; `duration_ms` default 600 |
| `self.motor.quick_forward` | `speed_percent` 60..100; `duration_ms` default 1000 |
| `self.motor.quick_backward` | `speed_percent` 0..100; `duration_ms` default 1000 |
| `self.motor.wiggle` | `speed_percent` 0..100; `duration_ms` default 300 |
| `self.motor.dance` | `speed_percent` 0..100 (forward briefly) |
| `self.motor.forward` / `backward` | `speed` 0..100; `duration_ms` default 1000 |
| `self.car.act` | `action` forward/backward/turn_left/turn_right/spin/stop; `speed` 0..100; `duration_ms` default 1000 |
| `self.lamp.*` | get_state/turn_on/turn_off; 7-LED WS2812 strip |
| `self.rgb_led.*` | set_color/turn_off/restore_default_idle |

## Why Wi-Fi and UUID survive a reflash

The flash command writes around NVS and `id`:

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

- Wi-Fi credentials, UUID, activation/MQTT config, and settings come from the
  vendor NVS.
- The hostname `Xiaozhi-CE50` is derived from the Wi-Fi MAC.
- The board type `desk-bot-01` comes from our `config.json`/`BOARD_TYPE`.
  Renamed from `qebabe-xiaoche` on 2026-11-09; builds and boot logs from
  before that date show the old identity.
- The device ID `RUK5IES0L128X` remains in the `id` partition.

### Erase only NVS (optional clean provisioning)

```sh
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 \
  erase-region 0x9000 0x4000
```

This removes Wi-Fi, UUID, and saved settings; the robot should enter the
`Xiaozhi-XXXX` Wi-Fi config hotspot. The `id` partition and assets remain.

## Validation

- `python3 scripts/build.py --list-boards` shows `desk-bot-01` (esp32s3).
- `gen_esp32part.py` validates `main/boards/desk-bot-01/partitions.csv`.
- `python3 -m unittest discover -s scripts/tests` passes with a `python` shim
  (the CI script invokes `python`, which is not on PATH on this Mac).
- `clang-format --dry-run -Werror` passes for the board C/C++ files.
- Boot log confirms display, audio, Wi-Fi, OTA, WebSocket, wake word, and TTS.

## Recovery

Enter download mode (hold **BOOT**, tap **RESET**, release BOOT), then reflash
the individual images. If the bootloader/partition table is damaged, restore the
full dump:

```sh
uvx --from esptool esptool --chip esp32s3 \
  -p /dev/cu.usbmodem5D300345091 -b 230400 \
  write-flash 0x0 backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin
```

## Troubleshooting: boot loop or instant panic

Symptoms (seen 2026-11-13):

- ROM boot loop: `Checksum failure. Calculated 0x45 stored 0xe1` / `ets_main.c 329`,
  with `rst:0x10 (RTCWDT_RTC_RST)` or `rst:0x7 (TG0WDT_SYS_RST)`. The bootloader in
  flash no longer matches its checksum.
- After fixing the bootloader, the app may panic at startup: `LoadProhibited`,
  `EXCVADDR: 0x0000000d`, registers full of `0x5a5a5a5a`/`0xa5a5a5a5`, and a backtrace
  that resolves to code impossible for that path (e.g. `AudioService::...` inside `nvs_open`).

Cause: single-bit corruption of flash contents (this incident: 128 bytes in the 2.6 MB
app, 3 bytes in the 16 KB bootloader). It is **not** a source regression — the ELF and
backtrace do not match what ran. Partition table, assets, NVS, and `id` were intact.

Diagnose by reading back and diffing against the build output, then reflash:

```sh
ESPTOOL="/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem5D300345091 -b 230400 \
  --before default-reset --after hard-reset"
$ESPTOOL read-flash 0x0 0x40a0 /tmp/boot.bin
$ESPTOOL read-flash 0x20000 0x282000 /tmp/app.bin
cmp /tmp/boot.bin build/bootloader/bootloader.bin
cmp -n "$(stat -f%z build/xiaozhi.bin)" /tmp/app.bin build/xiaozhi.bin
```

Reflash with the commands above; `esptool` prints `Hash of data verified` per write, and a
fresh readback must diff to zero. Then confirm the boot log reaches `Startup wake word hint`.

Notes:

- Stay at `-b 230400`. At `-b 921600` this CH343 link failed mid-read with
  `Corrupt data, expected 0x1000 bytes but received 0xfbb`; 230400 is repeatable.
- If corruption returns after a verified reflash, suspect flash chip or board power.
- Camera HTTP 500 `Capture failed (is the camera plugged in?)` just means no UVC camera.

## Do not do

- Do not flash `build/merged-binary.bin` at `0x0`; it can overwrite `id`/NVS.
- Do not flash `vendor/from_vendor/2.5ai_voice_display_oled_dog.ino.merged.bin`;
  it is a different project/partition layout.
- Do not run `idf.py erase-flash` unless you intend to erase everything.
- Do not write NVS (`0x9000`–`0xD000`) or `id` (`0x800000`) unless restoring
  the backup or deliberately re-provisioning.
- Do not change pins of an existing board; add a new variant.

## Takeover checklist

1. Read [../README.md](../README.md), this file, and
   `main/boards/desk-bot-01/README.md`.
2. `ls /dev/cu.*`; confirm CH343 `1a86:55d3`.
3. `. /Users/wenwu/esp/esp-idf/export.sh`; confirm `idf.py --version` is 6.1.
4. `git status`; preserve unrelated worktree changes.
5. Build: `python3 scripts/build.py desk-bot-01 --name desk-bot-01`.
6. Flash individual images with `esptool` (preserves `id`/NVS).
7. Capture the boot log; confirm `SKU=desk-bot-01`, display, audio, Wi-Fi,
   OTA, WebSocket.
8. Test motors/LED; adjust direction/PWM if needed.
9. Keep the 16 MB backup; do not erase `id`/NVS.
