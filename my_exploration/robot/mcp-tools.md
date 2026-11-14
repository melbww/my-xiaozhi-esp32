# Desk Bot 01 — MCP Tool Inventory

**Last verified:** 2026-11-14 (derived from firmware source; `take_photo`
image path verified on hardware)
**Related:** [firmware.md](firmware.md) · [../camera/README.md](../camera/README.md) ·
`docs/mcp-protocol.md` in the repo

The device is an MCP server over the XiaoZhi WebSocket/MQTT transport. The
backend (MCP client) discovers tools with `tools/list` and calls them with
`tools/call`. Tool code lives in `main/mcp_server.cc` (common + user-only) and
`main/boards/desk-bot-01/compact_wifi_board.cc` (motors and lights).

## Inventory at a glance

| Build | Registered | Visible to the model | User-only |
|---|---|---|---|
| Current (camera/UVC compiled in) | 31 | 24 | 7 |
| Without the UVC component | 30 | 23 | 7 |
| With `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y` (replaces `take_photo`) | 31 | 24 | 7 |

The boot log prints the same numbers:

```text
I (...) MCP: 31 tools registered: 24 visible to the model, 7 user-only
MCP: Add tool: self.motor.drive
...
```

The count is code-derived: 20 board tools + 4 common visible tools + 7
user-only tools. `self.camera.take_photo` registers when
`CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y` is compiled in, even if no
camera is physically attached. Verified on hardware 2026-11-14: the boot log
shows `self.camera.take_photo`, no `self.camera.ask_vision`, and
`31 tools registered: 24 visible to the model, 7 user-only`.

Namespaces: `self.motor.*` (13), `self.car.act`, `self.lamp.*` (3),
`self.rgb_led.*` (3), `self.screen.*` (1 model + 3 user), `self.audio_speaker.*`,
`self.camera.take_photo` (image) or `self.camera.ask_vision` (legacy, opt-in),
`self.get_device_status`, `self.get_system_info`,
`self.reboot`, `self.upgrade_firmware`, `self.assets.set_download_url`.

## How the list is exposed

- `tools/list` returns model-visible tools by default. Add
  `"params":{"withUserTools":true}` to include the 7 user-only tools.
- Each page is capped near 8000 bytes; follow `nextCursor` if present.
- The common tools are moved to the **front** of the list to keep the prompt
  prefix cacheable. Final list order: common → board → user-only.
- Property schema is JSON Schema style: `type`, `default`, `minimum`,
  `maximum`, `maxLength`. A property without `default` is required.
- Successful calls return `true`, or a JSON/text payload. Failures return a
  JSON-RPC error; the model does not get a partial result.

Example call:

```json
{"jsonrpc":"2.0","id":7,"method":"tools/call",
 "params":{"name":"self.motor.drive",
           "arguments":{"steer":20,"throttle":60,"duration_ms":1500}}}
```

## Manual testing from a browser

The device hosts a console on port 81 that calls the **same** `McpServer`
registry the model uses, so names, schemas, validation, and side effects are
identical:

```text
http://Xiaozhi-CE50:81/      generated controls for every tool
http://Xiaozhi-CE50:81/mcp   POST a JSON-RPC request, get the response
```

- The page loads `tools/list` with `withUserTools: true`, so all 31 tools
  appear. Untick **user-only tools** to see only the 24 the model gets.
- Controls are generated from each property schema: integer ranges become
  sliders, strings become text fields, `self.car.act` gets an action dropdown,
  and `self.rgb_led.set_color` gets a color picker.
- `self.motor.drive` gets a drag pad. Dragging sends `duration_ms: 0`;
  releasing calls `self.motor.stop`. The red **STOP** button (or `Esc`)
  always stops the motors, and leaving the page stops them too.
- Tools that run longer than the first HTTP round trip answer `202`; the page
  polls `GET /mcp/result?id=N` and shows the result when it arrives (up to
  60 s). This covers camera capture and `self.screen.snapshot`.
- `self.screen.snapshot` pre-fills `http://<device-ip>:81/snapshot`. The
  device stores the uploaded JPEG and the page shows it from `/snapshot.jpg`.
  `self.screen.preview_image` pre-fills that same image URL, so a snapshot can
  be replayed to the OLED without another tool. The upload sends
  `Content-Length` framing because ESP-IDF's `httpd` cannot decode chunked
  request bodies.
- `self.reboot` and `self.upgrade_firmware` ask for confirmation.
- The console runs even when the XiaoZhi server is stopped or the device is
  not bound, which separates firmware faults from backend faults.
- It is plain HTTP with **no authentication**. Anyone on the LAN can move the
  robot, change the volume, or reboot it. Do not forward port 81 outside the
  LAN, and do not leave a browser page open on a shared machine.
- `self.camera.take_photo` shows the image in the browser but does not send it
  to the LLM; testing the image path into the model still needs a real
  conversation. `self.camera.ask_vision` is not registered in the default
  build.

Implementation: `main/boards/common/mcp_http_console.{h,cc}`, started from
`DeskBot01Board::StartNetwork()`.

## Model-visible tools (24)

### Status, audio, screen, camera (4 by default; the legacy VLM row is opt-in)

| Tool | Parameters | Behavior / returns |
|---|---|---|
| `self.get_device_status` | none | JSON snapshot: `audio_speaker.volume`, `screen` (empty on the 128×64 OLED), `network` (`type`, `ssid`, `signal` strong/medium/weak), and `chip.temperature` when available. No `battery` (this board has no battery ADC). Call before changing volume. |
| `self.audio_speaker.set_volume` | `volume` int 0..100 (required) | Sets the MAX98357 output volume; returns `true`. |
| `self.screen.set_theme` | `theme` string (required) | Switches the LVGL theme. Only `dark` is registered by `OledDisplay`, so `light` returns `false` with no change. |
| `self.camera.take_photo` | none | Opens the UVC stream, captures a frame, and returns it as a standard MCP image part (`image/jpeg`). The server attaches it to the main LLM conversation, so a multimodal model sees the photo itself. Fails if no camera is attached (`Failed to capture photo`). Stream closes right after capture (idle timeout 0). |
| `self.camera.ask_vision` | `question` string (required) | **Legacy, opt-in** (`CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y`): captures a frame and asks a separate vision model via `capabilities.vision`. Its text now returns to the main LLM instead of being spoken directly. Not registered in the default build. |

On a camera backend without local JPEG support (`SupportsJpegSnapshot()` false,
e.g. DVP), `take_photo` stays the legacy VLM tool even in the default build;
UVC `EspVideo` gets the image version.

`take_photo` and the legacy `ask_vision` path have their own page:
[../camera/take-photo.md](../camera/take-photo.md) covers image ingestion,
the opt-in VLM branch, the HTTP contract, and the error map.

### Motion (14)

All motion tools clamp to the L298N rail and map every non-zero speed into
60–100 % duty (`MOTOR_MIN_DRIVE_PERCENT`), because the N20 motors stall below
~60 %. `duration_ms` 0 means continuous until another command or
`self.motor.stop`; otherwise an `esp_timer` stops both motors (max 15 s).
Negative speed is expressed with the dedicated backward/steer parameters, not
by passing a negative `speed_percent`.

| Tool | Parameters | Behavior |
|---|---|---|
| `self.motor.drive` | `steer` int −100..100 (default 0), `throttle` int −100..100 (default 0), `duration_ms` int 0..15000 (default 0) | Differential drive: left = throttle + steer, right = throttle − steer. Best tool for arcs and precise control. |
| `self.motor.stop` | none | Stops both motors immediately and cancels the auto-stop timer. |
| `self.motor.move_forward` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 5000) | Both sides forward. |
| `self.motor.move_backward` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 5000) | Both sides reverse. |
| `self.motor.spin_around` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 1000) | Counter-clockwise spin in place (left reverse, right forward). |
| `self.motor.quick_forward` | `speed_percent` 60..100 (default 100), `duration_ms` 0..15000 (default 1000) | Short forward burst. Minimum speed 60; no fall detection. |
| `self.motor.quick_backward` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 1000) | Short reverse burst. |
| `self.motor.wiggle` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 300) | Short spin/wiggle. |
| `self.motor.dance` | `speed_percent` 0..100 (default 100) | Fixed 800 ms forward move; speed is the only control. |
| `self.motor.forward` | `speed` 0..100 (default 100), `duration_ms` 0..15000 (default 1000) | Simple forward; longer default than `move_forward`. |
| `self.motor.backward` | `speed` 0..100 (default 100), `duration_ms` 0..15000 (default 1000) | Simple reverse. |
| `self.motor.turn_left` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 600) | Left side reverse, right side forward. |
| `self.motor.turn_right` | `speed_percent` 0..100 (default 100), `duration_ms` 0..15000 (default 600) | Right side reverse, left side forward. |
| `self.car.act` | `action` string (default `forward`, max 32), `speed` 0..100 (default 100), `duration_ms` 0..15000 (default 1000) | Preset actions: `forward`, `backward`, `turn_left`, `turn_right`, `spin` (= turn_left), `stop`. Unknown action returns an error. |

### Lights: 7-LED WS2812 strip (6)

The strip on GPIO38 is the only light on this revision. Tool commands override
the normal status colors until `self.rgb_led.restore_default_idle`. `lamp.*`
also drives the vendor's unused GPIO18 lamp output.

Status colors while no override is active: off when idle, **green while
listening**, **blue while speaking/notifying**. Every other state keeps the
shared `CircularStrip` colors (blue while booting, Wi-Fi configuring, or
connecting; green blink while activating or upgrading). Green/blue for
listening/speaking are board-local overrides of the shared defaults
(red/green), set in `DeskBot01StatusLed::OnStateChanged()`.

| Tool | Parameters | Behavior / returns |
|---|---|---|
| `self.lamp.get_state` | none | `{"power": true}` or `{"power": false}`. Tracks the last on/off/color command, not measured current. |
| `self.lamp.turn_on` | none | All 7 LEDs solid white (160,160,160); GPIO18 high. |
| `self.lamp.turn_off` | none | All LEDs off; GPIO18 low. |
| `self.rgb_led.set_color` | `r`, `g`, `b` int 0..255 (default 255 each) | Solid color on all 7 LEDs; `lamp_on_` becomes true for any non-black color. |
| `self.rgb_led.turn_off` | none | All LEDs off. |
| `self.rgb_led.restore_default_idle` | none | Releases the override and resumes status colors (Wi-Fi, listening, etc.). |

## User-only tools (7)

These are hidden from the model by default and are meant for a companion app
or the console. Request them with `withUserTools: true`.

| Tool | Parameters | Behavior |
|---|---|---|
| `self.get_system_info` | none | JSON: firmware/application info (`name`, `version`, `compile_time`, `idf_version`, `elf_sha256`), flash size, minimum free heap, MAC, UUID, chip model/info, partition table, running OTA slot, display info, board info. |
| `self.reboot` | none | Schedules a reboot after ~1 s; returns `true` immediately. |
| `self.upgrade_firmware` | `url` string (required) | Downloads and installs the firmware from the URL, then reboots. Scheduled off the main loop. |
| `self.screen.get_info` | none | JSON `{"width":128,"height":64,"monochrome":true}`. |
| `self.screen.snapshot` | `url` string (required), `quality` int 1..100 (default 80) | Renders the OLED to JPEG and POSTs it as `multipart/form-data` to the URL; returns `true` on HTTP 200. |
| `self.screen.preview_image` | `url` string (required) | GETs an image and displays it on the OLED. |
| `self.assets.set_download_url` | `url` string (required) | Persists the assets download URL in NVS (`Settings("assets")`). |

## Not registered on this build

Do not expect these names, even though other XiaoZhi boards expose them:

| Tool | Why absent |
|---|---|
| `self.screen.set_brightness` | No backlight control on the SSD1306 board (`GetBacklight()` is null). |
| `self.set_press_to_talk` | `PressToTalkMcpTool` is only wired on xmini/movecall/lckfb boards. |
| `self.battery.*`, `self.battery.get_level` | No battery voltage divider/ADC in this hardware definition. |
| `self.system.reconfigure_wifi` | Not registered by `WifiBoard`; use the BOOT-button config mode or erase NVS. |
| `self.camera.ask_vision` | Legacy VLM tool, disabled by default; enable with `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y`. |
| `self.motor.sequence`, advanced `self.car.act` sequences | C3 2.2.x vendor features; not in the S3 2.1.0 dump and not implemented. |
| Vendor `self.rgb_led.*` action names | Only the four tools above exist; no effects/animations. |
| `self.dog.*`, `self.model.*`, `self.light.*`, `self.chassis.*` | Other boards' tools. |

Fall detection for `move_forward` / `quick_forward` is not available: this unit
has no edge or distance sensor.

## Camera over HTTP (not MCP)

Independent of MCP, the board serves `http://<robot-ip>/photo.jpg` (and a
one-button viewer at `/`) without the vision model. See
[../camera/README.md](../camera/README.md).

## How to change the list

1. Add or edit tools in `main/boards/desk-bot-01/compact_wifi_board.cc`
   (`InitializeMotorTools`, `InitializeLampTools`) for board-specific tools, or
   `main/mcp_server.cc` only for genuinely common ones.
2. Keep `AddTool` names unique; duplicates are ignored with a `Tool ... already
   added` warning.
3. Use `Property` ranges/defaults so the model sees valid bounds; a property
   without a default is required.
4. Rebuild, reflash, and read the `MCP: N tools registered` line to confirm the
   inventory. A backend with a tool-count limit drops extras silently.
5. Update this page and `Last verified`.
