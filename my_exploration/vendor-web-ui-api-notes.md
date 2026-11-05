# Vendor Web UI, HTTP API, and MCP Notes

Reference notes for the vendor `xiaozhi` 2.1.0 web interface and its API, captured
while the vendor firmware was running. Use this if we later want to reimplement
the web UI, add matching MCP tools, or recover the music/action behavior.

## Where the pages came from

The vendor application starts an HTTP server on port 80 after activation:

```
I (13239) WebServer: Starting web server on port 80
I (13239) Application: Web server started successfully on port 80
```

The device got `192.168.1.13` over DHCP. The pages were fetched from the live
device and are saved (git-ignored) under:

```
my_exploration/vendor_app/web/
├── index.html    # 小智小车遥控器 (joystick, actions, LED, links)
├── config.html   # 配置中心 (motor timings + LED G-codes)
├── help.html     # G-code tutorial
└── music.html    # 音乐控制
```

Recapture with:

```sh
IP=192.168.1.13
curl -s "http://$IP/"       -o index.html
curl -s "http://$IP/config" -o config.html
curl -s "http://$IP/help"   -o help.html
curl -s "http://$IP/music"  -o music.html
```

The pages are self-contained: all CSS/JS is inline and there are no external
images or fonts. The footer reads:

```
开发者：半岛铁盒子 抖音号：qebabe
```

The vendor web UI has **no authentication**. It is reachable by anyone on the
same LAN while the vendor firmware is running.

## Pages

| Path | Content |
|---|---|
| `/` | 小智小车遥控器: joystick, action buttons, LED effects, music/config/help links |
| `/config` | 配置中心: motor timings, default speed, LED count and G-code presets |
| `/help` | G-code 教程: syntax, effect codes, examples, FAQ |
| `/music` | 音乐控制: search, random, next, stop, volume, now-playing metadata |

## Joystick / realtime control

The joystick quantizes to four directions and sends continuous speed updates:

| `direction` | Meaning |
|---|---|
| `4` | forward |
| `2` | backward |
| `3` | left turn |
| `1` | right turn |
| `0` | stop |

- `speed` is `0-100`; the page ignores movement below 5.
- Drag → `POST /api/control` `{"direction": 4, "speed": 60}` repeatedly.
- Release → `POST /api/control` `{"direction": 0, "speed": 0}`.
- The page throttles duplicate direction/speed pairs, but always sends the stop.

Mapping to the L298N channels (verified in the binary):

| Direction | Left motor | Right motor |
|---|---|---|
| `4` forward | forward | forward |
| `2` backward | reverse | reverse |
| `3` left | reverse | forward |
| `1` right | forward | reverse |

A suitable firmware mapping is `Move(left, right, 0)` with `duration_ms=0`
meaning continuous, and `StopMotors()` for `0/0`.

## Action buttons

`POST /api/motor/action` with `{"action": "<name>"}`.

Basic movement:

```
move_forward
move_backward
turn_left
turn_right
spin_around
```

Emotional expressions:

```
wake_up
happy
sad
thinking
listening
speaking
wiggle
dance
```

Advanced emotions:

```
excited
loving
angry
surprised
confused
```

The vendor applies its own motion sequences and, where applicable, LED/sound
effects. The exact sequences are compiled into the vendor firmware; the HTML
only sends the action name. If exact reproduction is needed, disassemble the
vendor action executor (strings: `Motor executor`, `Executing queued motor
action`, `self.motor.*`).

Suggested simple sequences if we implement these later (not vendor-exact):

| Action | Suggested motion |
|---|---|
| `wake_up` | forward 300 ms, pause 200 ms, forward 300 ms |
| `happy` | forward/backward bounce 200 ms × 4 |
| `sad` | slow backward 800 ms, pause, backward 800 ms |
| `thinking` | left 300 ms, right 300 ms, repeat |
| `listening` | stop (or tiny left/right sway) |
| `speaking` | wiggle 200 ms alternating |
| `wiggle` | left/right alternating 300 ms × 3 |
| `dance` | forward, spin left, forward, spin right |
| `excited` | quick forward/backward 150 ms × 4 |
| `loving` | gentle left/right sway 400 ms × 3 |
| `angry` | rapid left/right 100 ms × 6 |
| `surprised` | backward 400 ms, pause, forward 200 ms |
| `confused` | left 400 ms, right 400 ms, left 400 ms |

## LED control

### Button → G-code map (exact, from `index.html`)

| Button | What is sent to `POST /api/led/control` |
|---|---|
| ⏹️ 关闭 | `0,0,0` |
| 🌈 彩虹 | `<count>,50,R` |
| 🌊 彩虹流水 | `<count>,50,P` |
| 🎨 三色流水 | `<count>,50,C,255,0,0,0,255,0,0,0,255` |
| 💨 呼吸 | `<count>,50,W,0,255,255` |
| ✨ 三色闪烁 | `<count>,50,M,255,0,0,0,255,0` |
| ⚡ 闪烁3次 | `<count>,50,F3,255,0,0` |
| 🔵 启动蓝 | `<count>,0,0,0,255` |
| 🔴 启动红 | `<count>,0,255,0,0` |
| 🟢 启动绿 | `<count>,0,0,255,0` |
| 🏁 跑马灯 | `<count>,60,S` |
| ☄️ 流星 | `<count>,40,T` |
| 🌊 波浪 | `<count>,45,V` |
| ⚡ 闪电 | `<count>,30,L` |

`<count>` is fetched from `GET /api/led/config` (`led_count`, default 7). The
page functions `sendLightshowPreset` / `sendStartupPreset` prepend it.

There is also an older `controlLed(mode)` helper in `index.html` supporting
`off/red/green/blue/white/warm/rainbow/breath/flash/chase/meteor/three_color/
gradient`; its solid-color format is `<count>,0,R,G,B`.

### G-code syntax (from the vendor `help.html`)

```
<led_count>,<speed>,<command or RGB...>
```

- `<led_count>`: number of LEDs to control (integer).
- `<speed>`: for animated effects, larger is usually faster; static colors use 0.
- `<command>`: an effect letter, or RGB values `0-255`.

Effect codes documented by the vendor help page:

| Code | Effect |
|---|---|
| `R` | 彩虹 rainbow |
| `P` | 彩虹流水 rainbow flow |
| `C` | 多色流水 multi-color flow (followed by RGB triplets) |
| `G` | 渐变 gradient (followed by RGB triplets) |
| `W` | 呼吸 breathe (followed by RGB) |
| `S` | 跑马灯 marquee/chase (followed by RGB) |
| `M` / `F` | 闪烁 blink (`F3` = blink 3 times) |
| `L` | 闪电 lightning |
| `V` | 波浪 wave |
| `T` | 流星 meteor |
| `U` | 脉冲 pulse |
| `N` | 雪花 snowflake |
| `K` | 赛车/扫描 race/scan effect (vendor-specific; used for listening) |

Static colors:

```
<count>,0,R,G,B        # e.g. 5,0,0,0,255 = 5 blue LEDs
<count>,0,0,0,0        # clear all (use this if residual color remains)
0,0,0                  # all off
```

Multi-color example:

```
7,50,C,255,0,0,0,255,0,0,0,255
```

### Vendor boot defaults

The vendor firmware prints its LED config at boot:

```
灯珠数量: 7
启动(加载中) G-code: 7,50,W,0,255,255
启动(完成/空闲) G-code: 7,0,255,0,0
灯光秀G-code: 7,50,R
聆听G-code: 5,90,K,255,0,0
讲话G-code: 5,90,P
```

The HTML sends the same strings; the **effect engine is firmware-side**. Copying
the HTML only gives you the UI; the letters `R/P/C/G/W/S/M/F/L/V/T/U/N/K` need a
renderer in our firmware.

## Config APIs

### Motor config

`GET /api/config` returns:

```json
{
  "forward_ms": 5000,
  "backward_ms": 5000,
  "left_turn_ms": 600,
  "right_turn_ms": 600,
  "spin_ms": 2500,
  "def_speed_pct": 100
}
```

`POST /api/config` accepts the same JSON and returns `{"status":"success"}`.
The form sends numbers as strings (`FormData` → `Object.entries`), so the
backend should accept both JSON numbers and numeric strings.

Vendor boot log also shows internal timings: wiggle 600 ms, dance 1500 ms,
quick forward/backward 5000 ms, default speed 100%.

### LED config

`GET /api/led/config` returns:

```json
{
  "led_count": 7,
  "startup_initial_gcode": "7,50,W,0,255,255",
  "startup_idle_gcode": "7,0,255,0,0",
  "lightshow_gcode": "7,50,R",
  "listening_gcode": "5,90,K,255,0,0",
  "speaking_gcode": "5,90,P"
}
```

`POST /api/led/config` accepts the same fields and returns
`{"status":"success"}`. The config page's “保存全部配置” posts motor config
first, then LED config, and expects both to succeed.

## Audio volume

`POST /api/audio/volume`:

```json
{"volume": 0-100}
```

The music page debounces the slider by 120 ms. Map this to the active audio
codec's output volume.

## Music control

Page: `/music`. It polls `GET /api/music/status` every 1.5 s and renders
title/artist/cover. Controls:

| Action | Request |
|---|---|
| Search/play | `POST /api/music/play` `{"query": "..."}` |
| Random | `POST /api/music/play` `{"query": ""}` |
| Next | `POST /api/music/next` |
| Stop | `POST /api/music/stop` |
| Volume | `POST /api/audio/volume` `{"volume": n}` |

Expected status fields: `playing`, `title`, `artist`, `cover_url` (and likely
progress/duration/lyrics; the page tolerates missing fields).

### Backend and token

The vendor firmware talks to:

```
https://xiaozhimusic.ytqhz.com/api/v1/control
```

Read-only probes on 2026-11-05:

```
GET  /api/v1/control                     -> 403 {"detail":"无权访问"}
POST /api/v1/control {}                  -> 422 missing body.action
POST {"action":"search","query":"test"}  -> 403 {"detail":"无权访问"}
POST {"action":"recommend",...}          -> 403
POST {"action":"play",...}               -> 403
```

So the API is a FastAPI-style service requiring an `action` field and a
vendor-issued token. The binary has `token: ` strings adjacent to the URL. The
token is presumably obtained through the vendor activation/license flow
(`espdeviceverifyservice.ytqhz.com`), not something we can reproduce from the
backup.

Other vendor strings around the music path:

```
self.music_server.play
self.music_server.stop
self.music_server.get_endpoint
search_music
play_music
recommend_music
web_music_play / web_music_stop / web_music_next
MusicVisualizer
music_meta_or_lyric
cover_url
artist
```

The vendor downloader accepts:

```
audio/ogg,application/ogg,application/octet-stream,*/*
```

and downloads the track locally before playback. It is likely OGG/Opus, which
is convenient if we ever reuse the existing Opus audio path.

**Current status:** our firmware has no music module and no vendor token.
Porting the music *page* is copy-paste, but real playback needs one of:

1. the vendor token/API (not public),
2. a public music API that returns a direct stream URL, or
3. a user-supplied URL plus a downloader/decoder.

## Implementation blueprint (if we build this later)

### HTTP server

- Use `esp_http_server` on port 80 (`httpd_start`).
- Register handlers for `/`, `/config`, `/help`, `/music`, `/api/*`.
- Embed the four HTML files as raw string literals (`web_pages.h`) or with
  `EMBED_TXTFILES`. All pages together are ~75 KB; they are self-contained.
- Use `cJSON` for request/response bodies; accept both JSON numbers and numeric
  strings for config fields.
- Keep the server LAN-only; add a token if the robot is ever exposed.

### LED G-code renderer

- Parse `<count>,<speed>,<command>[,<rgb>...]`.
- Use the existing `CircularStrip` (`SetMultiColors`) for output.
- Drive animated effects from an `esp_timer` at 20-60 ms and keep the strip in
  an “override” mode so `OnStateChanged()` does not repaint the status colors.
- Implement `R/P/C/G/W/S/M/F[n]/L/V/T/U/N/K` plus solid RGB and clear/off.
- The vendor help page is the authority for the effect letters; exact frame
  timing can differ without breaking the UI.

### Motor actions

- Basic moves map directly to `Move(left, right, duration_ms)`.
- Emotions are short sequences; run them in a FreeRTOS task with a generation
  counter so a new joystick/action command cancels the old sequence.
- Use the configured `forward_ms`, `backward_ms`, `left_turn_ms`,
  `right_turn_ms`, `spin_ms`, and `def_speed_pct`.
- Share the action code between the HTTP handlers and MCP tools.

### Configuration storage

- Use a new NVS namespace (e.g. `qebabe`) instead of the vendor's
  `motor_config` / `led_config`, so restoring the vendor firmware does not see
  modified values and vice versa.
- Load at boot; save on `POST /api/config` and `POST /api/led/config`.

### Build integration

- `main/CMakeLists.txt` globs `main/boards/<BOARD_DIR>/*.cc`, so new `.cc` files
  in `main/boards/qebabe-xiaoche/` are picked up automatically. If CMake does
  not notice the new file, touch `main/CMakeLists.txt` or run `idf.py
  reconfigure`.
- The web pages are board-specific; do not add them to the shared component for
  all boards.

## MCP mapping

Current firmware tools after the pin/LED fixes:

```
self.lamp.get_state / turn_on / turn_off
self.rgb_led.set_color / turn_off / restore_default_idle
self.motor.drive / stop / move_forward / move_backward / spin_around
self.motor.turn_left / turn_right / quick_forward / quick_backward
self.motor.wiggle / dance / forward / backward
self.car.act
```

Vendor tools seen in the 2.1.0 log (for reference):

```
self.motor.move_forward / move_backward / spin_around / turn_left / turn_right
self.motor.quick_forward / quick_backward / wiggle / dance / stop
self.motor.random_emotion
self.robot.enter_explore_mode / enter_free_mode / play_follow_chase / edge_patrol
self.rgb_led.set_color / turn_off / blink / rainbow / custom / restore_default_idle
self.lamp.get_state / turn_on / turn_off
self.music_server.play / stop / get_endpoint
```

If we add web actions later, the cleanest design is a shared action layer:

- HTTP `POST /api/motor/action` → `PlayAction(name)`
- MCP `self.car.act` / a new `self.motor.play_action` → the same `PlayAction`
- HTTP `POST /api/led/control` → `PlayLedGcode(gcode)`
- MCP `self.rgb_led.play_gcode` → the same `PlayLedGcode`

That avoids duplicating motion/effect logic between the two front ends.

## Cautions

- **`/api/debug/motor_test` moves the robot.** A `POST` with `{}` caused
  immediate forward motion. Do not probe it on a live robot.
- The vendor web UI has no authentication; treat it as LAN-only.
- The music API returns `403` without a vendor token; do not ship or rely on a
  token extracted from a device.
- The saved HTML files are git-ignored. If they are deleted, recapture them from
  the vendor firmware with the `curl` commands above.
- The vendor's exact LED animations and emotion sequences are compiled into its
  firmware; this document records the UI/API contract, not the animation code.

## See also

- `my_exploration/reverse-engineering-vendor-firmware.md` — how the binary was
  disassembled and the hardware pin map was recovered
- `my_exploration/qebabe-xiaoche-project-context.md` — bring-up log and current
  firmware status
- `main/boards/qebabe-xiaoche/README.md` — verified board pin map
