# Self-Hosted Server for the Robot

**Last verified:** 2026-11-09
**Server repo:** `/Users/wenwu/work/xiaozhi-esp32-server`

The robot uses a full-module `xiaozhi-esp32-server` deployment instead of the
official cloud. The `digital-human` browser runtime is **not** part of this
path; it is optional and currently stopped.

## Architecture

```text
Robot (ESP32-S3)
  │  OTA  → http://192.168.1.6:8002/xiaozhi/ota/
  │  WS   → ws://192.168.1.6:8000/xiaozhi/v1/
  ▼
manager-api :8002  ── 智控台, device/agent config, OTA
xiaozhi-server :8000 (WS) / :8003 (vision)
MySQL + Redis
  │
  └─ local TTS on the Mac host :8880 (CustomTTS, see tts.md)
```

## Start / stop

```sh
cd /Users/wenwu/work/xiaozhi-esp32-server

# full stack: manager-api, xiaozhi-server, MySQL, Redis
my_explorations/digital-human/iac/docker-stack.sh up
my_explorations/digital-human/iac/docker-stack.sh status
my_explorations/digital-human/iac/docker-stack.sh restart
my_explorations/digital-human/iac/docker-stack.sh down
```

`docker-stack.sh up` tries to pull images first and falls back to the locally
cached images if the ghcr mirror is unreachable.

Start the local TTS separately:

```sh
my_explorations/local-say-tts/local-say-tts.sh start
```

## OTA and WebSocket parameters

Set in the 智控台 → 参数管理 (and stored in `sys_params` / Redis `sys:params`):

```text
server.ota        = http://192.168.1.6:8002/xiaozhi/ota/
server.websocket  = ws://192.168.1.6:8000/xiaozhi/v1/
server.fronted_url = http://192.168.1.6:8002
```

`/xiaozhi/ota/` in full-module mode is served by **manager-api on :8002**, not
by `xiaozhi-server:8003`. The simple-server mode uses `:8003`.

## Activation and device binding

The robot’s MAC `1C:29:04:3E:CE:50` is bound to agent
`f20677aea16a49b3a2d93ef3461bbcf5` (currently “湾湾小何”) under `admin`.

- 智控台: `http://192.168.1.6:8002` (`admin` / `Admin@123456`)
- The OTA response includes an activation code until the device is bound.
- Binding endpoint used: `POST /xiaozhi/device/bind/{agentId}/{code}`.
- After binding, OTA returns the WebSocket URL and a token; the device no
  longer asks for activation.

If the device row is deleted, the next OTA creates a new activation code;
bind it again in the 智控台.

## Logs and debugging

```sh
docker logs -f xiaozhi-esp32-server
docker logs -f xiaozhi-esp32-server-web
```

Persisted server log:

```text
my_explorations/digital-human/deploy/logs/server.log
```

The optional `digital-human` Live Logs page (`http://192.168.1.6:8006/logs.html`)
is **not part of the robot path** and is currently stopped. If you start it for
browser testing, it also starts a Mac wake-word listener unless
`wakeword_runtime/config.json` has `"enabled": false` (it is currently false).

## Common issues

| Symptom | Check |
|---|---|
| OTA says “缺少 ota 地址” | `server.ota` missing in 智控台 / `sys_params` |
| OTA works, WS fails | `server.websocket` must be the LAN URL, not 127.0.0.1 |
| `pull` fails on ghcr mirror | `docker-stack.sh up` now falls back to local images |
| Device not in console | OTA once, then bind the activation code in 智控台 |
| TTS silent / slow | See [tts.md](tts.md); ensure local TTS is running |
| Camera/vision fails | See [../camera/README.md](../camera/README.md) |

If the Mac’s LAN IP changes, update all of:

- `CONFIG_OTA_URL` in `main/boards/desk-bot-01/config.json` (rebuild/reflash)
- `server.ota`, `server.websocket`, `server.fronted_url` in the 智控台
- Any `vision_explain` URL in the server config

## Notes

- `manager-api` uses MySQL and Redis; allow ~20–30 s after `up` before the
  console/OTA are ready.
- The full stack’s `server.secret` is in
  `my_explorations/digital-human/deploy/data/.config.yaml` (gitignored).
- The robot’s audio path is its own INMP441 → WebSocket → server. The Mac
  microphone is not used by the robot.
