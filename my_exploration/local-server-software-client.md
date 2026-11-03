# Testing XiaoZhi Without Hardware: Local Server + Software Client

*Last checked: 2026-11-01 against `xinnan-tech/xiaozhi-esp32-server` `main`.*

This guide runs the **backend** (`xinnan-tech/xiaozhi-esp32-server`) on your Mac and connects a **browser-based software client** that acts like a device. It lets you test prompts, ASR, LLM, TTS, API keys, and server-side tools/MCP before your ESP32 arrives.

> **Important:** this does **not** run or emulate the `xiaozhi-esp32` firmware.
>
> The browser client speaks the same WebSocket protocol and sends Opus audio, so it exercises the server and conversation pipeline. It does not exercise board drivers, I2S codecs, displays, GPIO, wake-word detection on the device, or OTA firmware updates.

---

## 1. What you can and cannot test

| Area | Works without hardware? | Notes |
| --- | --- | --- |
| System prompt / persona | Yes | Configured on the server |
| ASR (speech to text) | Yes | Local FunASR or a cloud provider |
| LLM | Yes | API key or a local Ollama model |
| TTS (text to speech) | Yes | EdgeTTS or a cloud provider |
| API-key/provider switching | Yes | Edit `data/.config.yaml` and restart |
| Server-side built-in tools | Yes | Weather, news, time/lunar, music, etc. |
| Server-side MCP access point | Yes, with an extra service | Requires `mcp-endpoint-server` and an MCP tool server |
| Device-side MCP (GPIO, LED, servo) | No | The browser has no real ESP32 GPIO |
| Firmware/board/display/I2S behavior | No | Use real hardware or a QEMU mock board |
| OTA firmware update | No | The browser client is not an ESP32 |

A successful browser test means the **server and protocol** work. It does not guarantee the firmware will work on a specific board.

---

## 2. How the pieces fit

```text
Browser client (digital-human)
  microphone
      │  Opus over WebSocket
      ▼
xiaozhi-esp32-server
  VAD → ASR → LLM (+ tools/MCP) → TTS
      │  Opus over WebSocket
      ▼
Browser client
  speaker
```

The server also exposes an OTA endpoint:

```text
http://<host>:8003/xiaozhi/ota/
```

The browser client asks the OTA endpoint for the WebSocket address, then connects. This is the same discovery pattern the firmware uses, but the client is still a browser program, not the ESP32 firmware.

---

## 3. Prerequisites

- macOS or Linux with Docker Desktop.
- At least 10 GB of free disk space for the Docker image, Python dependencies, and the ASR model; more is safer.
- 8 GB of RAM is comfortable; local FunASR/torch is memory-hungry.
- `git` and `curl`.
- An LLM API key (ChatGLM, DeepSeek, Qwen, OpenAI-compatible, etc.), **or** a local Ollama installation.
- Free ports:
  - `8000` — WebSocket server
  - `8003` — HTTP/OTA server
  - `8006` — browser client page
  - `8004` — MCP access point (only if you test MCP)

No ESP32 hardware is needed for this guide.

---

## 4. Start the server with Docker

### 4.1 Clone and prepare directories

```sh
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server/main/xiaozhi-server

mkdir -p data models/SenseVoiceSmall
```

### 4.2 Download the local ASR model

The default ASR is FunASR with the `SenseVoiceSmall` model. The model is not in the repository, so download it before starting Docker:

```sh
curl -L --fail \
  -o models/SenseVoiceSmall/model.pt \
  https://modelscope.cn/models/iic/SenseVoiceSmall/resolve/master/model.pt
```

This is a large file. Make sure the downloaded file is `model.pt`, not an HTML error page. If the file is missing, Docker may create a **directory** at that path and the server will fail to load the model.

If you prefer a cloud ASR provider, you can skip the local model and change the ASR configuration, but this guide uses the local model because it needs no extra ASR account.

### 4.3 Create the server configuration

Create `data/.config.yaml`. The server deep-merges this file over `config.yaml`, so you only need to override what you care about.

The example below uses ChatGLM’s `glm-4-flash` (the server’s default), English-only ASR/TTS, and a short English persona:

```yaml
server:
  ip: 0.0.0.0
  port: 8000
  http_port: 8003
  websocket: ws://127.0.0.1:8000/xiaozhi/v1/

selected_module:
  VAD: SileroVAD
  ASR: FunASR
  LLM: ChatGLMLLM
  TTS: EdgeTTS
  Memory: nomem
  Intent: function_call

LLM:
  ChatGLMLLM:
    api_key: "PUT_YOUR_CHATGLM_API_KEY_HERE"

ASR:
  FunASR:
    language: en

TTS:
  EdgeTTS:
    voice: en-US-AriaNeural

prompt: |
  You are a concise English-only voice assistant.
  Always answer in English, even if the user speaks another language.
  Keep answers short and natural for spoken audio.
```

Notes:

- `server.websocket` is the address the **browser** will connect to. `127.0.0.1` is correct when the browser and server run on the same Mac.
- `server.http_port` must match the OTA URL you enter in the browser client.
- `provider` blocks such as `LLM.ChatGLMLLM` are merged with the defaults, so you usually only need to set the key.
- `Intent: function_call` lets the LLM call server-side tools. It requires a model that supports function calling; `glm-4-flash` does.

To use DeepSeek instead:

```yaml
selected_module:
  LLM: DeepSeekLLM

LLM:
  DeepSeekLLM:
    api_key: "PUT_YOUR_DEEPSEEK_API_KEY_HERE"
```

To use a local Ollama model instead, and avoid an LLM API key:

```yaml
selected_module:
  LLM: OllamaLLM

LLM:
  OllamaLLM:
    model_name: qwen2.5
    base_url: http://host.docker.internal:11434
```

Use `host.docker.internal` because the server is inside Docker and Ollama runs on the Mac host. If you run the server from source instead of Docker, use `http://127.0.0.1:11434`.

### 4.4 Start the container

```sh
docker compose up -d
docker logs -f xiaozhi-esp32-server
```

The compose file currently uses the `ghcr.nju.edu.cn` mirror. If that mirror is unreachable, change the image in `docker-compose.yml` to the upstream image:

```yaml
image: ghcr.io/xinnan-tech/xiaozhi-esp32-server:server_latest
```

Wait for log lines similar to:

```text
OTA接口是        http://<host>:8003/xiaozhi/ota/
Websocket地址是  ws://<host>:8000/xiaozhi/v1/
如想测试websocket请启动digital-human模块，打开浏览器交互测试
```

Use the addresses your server actually prints. If they differ from this guide, trust the logs.

---

## 5. Start the browser software client

The server repository includes `main/digital-human`, a browser client that behaves like a device: it captures microphone audio, encodes Opus, connects over WebSocket, plays returned audio, and shows a Live2D avatar.

### 5.1 Install and run

Open a second terminal. Use **Python 3.10–3.12** for this client if possible; the `sherpa-onnx` dependency may not have wheels for newer Python versions.

```sh
cd xiaozhi-esp32-server/main/digital-human

python3.11 -m venv .venv
source .venv/bin/activate
pip install -r wakeword_runtime/requirements.txt
```

If `python3.11` is not installed, use `pyenv`, Homebrew, or conda to install it. Then replace `python3.11` in the command above.

If `sounddevice` complains about PortAudio on macOS:

```sh
brew install portaudio
pip install sounddevice
```

Then start the client:

```sh
python start.py
```

Open:

```text
http://127.0.0.1:8006/index.html
```

The wake-word runtime is optional. If it fails to start, `start.py` prints a warning but the page still works for push-to-talk and text testing.

### 5.2 Point the client at your server

In the page, open **Settings** and set the OTA URL to:

```text
http://127.0.0.1:8003/xiaozhi/ota/
```

The page’s default placeholder may show port `8002`; the current server default is `8003`, so check it.

Click connect. The page requests the OTA endpoint, receives the WebSocket URL, and connects. Allow microphone access when the browser asks.

If the page is opened as `file://`, the browser will not grant microphone access. Always use `http://127.0.0.1:8006/index.html`.

---

## 6. Test checklist

Run these in order. Start with text so you can isolate audio problems.

### 6.1 LLM and prompt

Type into the chat box:

```text
In one sentence, what can you do?
```

You should get a short English answer. If this fails, the problem is the LLM key, model name, or base URL — not the microphone.

### 6.2 TTS

The answer should be spoken in an English voice. If text appears but there is no audio, check the server logs for TTS errors. EdgeTTS needs internet access.

### 6.3 ASR

Press the microphone/record control, say:

```text
What is the weather in Shanghai?
```

You should see a transcript and a response. If the transcript is wrong, check:

- The browser has microphone permission.
- `ASR.FunASR.language` is set to `en` if you want English-only recognition.
- The server container can read `models/SenseVoiceSmall/model.pt`.

### 6.4 English-only behavior

Ask a question in another language, for example:

```text
请用中文回答我。
```

With the prompt above, the assistant should politely refuse in English and continue in English. This tests the system prompt, not a hard security boundary.

For a stricter language lock, also set:

- ASR language to `en`
- TTS voice to an English voice such as `en-US-AriaNeural`
- The system prompt to explicitly forbid other languages

### 6.5 Server-side tool calling

Ask:

```text
What is the weather in Shanghai?
```

or:

```text
What is the latest news?
```

With `Intent: function_call`, the LLM can call built-in server functions such as weather or news. To see the calls, temporarily set:

```yaml
log:
  log_level: DEBUG
```

in `data/.config.yaml`, restart the server, and watch the logs.

These built-in functions are server-side tools. They are related to, but not the same as, the external MCP access point described next.

---

## 7. Test server-side MCP (optional)

There are two different things people call “server-side MCP”:

1. **Built-in server functions** — weather, news, music, lunar date, etc. These are loaded by the server and offered to the LLM through function calling.
2. **MCP access point** — a separate service that lets you register external MCP tool servers and expose their tools to the assistant.

This section covers the second one. It is optional and more involved.

### 7.1 Start the MCP access point

```sh
git clone https://github.com/xinnan-tech/mcp-endpoint-server.git
cd mcp-endpoint-server
docker compose up -d
docker logs -f mcp-endpoint-server
```

The logs print two addresses:

```text
智控台MCP参数配置: http://172.x.x.x:8004/mcp_endpoint/health?key=...
单模块部署MCP接入点: ws://172.x.x.x:8004/mcp_endpoint/mcp/?token=...
```

Copy the **single-module MCP access point** URL. The `172.x.x.x` address is Docker-internal and will not work from another container or from your browser directly.

Replace the host with the correct address for the process that will connect:

- `xiaozhi-server` running in Docker → `host.docker.internal:8004`
- A tool process running on the Mac host → `127.0.0.1:8004`
- Another device on the LAN → your Mac’s LAN IP, for example `192.168.1.25:8004`

For example:

```text
ws://host.docker.internal:8004/mcp_endpoint/mcp/?token=YOUR_TOKEN
```

### 7.2 Tell xiaozhi-server about the access point

Add this top-level setting to `data/.config.yaml`:

```yaml
mcp_endpoint: ws://host.docker.internal:8004/mcp_endpoint/mcp/?token=YOUR_TOKEN
```

Restart the main server:

```sh
cd xiaozhi-esp32-server/main/xiaozhi-server
docker compose restart xiaozhi-esp32-server
docker logs -f xiaozhi-esp32-server
```

Look for a log line like:

```text
mcp接入点是  ws://host.docker.internal:8004/mcp_endpoint/mcp/?token=...
```

### 7.3 Run an MCP tool server

The Xiaozhi project provides a simple calculator MCP server:

```sh
git clone https://github.com/78/mcp-calculator.git
cd mcp-calculator

python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Point the calculator at the access point, using `127.0.0.1` because it runs on the Mac host:

```sh
export MCP_ENDPOINT="ws://127.0.0.1:8004/mcp_endpoint/mcp/?token=YOUR_TOKEN"
python mcp_pipe.py calculator.py
```

In the browser client, ask:

```text
What is 23 times 17?
```

The LLM should call the `calculator` tool. Check the server logs for the tool name and result.

This demonstrates a real server-side MCP tool call without an ESP32. Device-side MCP tools, such as toggling a real GPIO or reading a real sensor, still require hardware.

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `FileNotFoundError: data/.config.yaml` | Config file missing | Create `data/.config.yaml` before starting |
| Docker creates a directory instead of `model.pt` | Model was not downloaded before `docker compose up` | Stop, remove the bad directory, download `model.pt`, start again |
| Browser cannot connect to OTA | Wrong port or server not running | Use `http://127.0.0.1:8003/xiaozhi/ota/`; check `docker ps` and logs |
| WebSocket connection fails | `server.websocket` points somewhere the browser cannot reach | For the same Mac, use `ws://127.0.0.1:8000/xiaozhi/v1/` |
| No microphone permission | Page opened with `file://` or browser blocked | Open `http://127.0.0.1:8006/index.html`; allow mic in browser settings |
| Text works but no voice | TTS provider error or no internet | Check logs; EdgeTTS needs internet; try `en-US-AriaNeural` |
| LLM authentication error | Missing/invalid key or wrong model name | Check `LLM.<provider>` config; confirm the model supports function calling |
| ASR model error | Model path or file is wrong | Ensure `models/SenseVoiceSmall/model.pt` exists and is a real file |
| MCP connection refused | Used Docker-internal IP | Use `host.docker.internal` from containers, `127.0.0.1` from the host |
| MCP tools not listed | `mcp_endpoint` not set or tool server not connected | Check the `mcp接入点` log line and the calculator process logs |
| Port already in use | Another process is using 8000/8003/8006/8004 | `lsof -i :8000` and stop the conflicting process |
| Container exits or runs out of memory | Local FunASR/torch needs more RAM | Increase Docker Desktop memory; use a cloud ASR provider if needed |

---

## 9. Cleanup

Stop the browser client with `Ctrl+C`.

Stop the server:

```sh
cd xiaozhi-esp32-server/main/xiaozhi-server
docker compose down
```

If you started the MCP access point:

```sh
cd mcp-endpoint-server
docker compose down
```

The downloaded model remains in `models/SenseVoiceSmall/`; remove it manually if you no longer need it.

---

## 10. Moving to real hardware later

When the ESP32 arrives, the server configuration, prompt, API keys, and MCP tools carry over. The browser client does not.

1. Flash a supported firmware variant. Build it for the exact board, or use a prebuilt firmware for a supported board.
2. Point the firmware’s OTA URL at your Mac:
   ```text
   http://<MAC_LAN_IP>:8003/xiaozhi/ota/
   ```
   This is `CONFIG_OTA_URL` or the `ota_url` NVS setting, depending on how you provision the device.
3. Change `server.websocket` to an address the ESP32 can reach:
   ```yaml
   server:
     websocket: ws://<MAC_LAN_IP>:8000/xiaozhi/v1/
   ```
4. Put the Mac and the ESP32 on the same Wi-Fi/LAN, and allow ports `8000` and `8003` through the macOS firewall.
5. Restart the server and the device.

Do not use `127.0.0.1` in the ESP32 configuration: on the ESP32, `127.0.0.1` means the ESP32 itself.

### Browser client versus real device

| Browser client | ESP32 device |
| --- | --- |
| Browser microphone/speaker | I2S microphone and amplifier/codec |
| Generated client identity | MAC address and board UUID |
| No real GPIO | Device-side MCP can control real pins |
| No display hardware | OLED/LCD/AMOLED output |
| No firmware OTA | Real OTA updates and rollback |
| Cannot validate board drivers | Full board initialization |

So: use the browser client to validate the **brain and protocol**, then use hardware to validate the **body**.

---

## Appendix: ports and files

| Port | Purpose |
| --- | --- |
| `8000` | WebSocket server (`/xiaozhi/v1/`) |
| `8003` | HTTP server and OTA endpoint (`/xiaozhi/ota/`) |
| `8006` | `digital-human` browser client |
| `8004` | MCP access point (optional) |

Important files and directories:

```text
xiaozhi-esp32-server/
└── main/
    ├── xiaozhi-server/
    │   ├── app.py                 # Python server entry point
    │   ├── config.yaml            # default configuration
    │   ├── docker-compose.yml     # Docker deployment
    │   ├── data/.config.yaml      # your overrides (create this)
    │   └── models/SenseVoiceSmall/model.pt
    └── digital-human/
        ├── start.py               # browser client runtime
        └── index.html             # browser client page
```

Useful commands:

```sh
# Server logs
docker logs -f xiaozhi-esp32-server

# Restart after config changes
docker compose restart xiaozhi-esp32-server

# Check listening ports
lsof -i :8000 -i :8003 -i :8006 -i :8004
```

---

## Sources

- Server repository: <https://github.com/xinnan-tech/xiaozhi-esp32-server>
- English README: <https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/readme/README_en.md>
- Simplified deployment guide: <https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment.md>
- Browser client: `main/digital-human/README.md` in the server repository
- MCP access point guide: <https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/mcp-endpoint-enable.md>
- MCP integration example: <https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/mcp-endpoint-integration.md>
- MCP access point server: <https://github.com/xinnan-tech/mcp-endpoint-server>
- Calculator MCP example: <https://github.com/78/mcp-calculator>

*Commands and configuration can change. If the repository differs from this guide, prefer the current repository files and the addresses printed in the server logs.*
