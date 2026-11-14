# Desk Bot 01 ESP32-S3 — Exploration Index

This folder documents our **Desk Bot 01**, a vendor robot car (ESP32-S3
N16R8) that was backed up, reverse-engineered, and reflashed with a custom
`desk-bot-01` board definition in this repository. It also covers the
self-hosted `xiaozhi-esp32-server` stack, TTS tuning, and the USB camera work.

**Last verified:** 2026-11-13

## Current state

| Area | Status |
|---|---|
| Hardware | Identified; full 16 MB flash backup taken |
| Firmware | `desk-bot-01`, app `2.5.3`, ESP-IDF 6.1 |
| OTA / WebSocket | Self-hosted server, `192.168.1.6:8002` / `:8000` |
| TTS | Local macOS `say` via `CustomTTS` (fast); Edge TTS available |
| `digital-human` runtime | Stopped; **not part of the robot path** |
| Camera / vision | USB UVC camera (external 5 V), hot-pluggable; stream opens on demand and closes right after (idle timeout `0`); `take_photo` returns the image to the main multimodal LLM; legacy `ask_vision` VLM tool is opt-in and off; `http://<robot-ip>/photo.jpg` serves a snapshot |
| Motors / LED | Not yet verified on hardware |
| Wi-Fi / OLED / audio | Verified in boot logs; OLED visual check pending |

## Quick start

Build and flash the firmware:

```sh
cd /Users/wenwu/work/my-xiaozhi-esp32
. /Users/wenwu/esp/esp-idf/export.sh
python3 scripts/build.py desk-bot-01 --name desk-bot-01
```

See [robot/firmware.md](robot/firmware.md) for the flash command that preserves
NVS and the vendor `id` partition.

Start the self-hosted server (separate repo):

```sh
cd /Users/wenwu/work/xiaozhi-esp32-server
my_explorations/server/iac/docker-stack.sh up
my_explorations/local-say-tts/local-say-tts.sh start
```

See [server/README.md](server/README.md) and [server/tts.md](server/tts.md).

## Documentation map

| Doc | What it covers |
|---|---|
| [robot/hardware.md](robot/hardware.md) | Device ID, USB ports, backup, partitions, verified pin map |
| [robot/firmware.md](robot/firmware.md) | Build, flash, recovery, takeover checklist |
| [robot/mcp-tools.md](robot/mcp-tools.md) | Full MCP tool inventory: names, parameters, defaults, visibility |
| [server/README.md](server/README.md) | Self-hosted stack, OTA/WS, activation, logs |
| [server/tts.md](server/tts.md) | TTS options, latency diagnosis, local `say`, barge-in |
| [camera/README.md](camera/README.md) | USB UVC camera, firmware support, vision pipeline |
| [camera/take-photo.md](camera/take-photo.md) | `take_photo` prompt chain, HTTP contract, return envelope, errors |
| [docs/framework-layers.md](docs/framework-layers.md) | Build/runtime/NVS/cloud layers; why Wi-Fi, display, and wake word behave as they do |
| [vendor/README.md](vendor/README.md) | Vendor BOM/wiring and inventory |
| [.archive/README.md](.archive/README.md) | Archived deep dives, history, background research |
| [MAINTENANCE.md](MAINTENANCE.md) | How to keep these docs accurate |

## Critical rules

- **Do not** flash `build/merged-binary.bin` at `0x0`; it can overwrite the
  vendor `id` partition and NVS.
- **Do not** flash `.archive/vendor-assets/2.5ai_voice_display_oled_dog.ino.merged.bin`;
  it is a different project/partition layout.
- **Do not** run `idf.py erase-flash` unless you intend to erase NVS, the `id`
  partition, and assets.
- **Do not** write NVS (`0x9000`–`0xD000`) or `id` (`0x800000`) unless restoring
  the backup or deliberately re-provisioning.
- Treat the 16 MB backup as a secret: it contains Wi-Fi credentials and
  activation data.
- Do not change pins of an existing board; add a new variant.

## Related code

- `main/boards/desk-bot-01/` — board definition and README
- `main/Kconfig.projbuild`, `main/CMakeLists.txt` — board selection chain
- `/Users/wenwu/work/xiaozhi-esp32-server` — self-hosted server
- `my_explorations/local-say-tts/` — local macOS TTS service (server repo)

## Naming history

The board definition was named `qebabe-xiaoche` before 2026-11-09. It was
renamed to `desk-bot-01` everywhere (board directory, `BOARD_TYPE`, build
variant, MCP/board identity, and docs). The old name now appears only in
historical material under [.archive/](.archive/), including
`.archive/history/build-qebabe.log`; vendor release names such as
`qebabe-xiaoche-c3` keep the vendor's own slug.
