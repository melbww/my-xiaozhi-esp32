# Camera and Vision — Qebabe Xiaoche (USB UVC)

All camera/vision findings for the Qebabe robot: the camera module, USB
wiring/power, firmware UVC support, the vision upload path, and the server-side
options.

## Current status (2026-11-08)

- The camera is a **4-pin USB 2.0 UVC module** (1 MP, 1280×720 / 480×320,
  YUV/MJPEG, 5 V) with a 4-wire cable ending in a **Type-C male** plug.
- Firmware UVC support is present (`CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`,
  `EspVideo`, `GetCamera()`), but **camera initialization is currently commented
  out** in `compact_wifi_board.cc`.
- Reason: with no enumerating camera, `esp_video` blocks boot for ~10 s waiting
  for the UVC device, then fails to open `/dev/video40`.
- Current firmware boots normally (no camera wait), registers 30 MCP tools, and
  does **not** expose `self.camera.take_photo`.
- **The camera does not enumerate on the board either**, so this is not an
  ESP-IDF/UVC-driver problem. Tested 2026-11-08 with Espressif's
  `usb_camera_mic_spk` USB-host demo: the USB bus reports **zero devices**
  (`USB bus: no devices enumerated`), with no UVC/UAC connect event.
- The board's **USB/OTG port itself is fine**: with a Mac USB-C cable in that
  port it enumerates as `USB JTAG_serial debug unit` (`0x303a:0x1001`,
  `/dev/cu.usbmodem101`) and the secondary USB-Serial/JTAG console works.
  So D+/D−, the receptacle and the cable are good.
- Therefore the OTG port is **not supplying VBUS to the camera**. The board
  follows the ESP32-S3-DevKitC-1-style design where each USB-C port's VBUS goes
  through a Schottky diode *into* the 5 V rail; it can power the board but it
  cannot power a USB device when the board is powered from the 5 V pin.
- Flipping the Type-C plug 180° made no difference (rules out a one-sided
  D+/D− wiring in the plug for the "no power" case, but the plug can still be
  a charge-only/miswired cable).
- The camera **needs external 5 V**. Either a powered USB-A hub with
  C-female→A-male adapters, or a 4-pin pigtail wired to the board's 5 V/GND
  header pins plus D+→GPIO20 / D−→GPIO19.
- The local `xiaozhi-esp32-server` vision backend is **not running/configured**
  yet. The Mac's LAN IP is `192.168.1.6`; the robot is `192.168.1.13`.

## Camera module

Product screenshots (product page, Chinese):

```
my_exploration/cam/IMG_2143.PNG
my_exploration/cam/IMG_2145.PNG
my_exploration/cam/IMG_2146.PNG
my_exploration/cam/IMG_2147.PNG
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

The Qebabe board has two USB-C ports:

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

Conclusion: the device did not enumerate as USB at all, so this is not an
ESP-side problem. Test it through a USB-A port/powered hub to distinguish a
CC/cable issue from a dead module.

## Firmware support

### Kconfig

`main/boards/qebabe-xiaoche/config.json`:

```json
"CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y",
"CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=n"
```

`esp_video` is already a managed dependency (`espressif/esp_video`), and the
repo contains the USB UVC host driver.

### Board code

`main/boards/qebabe-xiaoche/compact_wifi_board.cc`:

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
I (10354) QebabeXiaocheBoard: USB UVC camera initialized
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
   python3 scripts/build.py qebabe-xiaoche --name qebabe-xiaoche
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

**Official xiaozhi server** (currently used by the robot):

- The server provides the vision URL, e.g. the vendor log showed
  `http://api.xiaozhi.me/vision/explain` with a token.
- No separate backend is needed if the agent uses a vision-capable model.

**Self-hosted `xiaozhi-esp32-server`:**

The repo is already checked out at `/Users/wenwu/work/xiaozhi-esp32-server/`.
Its `docs/mcp-vision-integration.md` describes the setup:

```yaml
selected_module:
  VLLM: ChatGLMVLLM

VLLM:
  ChatGLMVLLM:
    api_key: your_key

server:
  vision_explain: http://192.168.1.6:8003/mcp/vision/explain
```

- The server listens on port **8003** for the vision API.
- The robot must be able to reach `192.168.1.6:8003`; open the Mac firewall.
- Start the server and confirm the log line:
  ```
  视觉分析接口是 http://192.168.1.6:8003/mcp/vision/explain
  ```
- Open the URL in a browser/curl to verify it responds.
- Pointing the robot at the local server also requires changing its
  OTA/WebSocket server address, not just starting the server.

Current local state (2026-11-06): `data/.config.yaml` is missing, port 8003 is
not listening, and the vision endpoint is unreachable. The robot still uses the
official server (`api.tenclass.net`).

## USB host test with esp-iot-solution (2026-11-08)

Espressif's USB AV demo was used to test the camera independently of the
XiaoZhi firmware:

```
https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/host/usb_camera_mic_spk
```

It is a USB host demo with a browser console (SoftAP `USB-AV-DEMO-<MAC>`,
`http://192.168.4.1`) that detects UVC cameras (and UAC mics/speakers) and
previews MJPEG. Unlike `esp_video`, it does not block boot when nothing is
plugged in.

### Build recipe (ESP-IDF 6.1, board-safe)

```sh
git clone --depth 1 https://github.com/espressif/esp-iot-solution.git ~/esp-iot-solution
cd ~/esp-iot-solution/examples/usb/host/usb_camera_mic_spk
```

Create `sdkconfig.board.qebabe`:

```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=n
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

```sh
source ~/esp/esp-idf/export.sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.board.qebabe" set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5D300345091 flash
```

**Do not enable `CONFIG_ESP32_S3_USB_OTG`** (Example Configuration → board
choice). That option compiles board init code that drives GPIO12/13/17/18; on
this robot GPIO12/13 are the L298N `IN1`/`IN2` motor inputs and GPIO18 is the
lamp. Keep the default `BOARD_GENERIC`.

### Diagnostic patch

`app_usb_host.c` was changed to poll the bus so "no device" is explicit:

```c
ret = usb_host_lib_handle_events(pdMS_TO_TICKS(500), &event_flags);
if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) { ... }

uint8_t addr_list[8] = {0};
int num_devices = 0;
if (usb_host_device_addr_list_fill(sizeof(addr_list), addr_list, &num_devices) == ESP_OK &&
        num_devices != last_num_devices) {
    last_num_devices = num_devices;
    if (num_devices == 0) {
        ESP_LOGW(TAG, "USB bus: no devices enumerated");
    } else {
        for (int i = 0; i < num_devices; i++) {
            ESP_LOGW(TAG, "USB bus: device %d at address %d", i + 1, addr_list[i]);
        }
    }
}
```

### Observed result

With the camera in the USB/OTG port (either plug orientation):

```
I (932) app_usb_host: USB Host installed
I (932) app_uvc: UVC host installed
I (932) uac-host: Install Succeed, Version: 1.5.0
I (933) app_uac: UAC host installed
I (1083) app_wifi: SoftAP started, SSID:USB-AV-DEMO-3ECE51 URL:http://192.168.4.1
W (1432) app_usb_host: USB bus: no devices enumerated
```

No `Handling UVC connect`, no `Pick UVC MJPEG`, no enumeration error — the host
controller never sees a D+ pull-up, i.e. the camera is unpowered.

Control experiment: same firmware, Mac USB-C cable in the USB/OTG port →
macOS shows `USB JTAG_serial debug unit` and `/dev/cu.usbmodem101`, so the port
and its data lines are healthy.

### Serial capture helper

`my_exploration/tools/serial_capture.py <port> <seconds>` pulses DTR/RTS to
reset the board (CH343 circuit) and prints the boot log; useful because
`idf.py monitor` is interactive.

## Formats and resolution

The camera supports **YUV/MJPEG** at **1280×720 / 480×320**.

`EspVideo` format ranking (from `esp_video.cc`):

| Rank | Format |
|---|---|
| 5 | JPEG (only if `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT`) |
| 10 | YUYV / UYVY / YUV422P |
| 11 | RGB565 / RGB565X |
| 12 | RGB24 |
| 20 | GREY |

`V4L2_PIX_FMT_MJPEG` is not explicitly ranked. If the camera only offers MJPEG
at 1280×720, the firmware may need MJPEG handling or a forced lower resolution
(e.g. 640×480 YUYV). YUYV 1280×720 is ~1.8 MB/frame and may be heavy for USB
2.0 + memory bandwidth, so a lower resolution or MJPEG is preferable for a
snapshot.

The `EspVideo` constructor uses the UVC driver's current format; there is no
explicit `VIDIOC_S_FMT` preference in the board code yet.

## Troubleshooting checklist

- [ ] Camera on the **USB / OTG** port, not `UART / COM`.
- [ ] Camera connected **before** boot/reset.
- [x] Board's USB/OTG port works (`USB JTAG_serial debug unit` on the Mac).
- [ ] Camera has 5 V — the OTG port does **not** supply VBUS
      (tested indirectly: zero devices on the bus). Power it externally.
- [ ] USB-C female→USB-A male adapter + **powered** hub.
- [ ] Known-good data cable; verify the 4-pin wire order.
- [ ] Mac sees it:
      `system_profiler SPUSBDataType` / `ffmpeg -f avfoundation ...`
- [ ] ESP log has no `open /dev/video40 failed`.
- [ ] `MCP: Add tool: self.camera.take_photo` appears.
- [ ] `self.camera.take_photo` is invoked (`Call tool:` log).
- [ ] Vision URL/token from the server matches the running backend.
- [ ] Robot can reach the server IP/port.

## Files and commits

| File | Purpose |
|---|---|
| `main/boards/qebabe-xiaoche/config.json` | UVC Kconfig flags |
| `main/boards/qebabe-xiaoche/compact_wifi_board.cc` | `EspVideo` init + `GetCamera()` (init currently commented) |
| `my_exploration/cam/IMG_2143.PNG` … `IMG_2147.PNG` | Product page/spec screenshots |
| `my_exploration/tools/serial_capture.py` | Reset + capture CH343 serial output |
| `my_exploration/qebabe-xiaoche-project-context.md` | Main bring-up log |
| `my_exploration/reverse-engineering-vendor-firmware.md` | Binary analysis / pin recovery |

Commit `feffbfb` contains the camera changes and the product screenshots.

## Next steps

1. **Give the camera 5 V.** The USB/OTG port cannot power it.
   - Cheapest: USB-A **powered** hub + `USB-C female → USB-A male` adapter for
     the camera, plus `USB-A female → USB-C male` adapter into the board's
     USB/OTG port (this also feeds VBUS into the board, which is fine).
   - Alternative: 4-pin pigtail from the camera connector to the board header:
     `5V → 5V`, `GND → GND`, `D+ → GPIO20`, `D− → GPIO19`. Verify the module
     wire order (red 5 V, black GND, green D+, white D−) before applying power.
2. Re-run the `usb_camera_mic_spk` demo to confirm enumeration and see a live
   MJPEG preview at `http://192.168.4.1` over the `USB-AV-DEMO-*` SoftAP.
3. Re-enable `InitializeCamera()`, rebuild/flash, and confirm `self.camera.take_photo`.
4. Decide on the vision backend:
   - official server (already configured), or
   - local `xiaozhi-esp32-server` with a VLLM + `server.vision_explain`.
5. Test: “what do you see?” and check `Call tool: self.camera.take_photo` plus the
   `EspVideo` upload log.
6. If the camera only exposes MJPEG or 1280×720 is too heavy, add MJPEG support or
   force a lower resolution.