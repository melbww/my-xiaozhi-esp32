# TTS, Latency, and Interruption

**Last verified:** 2026-11-09
**Server setup:** [README.md](README.md)
**Local TTS service:** `/Users/wenwu/work/xiaozhi-esp32-server/my_explorations/local-say-tts/`

## Current configuration

The agent uses `TTS_CustomTTS` + `TTS_CustomTTS0000`:

| Item | Value |
|---|---|
| Provider | `CustomTTS` (plain HTTP, non-streaming) |
| Endpoint | `http://host.docker.internal:8880/tts` |
| Voice | macOS `say -v Meijia` (Taiwanese) |
| Format | WAV, 22.05 kHz mono |

Edge TTS remains available (`TTS_EdgeTTS0001`, `zh-CN-XiaoxiaoNeural`) but is
slower. Tencent TTS failed earlier because its `secret_id`/`secret_key` were
empty.

## Why local `say`

Edge TTS is **non-streaming**: each sentence is fully downloaded before it is
encoded and sent. Measured from the server container:

| Provider | Per short sentence | Notes |
|---|---|---|
| Edge TTS | 1.2–6.5 s, occasionally more | New connection per sentence |
| macOS `say` | 0.6–1.1 s | Local CPU, no network |
| Tencent TTS | failed | Empty credentials |

Latency breakdown from a real turn (Edge TTS):

| Stage | Time |
|---|---|
| VAD end → ASR done | ~1.1 s |
| ASR done → LLM full response | ~0.5 s |
| LLM done → first TTS audio | ~3 s |
| First sentence → second sentence | **13 s** (slow Edge TTS call) |
| Second → third sentence | ~3 s |

TTS dominated the perceived delay. With the local `say` service:

- TTS synthesis: ~0.6–1.1 s per sentence
- Simulated WebSocket session: first audio ~2.6 s (including LLM), next
  sentence ~0.7 s later
- From real speech, add ~1.5 s for VAD + ASR; expect ~3.5–4 s to first audio
  and no multi-second sentence gaps.

## Local `say` service

```sh
cd /Users/wenwu/work/xiaozhi-esp32-server
my_explorations/local-say-tts/local-say-tts.sh start
my_explorations/local-say-tts/local-say-tts.sh status
my_explorations/local-say-tts/local-say-tts.sh logs
my_explorations/local-say-tts/local-say-tts.sh stop
```

It listens on `0.0.0.0:8880`; Docker reaches it at
`host.docker.internal:8880`. It uses `/usr/bin/say` + `/usr/bin/afconvert` and
does **not** access the microphone.

### 智控台 / CustomTTS config

```text
url:    http://host.docker.internal:8880/tts
method: POST
params: {"input": "{prompt_text}", "voice": "Meijia"}
format: wav
```

Voices:

```bash
say -v '?' | grep zh
SAY_TTS_VOICE=Tingting my_explorations/local-say-tts/local-say-tts.sh start
SAY_TTS_VOICE=Sinji    my_explorations/local-say-tts/local-say-tts.sh start
```

`Meijia` is Taiwanese, `Tingting` is mainland Mandarin, `Sinji` is Cantonese.

### Switch back to Edge TTS

In the 智控台, set the agent’s TTS model to `EdgeTTS` and voice to
`TTS_EdgeTTS0001` (or another Edge voice). The local service can keep running;
it is only used when the agent points at `CustomTTS`.

## Free TTS options

| Engine | Free? | Key? | Runs | Notes |
|---|---|---|---|---|
| Edge TTS | Yes | No | Microsoft cloud | Best zero-setup quality; non-streaming |
| macOS `say` | Yes | No | Local Mac | Current fast fallback; quality lower |
| Kokoro TTS | Yes | No | Local Docker | Good local quality; use via `CustomTTS` |
| PaddleSpeech | Yes | No | Local | Chinese-only, CPU-friendly |
| Fish Speech | Yes | No | Local | Good quality; GPU recommended |
| GPT-SoVITS v2/v3 | Yes | No | Local | Voice cloning; GPU recommended |
| IndexTTS | Yes | No | Local | GPU/vLLM recommended |
| Doubao / Aliyun / Xunfei | Trial quota | Yes | Cloud | Streaming, low latency |
| ElevenLabs | Paid credits | Yes | Cloud | Via `CustomTTS`; not built-in |

## ElevenLabs via `CustomTTS`

There is no dedicated ElevenLabs provider. `CustomTTS` can call it:

```text
url:    https://api.elevenlabs.io/v1/text-to-speech/<voice_id>?output_format=mp3_44100_128
method: POST
headers:
  xi-api-key: "sk_..."
  Content-Type: application/json
params:
  text: "{prompt_text}"
  model_id: "eleven_multilingual_v2"
format: mp3
```

Caveats: paid, voice ID is in the URL, and `CustomTTS` is non-streaming. A
dedicated provider would be needed for true mid-sentence cancellation.

## Interruption / barge-in

Interruption is a protocol/pipeline feature, not a TTS-vendor feature.

### What works now

- Say **“Hi, ESP”** while speaking: firmware `AbortSpeaking()` sends
  `{"type":"abort"}`; server `handleAbortMessage()` sets `client_abort`, clears
  TTS/audio queues, sends `tts stop`, and the device returns to listening.
- Press **BOOT**: `ToggleChatState()` → `AbortSpeaking()`.
- The LLM streaming loop also checks `client_abort`, so an in-flight LLM
  response stops.

### Why natural “talk over it” is off

- `aec_mode_ = kAecOff` because neither `CONFIG_USE_DEVICE_AEC` nor
  `CONFIG_USE_SERVER_AEC` is set.
- In `kDeviceStateSpeaking`, the firmware calls
  `EnableVoiceProcessing(false)`; only AFE wake-word detection stays on.
- `GetDefaultListeningMode()` returns `kListeningModeAutoStop` when AEC is off.

To enable natural barge-in:

- `CONFIG_USE_SERVER_AEC=y` makes the device send `features: {"aec": true}`;
  the server sets `client_aec` and aborts on detected voice.
- `CONFIG_USE_DEVICE_AEC=y` is the alternative if the board supports it. This
  board uses `NoAudioCodecSimplex`; server AEC is the likely route but needs
  hardware testing.
- With AEC, `GetDefaultListeningMode()` becomes `kListeningModeRealtime`.

### TTS cancellation behavior

- **Non-streaming** (`EdgeTTS`, `CustomTTS`/ElevenLabs, macOS `say`): abort
  stops queued playback quickly, but the current sentence’s synthesis cannot be
  cancelled; it finishes and the result is discarded.
- **Streaming** (`HuoshanDoubleStreamTTS`, `AliyunStreamTTS`,
  `XunfeiStreamTTS`, `IndexStreamTTS`): the provider loop checks
  `client_abort` and closes the stream, cancelling mid-sentence.
- ElevenLabs has a streaming API, but `CustomTTS` uses plain HTTP; a dedicated
  provider would be needed for true cancellation.
- Local `say` is non-streaming but fast; a cancel endpoint could be added to
  `local-say-tts.py` if needed.

### Code paths

- Firmware: `main/application.cc` (`AbortSpeaking`,
  `HandleWakeWordDetectedEvent`, `GetDefaultListeningMode`, state change),
  `main/protocols/protocol.cc` (`abort` message), `main/audio/audio_service.cc`
  (AEC and send queue).
- Server: `core/handle/abortHandle.py`, `core/handle/receiveAudioHandle.py`,
  `core/handle/sendAudioHandle.py`, `core/providers/tts/base.py`,
  `core/connection.py`.
