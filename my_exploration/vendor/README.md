# Vendor Source Material

**Last verified:** 2026-11-09
**Parent index:** [../README.md](../README.md)

This folder keeps the vendor material that is still useful for current work:
the BOM and the accurate wiring diagram. The vendor’s original
`qebabe-xiaoche` source was not available; the board definition was
reconstructed from these plus the vendor binary.

## Inventory

| Path | What it is |
|---|---|
| `from_vendor/小智AI机器人配件清单.txt` | BOM: ESP32-S3 N16R8, L298N, INMP441, MAX98357, 0.96" OLED, 1S protection, 5 V boost, switch, 2415 speaker, 2× N20 motors, 2× 16340 cells |
| `from_vendor/小智AI版机器人接线图.jpg` | Accurate wiring diagram for this robot |

The verified pin map is in [../robot/hardware.md](../robot/hardware.md).

## Archived vendor material

The deep-dive vendor analysis and large artifacts are in `.archive/`:

| Path | What it is |
|---|---|
| [../.archive/vendor/reverse-engineering.md](../.archive/vendor/reverse-engineering.md) | How the vendor app was disassembled and how the real motor/LED pins were recovered |
| [../.archive/vendor/web-ui-api.md](../.archive/vendor/web-ui-api.md) | Vendor LAN web UI, joystick/action APIs, LED G-code, music service |
| [../.archive/vendor/c3-2.2-release-notes.md](../.archive/vendor/c3-2.2-release-notes.md) | C3 2.2.x release notes (different variant) |
| `../.archive/vendor-artifacts/` | Vendor app binary, IROM disassembly, web snapshots (local/large) |
| `../.archive/vendor-assets/` | Large vendor RAR and the incompatible robot-dog image |

## Warnings

- **Do not flash** `.archive/vendor-assets/2.5ai_voice_display_oled_dog.ino.merged.bin`.
  It is a different project/partition layout.
- Do not flash the vendor application binary over the S3 robot; it is for
  analysis and a different build/partition configuration.
- The C3 2.2.x notes describe a **different chip variant**
  (`qebabe-xiaoche-c3`, 4 MB flash, factory partition). Do not apply its pin map
  or flash layout to this S3 robot.

## First diagram caveat

The first wiring image (`IMG_2133.JPG`) included LiDAR/VL53L0X and was **not**
accurate for this unit. The accurate diagram shows no LiDAR, no VL53L0X, and no
built-in camera. An external USB UVC camera is supported by the board code but
is currently disabled; see [../camera/README.md](../camera/README.md).
