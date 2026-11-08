# Vendor Material, Reverse Engineering, and Web UI

**Last verified:** 2026-11-09
**Parent index:** [../README.md](../README.md)

This folder holds the vendor source material and the analysis of the vendor
firmware. The vendor’s original `qebabe-xiaoche` source was not available, so
the board definition was reconstructed from the BOM/diagram and the vendor
binary.

## Inventory

| Path | What it is |
|---|---|
| `from_vendor/小智AI机器人配件清单.txt` | BOM: ESP32-S3 N16R8, L298N, INMP441, MAX98357, 0.96" OLED, 1S protection, 5 V boost, switch, 2415 speaker, 2× N20 motors, 2× 16340 cells |
| `from_vendor/小智AI版机器人接线图.jpg` | Accurate wiring diagram for this robot |
| `from_vendor/2.5ai_voice_display_oled_dog.ino.merged.bin` | 16 MB Arduino merged image for a **different** project (robot dog); **do not flash** |
| `assets/ESP32-S3 CAM开发板资料.rar` | Generic Freenove ESP32-S3 CAM board materials; useful for board hardware, not the qebabe source |
| `artifacts/vendor_app.bin` | Vendor application extracted from the 16 MB dump |
| `artifacts/irom.bin` / `irom.asm` | IROM slice and full disassembly used for pin recovery |
| `artifacts/web/` | Vendor web UI snapshots |

The `id` partition contains the vendor device ID `RUK5IES0L128X`; it is
preserved by the custom flash procedure. See
[../robot/hardware.md](../robot/hardware.md).

Some artifacts are **local-only** and may not be committed: `vendor_app.bin`,
`irom.bin`, `irom.asm`, and `assets/*.rar` are large analysis/source files.
Regenerate the disassembly with [reverse-engineering.md](reverse-engineering.md);
the small `artifacts/web/*.html` snapshots are tracked for reference.

## Documents

| Doc | Contents |
|---|---|
| [reverse-engineering.md](reverse-engineering.md) | How the vendor app was extracted and disassembled; motor/LED pin recovery |
| [web-ui-api.md](web-ui-api.md) | Vendor LAN web UI pages, joystick/action APIs, LED G-code, music service |
| [c3-2.2-release-notes.md](c3-2.2-release-notes.md) | Notes from the C3 2.2.x release (different variant): action sequences, distance sensor, bug fixes |

## Warnings

- **Do not flash** `from_vendor/2.5ai_voice_display_oled_dog.ino.merged.bin`.
  It is a different project/partition layout.
- The `artifacts/` binary is for analysis only. Do not flash it over the S3
  robot; the vendor app is for a different build/partition configuration.
- The vendor web UI and music backend are documented for reference; the custom
  firmware does not implement them.
- The C3 2.2.x notes describe a **different chip variant** (`qebabe-xiaoche-c3`,
  4 MB flash, factory partition). Do not apply its pin map or flash layout to
  this S3 robot.

## First diagram caveat

The first wiring image (`IMG_2133.JPG`) included LiDAR/VL53L0X and was **not**
accurate for this unit. The accurate diagram shows no LiDAR, no VL53L0X, and no
built-in camera. An external USB UVC camera is supported by the board code but
is currently disabled; see [../camera/README.md](../camera/README.md).
