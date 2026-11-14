# Camera Tools — Image to the Main LLM, Optional VLM

**Last verified:** 2026-11-14 (flashed and verified end-to-end on hardware)
**Related:** [README.md](README.md) · [../robot/mcp-tools.md](../robot/mcp-tools.md) ·
[../robot/firmware.md](../robot/firmware.md)

There are now **two camera tool paths**. The default one sends the photo to the
**main LLM**, which must be multimodal. The old separate-VLM path is still
available but disabled by default.

## Default path: photo into the main LLM

```text
user speech
    │
    ▼
WebSocket :8000  ──> ASR ──> main LLM (tools)
                                │
                                │ tools/call self.camera.take_photo
                                ▼
                       MCP server on robot
                         Capture() + JPEG
                                │  MCP image content (base64)
                                ▼
                  server: Action.REQLLM + images
                                │
                  tool message + user(image)
                                ▼
                  main LLM again, same turn (depth+1)
                                │
                    text ──> TTS    or    motor tool
```

1. The main LLM calls `self.camera.take_photo` with **no arguments**.
2. `EspVideo::Capture()` opens the UVC stream, waits **1 s** for
   auto-exposure, and grabs a frame.
3. `Camera::WriteJpeg()` encodes it at quality 80 and the tool returns a
   standard MCP image part:
   `{"type":"image","mimeType":"image/jpeg","data":"<base64>"}`.
4. `device_mcp/mcp_executor.py` keeps the image, returns
   `Action.REQLLM` with `images` attached.
5. `connection.py` appends the assistant tool call, a text tool result, and a
   user message containing the image, then calls the main LLM again
   (`chat(depth+1)`, max depth 5).
6. The main LLM sees the photo and either speaks or calls another tool (for
   example `self.motor.drive`). The image message is replaced with `[photo]`
   after the turn so later turns do not resend ~70 KB of base64.

The photo is therefore **not** a side channel: it enters the same model and
tool loop as the conversation.

The tool description tells the model to call `take_photo` **by itself**. If it
batches `take_photo` with `self.motor.drive`, both run before the image exists,
so the robot would move without having seen anything.

A 720p JPEG is ~40–50 KB, so the MCP payload is ~55–70 KB of base64. This is
fine over WebSocket (and the base64 string is allocated in PSRAM:
`CONFIG_SPIRAM_USE_MALLOC=y`, 1 KB internal threshold). An MQTT/UDP transport
with a smaller payload limit may reject it.

Camera backends without a local JPEG encoder report
`SupportsJpegSnapshot() == false`. On those boards (for example the DVP
`Esp32Camera`), the firmware keeps the legacy VLM tool under the name
`take_photo`, so older camera boards do not regress. `EspVideo` (UVC) reports
true and gets the image tool.

Verified 2026-11-14: the live `deepseek-flash` endpoint accepts JPEG
`image_url` parts in this exact message shape (HTTP 200, image tokens counted).
The main LLM loop already existed for `Action.REQLLM`; only the image plumbing
was added.

**Hardware verification (2026-11-14):** flashed `desk-bot-01`, boot log shows
`self.camera.take_photo` and no `ask_vision`. Voice requests "拍一张照" produced
repeated `self_camera_take_photo` calls and detailed scene descriptions from
`deepseek-flash` ("灰色卫衣，手腕上…橙色表带", "侧面，戴细框眼镜…黑表盘的方表"),
including a hand covering the lens. No `/mcp/vision/explain` request occurred.
The `/photo.jpg` endpoint returned a 1280×720 JPEG in ~1.5 s.

## Legacy path: `self.camera.ask_vision` (off by default)

The old behavior is kept for a text-only main LLM or a dedicated vision model.
It is compiled only when `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y`
(default `n`) and **replaces** `self.camera.take_photo`, so the model sees one
camera tool, not two.

```text
tools/call self.camera.ask_vision {question}
    -> Capture() + Explain(question)
    -> POST <vision_explain> (multipart: question + JPEG)
    -> VLM text
    -> server: Action.REQLLM(text)   (no longer Action.RESPONSE)
    -> main LLM decides what to say / which tool to call
    -> TTS
```

Changes from the old behavior:

- **Removed** the hardcoded `question + "(请使用中文回复)"` suffix in
  `core/providers/vllm/openai.py`. The question is now forwarded unchanged.
- The VLM text is **no longer spoken directly**. `vision_handler.py` returns
  `Action.REQLLM` and `mcp_executor.py` keeps it that way, so the main LLM
  composes the reply and can call another tool; the user hears the main LLM,
  not the raw VLM.
- The tool was renamed from `take_photo` to `ask_vision`, because it asks a
  vision model rather than just taking a photo.

Keep this path off unless the main model cannot accept images.

## Parameters and return

| Tool | Parameters | Return |
|---|---|---|
| `self.camera.take_photo` | none | MCP image part (`image/jpeg`) |
| `self.camera.ask_vision` | `question` string (required) | the vision model's text |

`self.camera.take_photo` fails with `Failed to capture photo` when no camera is
attached or the UVC open is still pending. The stream closes again at the
monitor tick (`CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0`), so every photo
pays the ~1.2 s start plus the 1 s settle.

## Timing

| Path | Cost |
|---|---|
| Default | ~2.2 s capture + encode/upload + **one** main LLM call |
| Legacy | ~2.2 s capture + VLM round trip + main LLM round trip |

The default path replaces the VLM call with image tokens on the main LLM call
that was going to happen anyway, so it is usually faster and keeps one model's
persona.

## Legacy HTTP contract

`self.camera.ask_vision` POSTs to `server.vision_explain` (default
`http://<server>:8003/mcp/vision/explain`):

```text
POST /mcp/vision/explain
Device-Id: <Wi-Fi MAC>
Client-Id: <board UUID>
Authorization: Bearer <token>        (when the server supplied one)
Content-Type: multipart/form-data; boundary=----ESP32_CAMERA_BOUNDARY
Transfer-Encoding: chunked

part 1: question = <question verbatim>
part 2: file = camera.jpg, image/jpeg, <JPEG bytes>
```

- The URL/token arrive in the server's MCP `initialize` message as
  `capabilities.vision = {url, token}`.
- `vision_handler.py` reads exactly the **first two** multipart parts, in this
  order.
- Limits: 5 MB per image; JPEG/PNG/GIF/BMP/TIFF/WEBP accepted; the device sends
  JPEG (~40–50 KB at 720p).
- A bad or expired token returns **HTTP 401**; the device reports
  `Failed to upload photo`.

## Failure modes

| Symptom | Cause / action |
|---|---|
| `Failed to capture photo` | No camera, UVC open pending, or unplugged. |
| `Camera returned an empty JPEG` | Encoder produced no data. |
| `JPEG snapshot is not supported by this camera` | The camera backend does not implement `WriteJpeg()` (currently only `EspVideo` does). |
| Main LLM returns HTTP 400 on the image | The configured main model is text-only. Switch to a multimodal model or enable `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL` and use `ask_vision`. |
| `Image explain URL or token is not set` (legacy) | The server did not send `capabilities.vision`. |
| `Failed to upload photo` (legacy) | Connection/write failure or non-200 status, including 401. |
| `success:false` from the vision handler | Legacy VLM configuration error (e.g. no `VLLM` module); the text now goes to the main LLM. |

## Configuration

| Goal | Change |
|---|---|
| Default image-to-main-LLM | nothing; `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=n` |
| Legacy VLM tool | `CONFIG_XIAOZHI_ENABLE_VLM_CAMERA_TOOL=y`, rebuild, reflash |
| Change image quality | `80` in the `take_photo` callback in `main/mcp_server.cc` |
| Change legacy vision model | select a `VLLM` module in 智控台 |
| Change vision endpoint | `server.vision_explain` on the server, restart |
| Restore the Chinese suffix | add it back in `core/providers/vllm/openai.py` |

### Deploying

- **Firmware:** `python3 scripts/build.py desk-bot-01 --name desk-bot-01`, then
  flash as in [../robot/firmware.md](../robot/firmware.md). The tool change is
  visible in the boot log (`MCP: Add tool: self.camera.take_photo`; no
  `self.camera.ask_vision` unless the Kconfig option is enabled).
- **Server:** the docker stack bind-mounts `main/xiaozhi-server/core` into the
  container, so `docker restart xiaozhi-esp32-server` applies the Python
  changes; no image rebuild is needed. `docs/mcp-vision-integration.md` in the
  server repo notes that the VLM path is now optional.
- **Mixed versions:** a robot on the old firmware still exposes the old
  `take_photo(question)` tool. The new server routes its VLM text back to the
  main LLM (instead of TTS), so that combination is safe but still uses the
  legacy VLM.

## Can this drive the robot? (collision / edge avoidance)

Still **no** as a safety loop, but the main LLM can now use the photo in the
same turn (up to 5 tool rounds), which makes "look, then act" possible as a
deliberate behavior.

- **Latency:** ~2.2 s capture + the main LLM call per look. Too slow for
  reflex avoidance; the robot keeps moving during it.
- **No continuous stream:** snapshots only; no MJPEG endpoint.
- **Optics:** 65° FOV, focus **5–20 cm**, no depth. Poor for desk edges.
- **No feedback:** open-loop PWM motors; no encoders or current sense.
- **Mount:** a forward/level camera cannot see the cliff under the bumper.

Use hardware for safety:

- 2× IR cliff sensors under the front bumper, or a downward VL53L0X; a
  high-priority GPIO task calls `SetMotorSpeeds(0,0)` in milliseconds.
- VL53L0X ToF (up to ~2 m) on the existing I2C bus (41/42, address `0x29`) or
  ultrasonic for obstacles.
- For vision, keep the stream open and run a local detector (ESP-DL or a LAN
  companion), not an MCP/cloud call.

## Not MCP: local snapshot

`http://<robot-ip>/photo.jpg` uses the same `Capture()` + `Camera::WriteJpeg()`
path but returns the JPEG to the caller and uploads nothing. See
[README.md#local-snapshot-over-http-no-vision-model](README.md#local-snapshot-over-http-no-vision-model).

## Source map

```text
main/mcp_server.cc                          # take_photo (image) + ask_vision (opt-in)
main/mcp_server.h                           # standard MCP image content
main/Kconfig.projbuild                      # XIAOZHI_ENABLE_VLM_CAMERA_TOOL
main/boards/common/esp_video.cc             # Capture(), WriteJpeg(), Explain()
main/boards/desk-bot-01/compact_wifi_board.cc  # UVC init, capture lock, hot-plug
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/providers/tools/device_mcp/mcp_handler.py  # preserve content list
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/providers/tools/device_mcp/mcp_executor.py  # REQLLM + images / legacy text
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/connection.py                               # image message + recursive chat
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/api/vision_handler.py                       # legacy endpoint: Action.REQLLM
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/providers/vllm/openai.py                    # no Chinese suffix
```
