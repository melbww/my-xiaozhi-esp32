# `self.camera.take_photo` — End-to-End

**Last verified:** 2026-11-14 (prompt/HTTP chain from source; vision verified
end-to-end on 2026-11-13)
**Related:** [README.md](README.md) · [../robot/mcp-tools.md](../robot/mcp-tools.md)

## Where the prompt lives

The device sends **no prompt of its own**. It forwards the `question`
argument verbatim to the server. Two pieces of fixed text shape the result:

1. **Tool description on the device** — `main/mcp_server.cc`, registration of
   `self.camera.take_photo`:

   > Always remember you have a camera. If the user asks you to see something,
   > use this tool to take a photo and then explain it.
   > Args: `question`: The question that you want to ask about the photo.
   > Return: A JSON object that provides the photo information.

   This is what tells the model to always pass a question and to narrate the
   answer afterwards. It is compile-time text; change it and rebuild.

2. **Server-side suffix** — the self-hosted server appends one instruction to
   every vision question in `core/providers/vllm/openai.py`:

   ```python
   question = question + "(请使用中文回复)"
   ```

   That is the "baked-in" prompt: whatever the model asks, the vision model is
   also told *"please reply in Chinese"*. It applies only to the built-in
   OpenAI-compatible VLLM provider; other providers added under
   `core/providers/vllm/` need the same line before they behave the same way.

Other server settings that affect the answer (`VLLM` provider defaults):
`max_tokens` 500, `temperature` 0.7, `top_p` 1.0.

## Call flow

1. The server's LLM calls `tools/call` with
   `{"name":"self.camera.take_photo","arguments":{"question":"..."}}`.
2. `McpServer::DoToolCall` schedules the call on the main task and lowers its
   priority (`TaskPriorityReset(1)`), so camera work does not starve audio.
3. `EspVideo::Capture()` issues `STREAMON`, waits **1 s** for auto-exposure,
   and grabs a frame. The UVC stream is not kept open at boot.
4. `EspVideo::Explain(question)` encodes the frame to JPEG (quality 80,
   ~0.5 s on a worker thread), then POSTs it to the vision URL.
5. The server authenticates, base64-encodes the image, and calls the configured
   `VLLM` module with the question (plus the Chinese suffix).
6. The server returns `{"success":true,"action":"RESPONSE","response":"..."}`.
7. The device wraps the body in MCP `content[0].text`.
8. The server's `device_mcp/mcp_executor.py` parses that text. Because the JSON
   has an `action` key, it returns `ActionResponse(action=RESPONSE,
   response=...)`, and `handle/intentHandler.py` sends `response` straight to
   TTS. **The conversational LLM does not see or rewrite the vision answer**
   ("视觉大模型不经过二次LLM处理").

Measured cost: ~1.2 s `STREAMON` + 1 s settle + encode/upload, so roughly
2–4 s before the vision model's own latency. With
`CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC=0` the stream closes at the next
monitor tick (~0.7 s after the frame), so every photo pays the start cost again.

### Why the answer feels rigid

- The vision model's reply is spoken **verbatim**; the agent LLM never
  paraphrases it. Style, length, and tone come from one vision call.
- Every question gets the server's `(请使用中文回复)` suffix.
- `max_tokens` defaults to 500, which caps how much detail can come back.
- The device's tool description pushes a question/answer framing, not a
  conversation. It also says "take a photo and then explain it", but the LLM
  never gets that second turn: the server speaks the vision answer directly.

## Parameters and return

| Item | Value |
|---|---|
| Tool name | `self.camera.take_photo` |
| `question` | string, **required**, no `minLength`/`maxLength` |
| Return | the raw HTTP response body as MCP text content |

- `question` is passed through unchanged. An **empty string is legal** and is
  effectively "describe this image"; the server then prompts only with
  `(请使用中文回复)`.
- The answer is not a normal tool result in the LLM sense: the server turns it
  into `Action.RESPONSE` and speaks `response` directly. A successful call is
  therefore **not** followed by a second LLM turn.
- If the response JSON has no `action` key (for example a `success:false`
  configuration error), the executor falls back to `Action.REQLLM`; only then
  does the conversational LLM see the raw string and explain it.
- A device-side tool error (`Failed to capture photo`, etc.) is returned as a
  JSON-RPC `error` (not a text result); the server raises it. Successful
  results always carry `"isError": false`.

## HTTP contract

The device POSTs to `server.vision_explain` (default
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

- The URL and token arrive in the server's MCP `initialize` message as
  `capabilities.vision = {url, token}`; `McpServer::ParseCapabilities()` stores
  them through `SetExplainUrl()`. A server that never sends that capability
  leaves the URL empty.
- `vision_handler.py` reads exactly the **first two** multipart parts, in this
  order. Adding headers or fields is fine; adding a part before `file` breaks
  the upload.
- Limits: 5 MB per image; JPEG/PNG/GIF/BMP/TIFF/WEBP accepted; the device always
  sends JPEG (the 1280×720 frame is ~40–50 KB).
- Auth: the token is bound to the `Device-Id` header by the server. An expired
  or mismatched token returns **HTTP 401**, which the device reports as
  `Failed to upload photo`.

## Failure modes

| Device-side error | Cause |
|---|---|
| `Failed to capture photo` | No camera attached, UVC open still pending, or camera unplugged. The capture is attempted before the upload, so the ~2 s capture cost is paid even if vision is unconfigured. |
| `Image explain URL or token is not set` | The server did not send `capabilities.vision` (MCP `initialize` never ran, or a non-XiaoZhi MCP client). |
| `Failed to upload photo` | Connection/write failure or any non-200 status, including 401. |
| `Failed to encode image to JPEG` | Encoder failure or empty output. |
| Tool succeeds but text says `"success": false` | The vision handler answers **HTTP 200** with `{"success":false,"message":"..."}` for configuration errors (e.g. no `VLLM` module selected). The device passes it through; the server sees no `action` key and falls back to `Action.REQLLM`, so the LLM gets the error text. |

A common `success:false` message is `您还未设置默认的视觉分析模块` (no VLLM
module selected). Auth failures are different: the body says
`无效的认证token或token已过期`, but with HTTP 401 the device discards it and
reports `Failed to upload photo`.

## Changing the behavior

| Goal | Change |
|---|---|
| Always describe rather than answer a narrow question | Prompt the model in its persona, or call with `question: ""`; no firmware change needed. |
| Different wording for the model | Edit the tool description in `main/mcp_server.cc`, rebuild, reflash. |
| Remove/replace the Chinese-only instruction | Patch `core/providers/vllm/openai.py` (`question + "(请使用中文回复)"`) in the server repo and restart. This affects every vision call. |
| Longer/shorter answers | Set `max_tokens` on the selected `VLLM` module (default 500). |
| Different vision model/backend | Select another `VLLM` module in the 智控台, or add a provider under `core/providers/vllm/`. |
| Point at another vision service | Set `server.vision_explain` in the server config and restart; it must accept the multipart contract above. |

The query at `GET /mcp/vision/explain` returns the configured URL and a health
message, which is the quickest way to confirm the server side is wired up.

## Can this drive the robot? (collision / edge avoidance)

**No.** `take_photo` is a human-request/response tool, not a control loop.
Verified constraints:

- **Latency.** ~1.2 s `STREAMON` + 1 s auto-exposure + encode/upload, then a
  VLM call: ~2–4 s per look before model latency. A robot at 60–100 % duty
  moves a long way in that time.
- **No continuous stream.** The UVC stream is closed when idle; there is no
  MJPEG endpoint, only `/photo.jpg`. Frames are produced per tool call.
- **The image never reaches the decision maker.** The answer is spoken via
  `Action.RESPONSE`; the main LLM is text-only and gets no pixels. A successful
  vision call also does not loop back into function calling: the server makes
  one tool round per user turn (`replyResult` has no tools), so the LLM cannot
  "look, then decide to drive" in the same turn.
- **Optics.** 65° FOV, focus range **5–20 cm**, no depth. Good for close
  objects in front of the lens; poor for reading the desk edge or the floor.
- **No feedback layer.** The motors are open-loop PWM (no encoders, no current
  sense). `self.motor.*` just sets duty and an auto-stop timer; nothing in the
  camera path can stop them in time.
- **Mount matters.** A single forward/level camera cannot see the cliff under
  the bumper, which is exactly where a fall starts.

What works instead:

| Goal | Approach |
|---|---|
| Don't fall off the desk | 2× IR reflectance cliff sensors under the front bumper, or a VL53L0X ToF; a high-priority GPIO task calls `SetMotorSpeeds(0,0)` in milliseconds. Keep it independent of MCP/Wi-Fi/cloud. |
| Don't bump into objects | VL53L0X (up to ~2 m, I2C on the existing 41/42 bus) or ultrasonic; stop/back off in the reflex loop. |
| Vision-aware navigation | Keep the stream open (`CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC` > 0) and run a local detector on-device (ESP-DL) or on a LAN companion at several fps; drive from that loop, not from an MCP call. |
| Use the VLM for high-level checks | Stop → capture → ask "is the path clear, left or right?" → act. Seconds per step; use for deliberate moves, never as the safety layer. |
| Put the photo in LLM context | Either fetch `/photo.jpg` server-side and use a multimodal main LLM, or add an image-content tool and patch the server. The device's `ImageContent` return type exists, but `device_mcp/mcp_handler.py` only forwards `content[0].text`, and the main LLM provider has no image message support. |

## Not MCP: local snapshot

`http://<robot-ip>/photo.jpg` uses the same `Capture()` path and
`Camera::WriteJpeg()`, but uploads nothing and returns the JPEG directly. It
does not involve a prompt, a token, or the vision model. See
[README.md#local-snapshot-over-http-no-vision-model](README.md#local-snapshot-over-http-no-vision-model).

## Source map

```text
main/mcp_server.cc                          # tool description, capabilities parsing
main/boards/common/esp_video.cc             # Capture(), Explain(), HTTP upload
main/boards/common/esp_video.h              # explain_url_ / explain_token_
main/boards/common/camera.h                 # Camera interface
main/boards/desk-bot-01/compact_wifi_board.cc  # UVC init, capture lock, hot-plug
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/providers/tools/device_mcp/mcp_handler.py  # initialize -> vision url/token
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/api/vision_handler.py                       # multipart -> VLLM
/Users/wenwu/work/xiaozhi-esp32-server/main/xiaozhi-server/core/providers/vllm/openai.py                    # "(请使用中文回复)"
```
