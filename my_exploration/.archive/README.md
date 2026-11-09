# Archive

**This material is historical/superseded. Do not treat it as the current
setup.** Start at [../README.md](../README.md).

The archive keeps earlier exploration for traceability without making the
active docs long. Paths inside archived docs may refer to the pre-archive
layout; use the active docs for current commands.

Archived material also keeps the board's old name, `qebabe-xiaoche`. On
2026-11-09 the active board definition was renamed to `desk-bot-01`; read the
old name in anything below as a historical alias.

## Contents

| Path | What it is |
|---|---|
| [history/2026-11-bringup-log.md](history/2026-11-bringup-log.md) | Full 2026-11-04 → 2026-11-08 bring-up narrative |
| [history/build-qebabe.log](history/build-qebabe.log) | Build log from the pre-rename board definition |
| [reference/README.md](reference/README.md) | Early local-server guide, background research, architecture parable |
| [vendor/reverse-engineering.md](vendor/reverse-engineering.md) | Vendor binary extraction/disassembly and pin recovery |
| [vendor/web-ui-api.md](vendor/web-ui-api.md) | Vendor LAN web UI/API notes (not implemented) |
| [vendor/c3-2.2-release-notes.md](vendor/c3-2.2-release-notes.md) | C3 2.2.x variant notes (different chip) |
| [camera/usb-host-test.md](camera/usb-host-test.md) | USB-host test, build recipe, detailed camera formats |
| `camera/images/` | Product/spec screenshots for the USB camera |
| `vendor-artifacts/` | Vendor app binary, IROM disassembly, web snapshots (large/local) |
| `vendor-assets/` | Large vendor RAR and incompatible robot-dog image |

## Active docs

- [../README.md](../README.md) — index/current state
- [../robot/hardware.md](../robot/hardware.md)
- [../robot/firmware.md](../robot/firmware.md)
- [../server/README.md](../server/README.md)
- [../server/tts.md](../server/tts.md)
- [../camera/README.md](../camera/README.md)
- [../vendor/README.md](../vendor/README.md)
- [../MAINTENANCE.md](../MAINTENANCE.md)

## Restoring something

If an archived doc becomes active again, move it back with `git mv`, update its
links/`Last verified` date, and add it to [../README.md](../README.md). Do not
edit archived docs in place for current facts.
