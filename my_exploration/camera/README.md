# Camera and Vision — Desk Bot 01 (USB UVC)

> Part of the [exploration index](../README.md). Current robot/server status:
> [../README.md](../README.md) and [../server/README.md](../server/README.md).

All camera/vision findings for the Desk Bot 01 robot: the camera module, USB
wiring/power, firmware UVC support, the vision upload path, and the server-side
options.

## Current status (2026-11-11)

- The camera is a **4-pin USB 2.0 UVC module** (1 MP, 1280×720 / 480×320,
  YUV/MJPEG, 5 V) with a 4-wire cable ending in a **Type-C male** plug.
- **The camera module is confirmed working (2026-11-11).** Through a USB-C
  OTG-style adapter on the Mac it enumerates as `HD video`
  (`0x349c:0x3307`) plus `HD audio` (UAC mic) and delivers live 1280×720
  frames. The module, its cable and its wiring are all good.
- Root cause of the earlier "Mac sees nothing": the camera's Type-C plug has no
  CC pull-downs, so a **C-to-C** link never enables VBUS. An OTG adapter (or a
  USB-A host port) works around it completely.
- Firmware UVC support is present (`CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`,
  `EspVideo`, `GetCamera()`), but **camera initialization is currently commented
  out** in `compact_wifi_board.cc`.
- Reason: with no enumerating camera, `esp_video` blocks boot for ~10 s waiting
  for the UVC device, then fails to open `/dev/video40`.
- Current firmware boots normally (no camera wait), registers 30 MCP tools, and
  does **not** expose `self.camera.take_photo`.
- **The camera still does not enumerate on the board**, so this is not an
  ESP-IDF/UVC-driver problem. Re-tested 2026-11-11 with Espressif's
  `usb_camera_mic_spk` USB-host demo and a USB-C OTG cable in the `USB / OTG`
  port (both plug orientations): `USB bus: no devices enumerated`, no UVC/UAC
  connect event.
- The board's **USB/OTG port itself is fine**: with a Mac USB-C cable in that
  port it enumerates as `USB JTAG_serial debug unit` (`0x303a:0x1001`) and the
  secondary USB-Serial/JTAG console works. So D+/D−, the receptacle and the
  cable are good.
- Therefore the OTG port is **not supplying VBUS to the camera**. The board
  follows the ESP32-S3-DevKitC-1-style design where each USB-C port's VBUS goes
  through a Schottky diode *into* the 5 V rail; it can power the board but it
  cannot power a USB device when the board is powered from the 5 V pin. An OTG
  cable does not change this — it only reshapes the connector.
- The camera **needs external 5 V**. Either a powered USB-A hub with
  C-female→A-male adapter behind the OTG cable, 5 V injected into the OTG
  adapter's VBUS, or a 4-pin pigtail wired to the board's 5 V/GND header pins
  plus D+→GPIO20 / D−→GPIO19.
- Firmware state: the **USB-host demo is currently flashed** (SoftAP
  `USB-AV-DEMO-*`), not XiaoZhi. Reflash `build/xiaozhi.bin` at `0x20000` plus
  the bootloader/partition table from `build/` to restore the robot.
  `my_exploration/.archive/camera/backup/` holds a 64 KB `0x0` and 24 KB NVS
  (`0x9000`) dump taken before the demo was flashed.
- The local `xiaozhi-esp32-server` vision backend is **not running/configured**
  yet. The Mac's LAN IP is `192.168.1.6`; the robot is `192.168.1.13`.

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
- The constructor currently has the call commented out:

```cpp
// Camera temporarily disabled: USB UVC enumeration blocks boot for up
// to 10 seconds when no camera is attached. Re-enable this call once
// the camera enumerates reliably.
// InitializeCamera();
```

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
- [ ] Camera has 5 V — the OTG port does **not** supply VBUS (USB-host demo
      still reports zero devices, both plug orientations). Power it externally.
- [ ] USB-C female→USB-A male adapter + **powered** hub (or 5 V injected into
      the OTG adapter's VBUS).
- [ ] Known-good data cable; verify the 4-pin wire order.
- [x] Mac sees it:
      `system_profiler SPUSBDataType` / `ffmpeg -f avfoundation ...`
- [ ] ESP log has no `open /dev/video40 failed`.
- [ ] `MCP: Add tool: self.camera.take_photo` appears.
- [ ] `self.camera.take_photo` is invoked (`Call tool:` log).
- [ ] Vision URL/token from the server matches the running backend.
- [ ] Robot can reach the server IP/port.

## Files and commits

| File | Purpose |
|---|---|
| `main/boards/desk-bot-01/config.json` | UVC Kconfig flags |
| `main/boards/desk-bot-01/compact_wifi_board.cc` | `EspVideo` init + `GetCamera()` (init currently commented) |
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