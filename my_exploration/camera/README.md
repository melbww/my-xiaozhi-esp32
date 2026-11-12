# Camera and Vision — Desk Bot 01 (USB UVC)

> Part of the [exploration index](../README.md). Current robot/server status:
> [../README.md](../README.md) and [../server/README.md](../server/README.md).

All camera/vision findings for the Desk Bot 01 robot: the camera module, USB
wiring/power, firmware UVC support, the vision upload path, and the server-side
options.

## Current status (2026-11-13)

- The camera is a **4-pin USB 2.0 UVC module** (1 MP, 1280×720 / 480×320,
  YUV/MJPEG, 5 V) with a 4-wire cable ending in a **Type-C male** plug.
- **The camera module is confirmed working (2026-11-11).** Through a USB-C
  OTG-style adapter on the Mac it enumerates as `HD video`
  (`0x349c:0x3307`) plus `HD audio` (UAC mic) and delivers live 1280×720
  frames. The module, its cable and its wiring are all good.
- Root cause of the earlier "Mac sees nothing": the camera's Type-C plug has no
  CC pull-downs, so a **C-to-C** link never enables VBUS. An OTG adapter (or a
  USB-A host port) works around it completely.
- **The camera now works on the robot (2026-11-11).** VBUS is spliced into the
  OTG cable; the host then enumerates it at boot and on hot-plug. See
  [Verified fix](#verified-fix-vbus-splice-in-the-otg-cable).
- **Power source is currently external (2026-11-11)**: feeding the splice from
  the board's 5 V did not work, so the camera runs from a third-party 5 V
  supply. The board 5 V feed is still to be found — use the amp `VIN` node as
  the measured 5 V reference, not a silkscreen label.
- Reported UVC modes from the device itself: MJPEG 1280×720@15, 800×480@20,
  640×480@25, 480×320@25; default 1280×720@30 (bulk, 3×10 KiB transfers).
  It also exposes a **UAC mic (iface 3) and speaker (iface 4)**.
- The OTG port still does **not** supply VBUS on its own: each USB-C port's VBUS
  goes through a Schottky diode *into* the 5 V rail (DevKitC-style), so it can
  power the board but not a downstream device.
- Firmware UVC support is present (`CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`,
  `EspVideo`, `GetCamera()`). `InitializeCamera()` is now called in the board
  constructor; without an enumerating camera `esp_video` blocks boot for ~10 s
  and then fails to open `/dev/video40`.
- With no camera attached the board still boots normally (after the timeout),
  registers 30 MCP tools, and does **not** expose `self.camera.take_photo`.
- **Status icon (2026-11-13):** the 128×64 OLED top bar shows a
  `photo_camera` icon immediately left of the Wi-Fi icon only while the camera is
  connected and streaming. `EspVideo` runs a 1 s probe using `VIDIOC_G_PARM`,
  which the UVC driver fails with `ENODEV` once it clears the device address on
  `UVC_HOST_DEVICE_DISCONNECTED`. `Camera::IsStreaming()` requires `STREAMON` to
  have succeeded, the probe to report connected, and no latched disconnect, so
  unplugging the camera turns the icon off within ~1 s. After a reconnect the
  icon stays off because this driver cannot restart the stream without reopening
  the device (reboot to restore the camera).
- During testing the camera occasionally dropped off the USB bus by itself
  (`uvc_video_get_parm: UVC device ... is not connected` in the log). The icon
  turned off as expected. If that happens repeatedly, recheck the external 5 V
  feed and the VBUS splice — the driver only clears `dev_addr` on a real
  `DEV_GONE` event.
- The local `xiaozhi-esp32-server` vision backend still needs a real VLLM key;
  the Mac's LAN IP is `192.168.1.6`, the robot is `192.168.1.13`.

## Camera module

Product screenshots (product page, Chinese):

```
my_exploration/.archive/camera/images/IMG_2143.PNG
my_exploration/.archive/camera/images/IMG_2145.PNG
my_exploration/.archive/camera/images/IMG_2146.PNG
my_exploration/.archive/camera/images/IMG_2147.PNG
```

Specs from the page:

| Item | Value |
|---|---|
| Sensor | 1 MP, M5-CSP lens |
| Focal length | 2.07 ± 5% mm |
| Aperture | F2.4 ± 5% |
| FOV | 65° |
| Distortion | <5% |
| Focus range | 5 cm – 20 cm |
| Resolution | 1280×720 / 480×320 |
| Output | YUV / MJPEG |
| Interface | USB 1.1 / USB 2.0 |
| Supply | DC 5 V |
| Module size | 26 mm × 10.5 mm |
| Connector | 4-pin header → 4-wire cable → Type-C male |

The vendor markets it as “ESP32-S3 optimized, plug and play, standard UVC”.
The module itself is a standard UVC camera; the Type-C cable is the only
unusual part.

### Confirmed USB identity and modes (2026-11-11, on macOS)

| Item | Value |
|---|---|
| Product name | `HD video` (camera), `HD audio` (UAC mic) |
| VID:PID | `0x349c:0x3307` |
| Video formats | `uyvy422`, `yuyv422`, `nv12`, `0rgb`, `bgr0` |
| Video modes | 480×320@25/15, 640×480@25/15, 800×480@20/15, 1280×720@15/10 |

AVFoundation exposes no MJPEG, so frame capture on the Mac uses `uyvy422`:

```sh
ffmpeg -f avfoundation -framerate 15 -video_size 1280x720 -i "1:none" \
  -frames:v 1 -y cam.jpg
```

Nothing here needs a `self.camera.take_photo` capability until the board can
power the camera.

### 4-pin wiring

Typical wire order for these modules:

| Wire | Signal |
|---|---|
| Red | 5 V |
| Black | GND |
| Green | D+ |
| White | D− |

Verify continuity against the module's PCB labels before applying power; some
modules use a different order (5V/D−/D+/GND).

## USB ports and power

The Desk Bot 01 board has two USB-C ports:

| Port | Chip | Use |
|---|---|---|
| `UART / COM` | CH343 USB-to-UART | Flashing, serial console (GPIO43/44) |
| `USB / OTG` | ESP32-S3 native USB | USB-Serial/JTAG, USB device, USB host (GPIO19/20) |

A USB camera **must** use the native `USB / OTG` port. The CH343 UART port is a
USB-to-UART bridge and cannot host a camera.

Two power/CC issues apply:

1. **Camera Type-C plug:** many cheap UVC modules omit the 5.1 kΩ CC pull-down
   resistors. A Mac USB-C port will not enable VBUS or enumerate such a device.
   The ESP32-S3 does not implement USB-C PD/CC either, so the same issue can
   occur there.
2. **Board VBUS:** the native USB-C port may be wired device-only and may not
   supply 5 V to a camera. The 1S→5 V boost may also sag under camera load.

Recommended wiring for both Mac testing and the robot:

```
camera Type-C male
    -> USB-C female / USB-A male adapter
    -> powered USB-A hub
    -> Mac USB-A port   (for testing)
       or ESP32-S3 USB/OTG port via a USB-A→USB-C adapter (for the robot)
```

The powered hub supplies the camera's 5 V; the ESP32-S3 only needs D+/D− and
GND. This bypasses both the missing CC resistors and the board's VBUS limits.

### Verified fix: VBUS splice in the OTG cable

Proven working 2026-11-11. The C-male→A-female OTG cable is opened mid-run and
only its VBUS wire is diverted; every other conductor is left intact so the
cable still plugs into the board's `USB / OTG` port normally.

```
external 5 V ── red (camera side of the cut) ── A-female VBUS ── camera
               red (plug side of the cut)   = cut, insulated, unused
GND            ─────────────────────────────  intact through the C plug
white D− / green D+ ────────────────────────  intact through the C plug
```

- Feed the **camera side** of the red wire. The plug side sits behind the board's
  Schottky diode, so feeding it delivers nothing.
- Do not cut D+/D−. A disturbed or swapped pair shows up as
  `E (1252) HUB: Root port reset failed` — the host sees a connection, then the
  port reset fails. A clean pair enumerates immediately.
- Ground needs no extra wire: the cable's black wire already ties the camera to
  the board's ground plane.
- Verify before plugging the camera in: 5.0–5.2 V between A-female VBUS and the
  shell. Not 9 V — never use a QC/PD fast-charge port as the source.
- If a mains charger is the source, prefer a **power bank** (floating, no
  leakage path) and unplug the Mac's own charger while wiring; the CH343
  `UART / COM` link is the path that would carry a fault into the Mac.

Boot log with the fix applied (`usb_camera_mic_spk` demo):

```
W (1325) app_usb_host: USB bus: device 1 at address 1
I (1325) app_uvc: Handling UVC connect addr=1 stream_index=0 frame_num=4
I (1326) app_uvc: Pick UVC MJPEG 1280x720@15.0fps
I (1334) app_uac: mic connected addr=1 iface=3 alt_num=1
I (1380) app_uvc: UVC idle stream opened 480x320@25.0fps
```



The board's `USB / OTG` receptacle is **USB-C female**, so the hub must reach it
through a C-male connector:

```
board USB-C female
  <- C-male → A-female "OTG" cable        (also supplies Rd on CC; already proven on the Mac)
  <- hub's captive USB-A male upstream cable
  <- self-powered USB-A hub (4-port, 5 V DC barrel-jack adapter)
  <- hub downstream A port
  <- C-female → A-male adapter
  <- camera Type-C male
```

Requirements for the hub:

- **Self-powered.** A bus-powered hub takes its 5 V from the upstream port, and
  the board's port supplies none → nothing works. A hub with a USB-C "PD in"
  meant for charging a laptop is a bad choice: it may only power its ports after
  PD negotiation, which the ESP32-S3 never performs. Use a DC barrel-jack hub.
- USB 2.0 is enough (the camera is USB 2.0) and is the cheapest option.
- The upstream cable must be a **data** cable, not charge-only.

**Gotcha — hub support must be compiled in.** The USB host stack does not
support external hubs by default (`CONFIG_USB_HOST_HUBS_SUPPORTED` defaults to
`n`). ESP-IDF 6.x ships the host stack in the managed `espressif/usb`
component, so this is a build config, not a driver change:

```
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y
```

The `usb_camera_mic_spk` demo already sets both (its `sdkconfig.defaults`), so
a hub can be validated with the demo as-is. XiaoZhi's `desk-bot-01`
`config.json` does **not** set them yet — add them to `sdkconfig_append` before
expecting `esp_video` to see a camera behind a hub.

With the hub working, the demo log shows two devices (hub first, camera
second):

```
W (1430) app_usb_host: USB bus: device 1 at address 1
W (…) app_usb_host: USB bus: device 1 at address 1, device 2 at address 2
I (…) app_uvc: UVC device connected
```

Also note the console must stay on the CH343 `UART / COM` port: once the USB
host driver owns the OTG pins, the chip's USB-Serial/JTAG device disappears from
the Mac.


## Mac diagnostics (what was observed)

Commands:

```sh
system_profiler SPUSBDataType
system_profiler SPCameraDataType
ffmpeg -f avfoundation -list_devices true -i ""
ioreg -p IOUSB -w 0 -r -c IOUSBHostDevice
ls /dev/cu.*
```

Observed on 2026-11-05 with the camera connected directly to the Mac:

- `system_profiler SPUSBDataType` → no camera (empty list)
- `ioreg` → only a generic `USB 2.0 Hub` (`idVendor 0x1a40`, `idProduct 0x0101`)
- `ffmpeg` → only the built-in MacBook camera, iPhone Continuity Camera, Desk
  View, and screen capture
- `ls /dev/cu.*` → no new serial port

That was a **C-to-C** connection: the camera's Type-C plug has no CC
pull-downs, so the Mac never enabled VBUS.

Observed on 2026-11-11 through a **USB-C OTG-style adapter** into the same Mac
port:

- `ioreg` → `HD video`, `"USB Vendor Name" = "Generic"`, `0x349c:0x3307`
- `system_profiler SPCameraDataType` → `HD video` as
  `UVC Camera VendorID_13468 ProductID_13063` (13468 = 0x349C,
  13063 = 0x3307)
- `ffmpeg -f avfoundation -list_devices true -i ""` → video `[1] HD video`,
  audio `[1] HD audio`
- A single 1280×720 frame captured successfully → the sensor works

So the module is healthy; only the board side is still blocked (no VBUS).

## Firmware support

### Kconfig

`main/boards/desk-bot-01/config.json`:

```json
"CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y",
"CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=n"
```

`esp_video` is already a managed dependency (`espressif/esp_video`), and the
repo contains the USB UVC host driver.

### Board code

`main/boards/desk-bot-01/compact_wifi_board.cc`:

- `#include "esp_video.h"`
- `EspVideo* camera_ = nullptr;`
- `InitializeCamera()`:

```cpp
esp_video_init_usb_uvc_config_t usb_uvc_config = {
    .uvc = {
        .uvc_dev_num = 1,
        .task_stack = 4096,
        .task_priority = 5,
        .task_affinity = -1,
    },
    .usb = {
        .init_usb_host_lib = true,
        .task_stack = 4096,
        .task_priority = 5,
        .task_affinity = -1,
    },
};
esp_video_init_config_t video_config = {};
video_config.usb_uvc = &usb_uvc_config;
camera_ = new EspVideo(video_config);
```

- `GetCamera()` returns `camera_`.
- The constructor calls `InitializeCamera()` after the display and buttons are
  initialized. When no camera enumerates, `EspVideo` construction fails its
  `/dev/video40` open after the 10 s timeout, `camera_` remains non-null, and
  `IsStreaming()` stays false — so the OLED status icon stays hidden and the
  board still boots normally.

### Boot log with camera enabled (no camera attached)

```
I (354) usb_uvc_device: Waiting for UVC device to be enumerated...
E (10354) usb_uvc_device: uvc_video_init(266): Failed to take UVC device ready semaphore
E (10354) EspVideo: open /dev/video40 failed, errno=19(No such device)
I (10354) DeskBot01Board: USB UVC camera initialized
I (10504) MCP: Add tool: self.camera.take_photo
```

The 10 s wait is controlled by `CONFIG_USB_UVC_INIT_TIMEOUT_MS` (default
10000 ms, range 500–60000). If the camera is present, this wait is shorter.

### Boot log with camera disabled (current)

```
I (508) MCP: 30 tools registered: 23 visible to the model, 7 user-only
I (5478) WifiBoard: Connected to WiFi: ziroom904
I (6958) Application: Activation done
```

No `usb_uvc_device`, `EspVideo`, or `self.camera.take_photo`.

### Re-enabling

1. Uncomment `InitializeCamera();` in the board constructor.
2. Build and flash the app:
   ```sh
   python3 scripts/build.py desk-bot-01 --name desk-bot-01
   esptool --chip esp32s3 -p /dev/cu.usbmodem... -b 230400 \
     write-flash 0x20000 build/xiaozhi.bin
   ```
3. Connect the camera **before** booting.
4. Check the log for no `open /dev/video40 failed` and for
   `MCP: Add tool: self.camera.take_photo`.
5. Optional: lower `CONFIG_USB_UVC_INIT_TIMEOUT_MS` (e.g. 3000–5000) in
   `sdkconfig_append` to shorten the no-camera boot delay.

## Vision pipeline

The firmware already implements the xiaozhi vision flow:

1. `McpServer::AddCommonTools()` checks `Board::GetCamera()`. If non-null, it
   registers `self.camera.take_photo` with a `question` argument.
2. The server sends an MCP `initialize` message with
   `capabilities.vision = {url, token}`. `McpServer::ParseCapabilities()` reads
   it and calls `camera->SetExplainUrl(url, token)`.
3. When the LLM calls `self.camera.take_photo`:
   - `Camera::Capture()` grabs a frame.
   - `Camera::Explain(question)` JPEG-encodes it and POSTs `multipart/form-data`
     to the explain URL.
   - Headers include `Device-Id` (MAC), `Client-Id` (UUID), and
     `Authorization: Bearer <token>` when a token is present.
   - The server runs a vision model and returns text; the LLM uses that text.

Relevant code:

```
main/mcp_server.cc            # tool registration + capability parsing
main/boards/common/camera.h   # Camera interface
main/boards/common/esp_video.cc  # UVC capture + Explain upload
main/boards/common/esp32_camera.cc  # DVP camera variant
```

### Server options

**Official xiaozhi server:** supplies the vision URL itself (vendor log showed
`http://api.xiaozhi.me/vision/explain` + token); nothing extra to configure if
the agent uses a vision-capable model.

**Self-hosted `xiaozhi-esp32-server`** (`/Users/wenwu/work/xiaozhi-esp32-server/`,
see its `docs/mcp-vision-integration.md`): set a `VLLM` module with a real API
key and `server.vision_explain: http://192.168.1.6:8003/mcp/vision/explain`,
then confirm the log line `视觉分析接口是 http://192.168.1.6:8003/...` and that
the robot can reach `192.168.1.6:8003` (open the Mac firewall). Restart the
server after changing `vision_explain`.

Current local state (2026-11-11): the stack at `192.168.1.6` is configured for
OTA + WebSocket but was down during the last camera test, and
`VLLM_ChatGLMVLLM` still holds a placeholder API key, so vision cannot answer
yet. Server details: [../server/README.md](../server/README.md).

> Earlier USB-host test, build recipe, diagnostic patch, and detailed formats:
> [.archive/camera/usb-host-test.md](../.archive/camera/usb-host-test.md).

## Troubleshooting checklist

- [ ] Camera on the **USB / OTG** port, not `UART / COM`.
- [ ] Camera connected **before** boot/reset.
- [x] Board's USB/OTG port works (device mode; `USB JTAG_serial debug unit`).
- [x] Camera module is alive — `HD video` `0x349c:0x3307` on the Mac via an OTG
      adapter, live 1280×720 frames (2026-11-11).
- [x] Camera has 5 V — external supply spliced into the OTG cable's VBUS
      (2026-11-11). The OTG port itself still does **not** supply VBUS.
- [x] VBUS splice verified; a powered hub is an alternative, not required.
- [ ] Known-good data cable; verify the 4-pin wire order.
- [x] Mac sees it:
      `system_profiler SPUSBDataType` / `ffmpeg -f avfoundation ...`
- [x] ESP log has no `open /dev/video40 failed` (camera enumerated,
      `Camera init success`).
- [x] `MCP: Add tool: self.camera.take_photo` appears.
- [x] OLED `photo_camera` icon appears next to Wi-Fi while the camera streams
      (`Camera indicator: on` in the log) and turns off within ~1 s when the USB
      device disappears (2026-11-13).
- [ ] `self.camera.take_photo` is invoked (`Call tool:` log).
- [ ] Vision URL/token from the server matches the running backend.
- [ ] Robot can reach the server IP/port.

## Files and commits

| File | Purpose |
|---|---|
| `main/boards/desk-bot-01/config.json` | UVC Kconfig flags |
| `main/boards/desk-bot-01/compact_wifi_board.cc` | `EspVideo` init + `GetCamera()` |
| `main/boards/common/esp_video.h` | connection probe + `IsStreaming()` for the status icon |
| `main/boards/common/esp_video.cc` | 1 s `VIDIOC_G_PARM` probe, disconnect latch, DQBUF timeout |
| `main/display/oled_display.cc` | 128×64/128×32 camera-icon label placement |
| `main/display/lvgl_display/lvgl_display.cc` | `UpdateStatusBar()` camera-icon state |
| `my_exploration/.archive/camera/images/IMG_2143.PNG` … `IMG_2147.PNG` | Product page/spec screenshots |
| `my_exploration/tools/serial_capture.py` | Reset + capture CH343 serial output |
| `my_exploration/README.md` | Current project index |
| `my_exploration/.archive/history/2026-11-bringup-log.md` | Full dated bring-up log |
| `my_exploration/.archive/vendor/reverse-engineering.md` | Binary analysis / pin recovery |

Commit `feffbfb` contains the camera changes and the product screenshots.

## Next steps

1. **Give the camera 5 V.** The USB/OTG port cannot power it (verified twice with
   the `usb_camera_mic_spk` demo; the module itself is confirmed good on the
   Mac).
   - Cheapest: USB-A **powered** hub + the OTG cable into the board's USB/OTG
     port, camera on a `USB-C female → USB-A male` adapter on the hub.
   - Or inject 5 V from the board's 5 V header pin into the OTG adapter's VBUS
     (share GND).
   - Alternative: 4-pin pigtail from the camera connector to the board header:
     `5V → 5V`, `GND → GND`, `D+ → GPIO20`, `D− → GPIO19`. Verify the module
     wire order (red 5 V, black GND, green D+, white D−) before applying power.
2. Restore the robot firmware (the demo is flashed right now):
   ```sh
   esptool --chip esp32s3 -p /dev/cu.usbmodem5D300345091 -b 460800 \
     write-flash 0x0 build/bootloader/bootloader.bin \
     0x8000 build/partition_table/partition-table.bin \
     0x20000 build/xiaozhi.bin
   ```
   Only re-flash the demo (`~/esp-iot-solution/examples/usb/host/usb_camera_mic_spk`,
   app at `0x10000`) when testing new camera wiring; keep the NVS backup in
   `.archive/camera/backup/` in mind if Wi-Fi provisioning is lost.
3. Re-enable `InitializeCamera()`, rebuild/flash, and confirm `self.camera.take_photo`.
4. Decide on the vision backend:
   - official server (already configured), or
   - local `xiaozhi-esp32-server` with a VLLM + `server.vision_explain`.
5. Test: “what do you see?” and check `Call tool: self.camera.take_photo` plus the
   `EspVideo` upload log.
6. If the camera only exposes MJPEG or 1280×720 is too heavy, add MJPEG support or
   force a lower resolution.