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
  constructor; the camera does **not** have to be attached at boot: `open()`
  fails after `CONFIG_USB_UVC_INIT_TIMEOUT_MS` (500 ms) and the monitor retries
  every ~2 s, so a camera plugged in later is picked up without a reboot.
- With no camera attached the board boots normally, registers 31 MCP tools
  (including `self.camera.take_photo`, which fails until a camera is attached)
  and shows no camera icon.
- **Status icons (2026-11-13, verified on hardware):** the 128×64 OLED top bar
  has a camera label next to the Wi-Fi icon with two states:
  - `MATERIAL_SYMBOLS_IMAGE` (a picture) while a camera is **present**, from
    `Camera::IsConnected()`. This is the `VIDIOC_G_PARM` probe, which stays valid
    while the stream is closed, so the icon shows the whole time the module is
    plugged in.
  - `MATERIAL_SYMBOLS_PHOTO_CAMERA` while it is **capturing**, from
    `Camera::IsStreaming()`. The icon font has no `videocam` glyph, hence the
    picture icon for "present".

  The UVC host only reports a disconnect to a stream that is open, so with the
  stream closed an unplug would go unnoticed. `VIDIOC_G_PARM` therefore verifies
  `dev_addr` against the USB host's device list (hot-plug patch 4) and resets the
  device state when it is gone, which also lets the reconnect handler adopt the
  replugged camera. Unplugging turns the icon off within ~1 s and replugging
  brings it back. `EspVideo` calls `UpdateStatusBar(true)` right after
  `STREAMON`/`STREAMOFF` and on unplug/replug: the 1 s status-bar tick can be
  starved for tens of seconds while the robot is in a conversation (the
  `SystemInfo` tick logs show 30 s gaps), which otherwise skipped the ~2 s
  "capturing" window entirely.
- **On-demand streaming (2026-11-13, verified on hardware):** the module runs
  hot while streaming, so the UVC stream is **no longer started at boot**.
  `Capture()` issues `STREAMON`, waits 1 s for auto-exposure to settle, takes
  the photo, and keeps the stream open for
  `CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC`; the 1 s monitor thread issues
  `STREAMOFF` when that deadline expires. The `desk-bot-01` build sets the value
  to **0**, so the stream closes at the monitor's next tick (measured 0.73 s
  after the frame) and every photo pays the ~1.2 s start. The Kconfig default is
  10 s, which lets a follow-up question reuse the already-open stream. A fresh
  stream needs the full 1 s settle: at 500 ms the same scene came back badly
  overexposed. The "capturing" icon still shows with `0` because the stream is
  pushed to the display on `STREAMON`/`STREAMOFF` (see the status-icon item).
  Built-in CSI/DVP sensors are unaffected and keep streaming. Verified
  boot/capture/idle log: `Camera init success (UVC stream stays off until
  capture)` → `Camera connected` (probe only, no `STREAMON`) →
  `Call tool: self.camera.take_photo` → `Explain image size=49708` →
  `Camera idle, stopping UVC stream to save power` right after the capture.
  (That log predates 2026-11-14, when `take_photo` still returned VLM text; the
  default now returns the image to the main LLM, but the capture/idle path is
  the same.)
  Two consecutive captures ~60 s apart were verified: each paid the ~1.3 s
  `STREAMON` + settle and both uploads succeeded, so repeated
  `STREAMON`/`STREAMOFF` cycles are safe.
  Note: the module's VBUS stays on (the external 5 V splice), so it is not fully
  powered down — only the sensor and USB stream go idle.
- **Hot-plug recovery (2026-11-13, verified on hardware):** an unplug is noticed
  within ~1 s even while the stream is closed (hot-plug patch 4), which turns the
  status icon off. When the camera reappears, the monitor resets the V4L2 stream
  state left behind by the disconnect and restarts the stream (`STREAMOFF` →
  `STREAMON` plus a one-frame check) only if a photo was in flight. While idle it
  just marks the camera connected; the next `take_photo` opens a fresh stream. It
  does **not** deinit/reinit `esp_video` (that path deadlocks waiting on the
  USB-host teardown). After three consecutive failed attempts the board reboots
  automatically as a guaranteed fallback. A `take_photo` during recovery waits
  up to ~1 s for the capture lock and then fails with
  `Capture skipped: camera is restarting` rather than blocking.
- **Vendor patch required (four changes):**
  1. `esp_video_usb_uvc_device.c` / disconnect callback: return the frame(s)
     still held by the capture buffer and call `uvc_host_stream_close()` (the
     same flow as Espressif's USB UVC example), then clear `stream_hdl`.
  2. `esp_video_init.c` / USB-host task: do not uninstall the USB host stack on
     `ALL_FREE`; keep it running so a replugged device can enumerate.
  3. `esp_video_usb_uvc_device.c` / `uvc_video_stop()`: skip the UVC host calls
     when `stream_hdl` is already NULL, so `STREAMOFF` still resets the
     esp_video stream state after a disconnect.
  4. `esp_video_usb_uvc_device.c` / `uvc_video_get_parm()`: the UVC host only
     delivers a disconnect event to an **open** stream, so with on-demand
     streaming `dev_addr` stayed valid after an unplug — the icon never cleared
     and the reconnect handler could not adopt the replugged device (it only
     fills slots with `dev_addr == 0`). The probe now checks `dev_addr` against
     `usb_host_device_addr_list_fill()` and resets the device state when absent.
  `my_exploration/tools/patch-esp-video-uvc-hotplug.py` applies all four
  idempotently. `managed_components/` is generated and gitignored — rerun the
  script after a clean build or component update.
- During testing the camera occasionally dropped off the USB bus by itself
  (`uvc_video_get_parm: UVC device ... is not connected` in the log). The icon
  turned off as expected. If that happens repeatedly, recheck the external 5 V
  feed and the VBUS splice.
- Vision is **working end-to-end (2026-11-13)**: `take_photo` → JPEG upload →
  the model's description came back through the self-hosted stack, so the
  configured vision backend/key is valid. The Mac's LAN IP is `192.168.1.6`;
  the robot is reachable as `Xiaozhi-CE50` (DHCP hostname). Its numeric lease
  changed from `.13` to `.5` on 2026-11-14.
- **Image-to-main-LLM path (2026-11-14, verified on hardware)**: the default
  tool returns the JPEG as MCP image content and the server injects it into the
  `deepseek-flash` conversation. Flashed firmware, repeated voice photos, and
  accurate scene descriptions were confirmed; no `vision/explain` call is made.
  The separate VLM tool is now `self.camera.ask_vision`, opt-in and off by
  default. See [take-photo.md](take-photo.md).
- **Local snapshot over HTTP (2026-11-13, verified on hardware):** the board
  serves `http://<robot-ip>/photo.jpg` plus a one-button viewer at `/`, with no
  vision model and no server involved. `curl http://Xiaozhi-CE50/photo.jpg`
  returned a 1280×720 JPEG (~40 KB) in 0.4–2.5 s. See
  [Local snapshot over HTTP](#local-snapshot-over-http-no-vision-model).

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

The MCP `take_photo` capability was added once the board could power the
camera; the Mac capture above only identifies the module.

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
  initialized. `open("/dev/video40")` runs the UVC init, so a camera that is
  **absent at boot** fails that open after `CONFIG_USB_UVC_INIT_TIMEOUT_MS`
  (500 ms on this board). `camera_` remains non-null and the status icon stays
  hidden, but the monitor thread keeps retrying `open()` every ~2 s, so the
  camera can be plugged in at any time afterwards — no reboot needed.

### Boot log with no camera attached

```
I (358) usb_uvc_device: Waiting for UVC device to be enumerated...
E (858) EspVideo: open /dev/video40 failed, errno=19(No such device)
W (1358) EspVideo: Camera not attached yet, will keep probing
...
I (9878) EspVideo: Camera init success (UVC stream stays off until capture)
I (9888) EspVideo: Camera attached
I (9918) Display: Camera indicator: connected
```

The first open waits `CONFIG_USB_UVC_INIT_TIMEOUT_MS` (this board sets **500**,
the range minimum is 500, component default 10000), so an absent camera costs
half a second of boot instead of 10 s. Retries stop as soon as a device opens.

With the camera present, the same wait is short and the init log ends with:

```
I (…) EspVideo: Camera init success (UVC stream stays off until capture)
```

The monitor prints `Camera connected` ~1 s later (the `VIDIOC_G_PARM` probe
succeeds without a stream), but no `STREAMON` happens at boot. The first
`take_photo` opens the stream, and `CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0`
closes it again right after the capture.

### Boot log without the UVC component (reference)

```
I (508) MCP: 30 tools registered: 23 visible to the model, 7 user-only
I (5478) WifiBoard: Connected to WiFi: ziroom904
I (6958) Application: Activation done
```

No `usb_uvc_device`, `EspVideo`, or `self.camera.take_photo`.

### Rebuild and reflash

```sh
cd /Users/wenwu/work/my-xiaozhi-esp32
. /Users/wenwu/esp/esp-idf/export.sh
python3 scripts/build.py desk-bot-01 --name desk-bot-01
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

Check the log for no `open /dev/video40 failed` and for
`MCP: Add tool: self.camera.take_photo`. The camera no longer has to be connected
before boot (hot-plug is detected), but boot-time connection avoids the 10 s
`CONFIG_USB_UVC_INIT_TIMEOUT_MS` wait. Lowering that timeout (e.g. 3000–5000) in
`sdkconfig_append` shortens the no-camera boot delay.

## Vision pipeline

There are two paths; the default sends the photo to the **main LLM** (it must
be multimodal).

**Default: `self.camera.take_photo` → main LLM**

1. `McpServer::AddCommonTools()` registers `self.camera.take_photo` with no
   arguments when `Board::GetCamera()` is non-null.
2. The tool calls `Camera::Capture()` and `Camera::WriteJpeg()` and returns a
   standard MCP image part (`image/jpeg`).
3. The server's `device_mcp` executor keeps the image and returns
   `Action.REQLLM`; `connection.py` appends a tool result plus a user message
   containing the image, then calls the main LLM again with tools.
4. The main LLM sees the photo, answers, or calls another tool (for example a
   motor tool). The image is replaced with `[photo]` after the turn.

**Legacy: `self.camera.ask_vision` → separate VLM (opt-in)**

- Enabled only with `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y` (default `n`).
- The server sends `capabilities.vision = {url, token}` in MCP `initialize`;
  `McpServer::ParseCapabilities()` calls `camera->SetExplainUrl(url, token)`.
- `Camera::Explain(question)` JPEG-encodes the frame and POSTs
  `multipart/form-data` to the explain URL with `Device-Id` (MAC), `Client-Id`
  (UUID), and `Authorization: Bearer <token>`.
- The text now returns to the main LLM (`Action.REQLLM`), not straight to TTS.

Full detail — tool contract, image ingestion, the legacy multipart contract,
and every failure mode — is in **[take-photo.md](take-photo.md)**.

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

Current local state (2026-11-13): vision works end-to-end through the
self-hosted stack at `192.168.1.6` with a valid `VLLM` module and key. If it
regresses, check the selected module and `server.vision_explain` first; the
prompt chain and error map are in [take-photo.md](take-photo.md). Server
details: [../server/README.md](../server/README.md).

> Earlier USB-host test, build recipe, diagnostic patch, and detailed formats:
> [.archive/camera/usb-host-test.md](../.archive/camera/usb-host-test.md).

## Local snapshot over HTTP (no vision model)

The MCP path sends the photo into the conversation (default) or to the legacy
VLM; either way it leaves the robot. To look at a photo yourself, without a
model and without the xiaozhi server at all,
`desk-bot-01` also runs a tiny HTTP server on the device
(`CameraHttpServer`, `main/boards/common/camera_http_server.cc`):

| URL | Result |
|---|---|
| `http://Xiaozhi-CE50/photo.jpg` | take one photo and return it as JPEG; `?q=1..95`, default 80 |
| `http://Xiaozhi-CE50/` | one-button viewer page (size + round-trip time) |

- `Xiaozhi-CE50` is the DHCP hostname derived from the Wi-Fi MAC, so it
  survives IP changes (the lease moved from `.13` to `.5` on 2026-11-14). It is
  not mDNS, so `Xiaozhi-CE50.local` does not resolve. For a fixed numeric
  address, reserve MAC `1c:29:04:3e:ce:50` on the router, or find the current
  lease with `arp -an | grep -i 1c:29:04:3e:ce:50`.

- Same capture path as the MCP tool: `Capture()` opens the UVC stream, waits 1 s
  for auto-exposure, and the monitor closes it at the next tick because
  `CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0`. Measured 2026-11-13: ~2 s from
  idle, ~0.4 s when the previous photo's stream is still open.
- Nothing is uploaded. The only traffic is the response to whoever opened the
  URL, so the robot does not need to reach the Mac (only your browser needs to
  reach the robot).
- The page never polls: it captures only when you press the button, so the
  module is not kept streaming (and hot).
- The server starts from `DeskBot01Board::StartNetwork()` once the WiFi stack is
  up; it is always on for this board (no Kconfig switch) and uses port 80.
- The photo is **not** drawn on the OLED yet: `OledDisplay` has no preview
  implementation, and `Capture()` skips the preview decode entirely when
  `Display::SupportsPreviewImage()` is false (the OLED case). Decoding a
  1280×720 JPEG down to 128×64 used to cost ~3 s per photo on this board.
- `Camera::WriteJpeg()` (implemented by `EspVideo`) is the reusable piece: it
  streams the last captured frame as JPEG instead of uploading it, and passes
  the encoder's own buffer through, so no full-size copy is allocated.
- `?q=` only affects frames that the sensor delivers uncompressed. This module
  delivers MJPEG, which is passed through unchanged, so `q` is a no-op here; a
  photo is roughly 50 KB.

## Troubleshooting checklist

- [ ] Camera on the **USB / OTG** port, not `UART / COM`.
- [x] Camera connected **before** boot is not required (2026-11-13): with the
      camera absent, boot pays only the 500 ms init timeout, then `open()` is
      retried every ~2 s and the camera is picked up as soon as it is plugged in.
      Boot-time connection is still the fastest path (icon at ~1 s).
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
- [x] OLED shows a picture icon while the camera is plugged in and the
      `photo_camera` icon while it captures (`Camera indicator: connected` →
      `capturing` → `connected`); unplugging hides it within ~1 s (2026-11-13,
      hardware verified).
- [x] Unplug/replug while the stream is idle: `Camera disconnected` →
  `Camera indicator: off`, then `Camera re-enumerated while idle, stream stays
  off` → `Camera indicator: connected`, and the next photo works (2026-11-13,
  hardware verified).
- [x] `self.camera.take_photo` is invoked (`MCP: Call tool:
      self.camera.take_photo`, 2026-11-13).
- [x] Vision URL/token from the server is valid (the model described the
      captured photo, 2026-11-13).
- [x] Robot can reach the server IP/port.
- [x] Local snapshot, no vision model:
      `curl -s http://Xiaozhi-CE50/photo.jpg -o cam.jpg` returns a 1280×720
      JPEG (2026-11-13, 0.4–2.5 s per request).
- [x] Camera preview decode is skipped on the OLED (`SupportsPreviewImage()`
      false), which removed the ~3 s/photo preview cost (2026-11-13).

### Snapshot server symptoms

| Symptom | Likely cause |
|---|---|
| `curl: (7) Failed to connect` | Server starts only after `StartNetwork()`; check Wi-Fi and the robot's IP |
| `/photo.jpg` says "Capture failed" | Camera unplugged or UVC init still retrying (~2 s probe); check the OLED camera icon |
| 500 on every request | Port 80 may be taken, or `EspVideo` never opened `/dev/video40`; check the boot log |

## Files and commits

| File | Purpose |
|---|---|
| `main/boards/desk-bot-01/config.json` | UVC Kconfig flags |
| `main/Kconfig.projbuild` | `XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC` (idle linger before `STREAMOFF`) |
| `main/boards/desk-bot-01/compact_wifi_board.cc` | `EspVideo` init + `GetCamera()`; `StartNetwork()` starts `CameraHttpServer` |
| `main/boards/common/esp_video.h` | connection probe, hot-plug reopen, on-demand stream, `IsStreaming()` |
| `main/boards/common/camera_http_server.{h,cc}` | on-device `GET /photo.jpg` snapshot server (no vision model) |
| `main/boards/common/camera.h` | `WriteJpeg()` — stream the frame as JPEG instead of an explain upload |
| `main/display/display.h` | `SupportsPreviewImage()` — skip the camera preview decode on displays without a preview area |
| `main/boards/common/esp_video.cc` | 1 s `VIDIOC_G_PARM` probe, on-demand `STREAMON`/`STREAMOFF`, reconnect handling, DQBUF timeout, status-bar push on stream change |
| `my_exploration/tools/patch-esp-video-uvc-hotplug.py` | reapply the four vendor UVC hot-plug fixes after component sync |
| `main/display/oled_display.cc` | 128×64/128×32 camera-icon label placement |
| `main/display/lvgl_display/lvgl_display.cc` | `UpdateStatusBar()` camera icon: `IMAGE` = present, `PHOTO_CAMERA` = capturing |
| `my_exploration/.archive/camera/images/IMG_2143.PNG` … `IMG_2147.PNG` | Product page/spec screenshots |
| `my_exploration/tools/serial_capture.py` | Reset + capture CH343 serial output |
| `my_exploration/README.md` | Current project index |
| `my_exploration/.archive/history/2026-11-bringup-log.md` | Full dated bring-up log |
| `my_exploration/.archive/vendor/reverse-engineering.md` | Binary analysis / pin recovery |

Commit `feffbfb` contains the camera changes and the product screenshots.

## Next steps

1. Measure the power difference: with the stream idle the module should stay
   noticeably cooler. Compare a boot-idle period against a `take_photo` plus
   10 s of streaming.
2. Tune `CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC` if 10 s of linger is too
   much (set `0` to close the stream as soon as a photo is captured).
3. If 1280×720 MJPEG is too heavy for the USB link, force a lower resolution
   (`VIDIOC_S_FMT` in `EspVideo::SetupDevice`).
