# Reverse-Engineering the Vendor ESP32-S3 Firmware

How the working vendor `xiaozhi` 2.1.0 image from
`backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin` was analyzed to recover the
real hardware pin map. Written as a reusable recipe for future boards and
binary-only recovery work.

## Results first

The exercise recovered the actual pins for this robot car:

| Signal | Vendor-verified value |
|---|---|
| L298N `IN1`..`IN4` | GPIO **12, 13, 14, 21** |
| LED/lamp output | GPIO **18** (not populated on this unit) |
| WS2812 status strip | GPIO **38**, 7 LEDs |
| Motor PWM | LEDC, 20 kHz, 10-bit, 50 ms ramp |
| Forward | LEDC ch0 + ch2 = GPIO 12 + 14 |
| Backward | ch1 + ch3 = GPIO 13 + 21 |
| Left turn | ch1 + ch2 = GPIO 13 + 14 |
| Right turn | ch0 + ch3 = GPIO 12 + 21 |

The reconstructed board had used `IN1=11, IN2=12, IN3=13, IN4=14`. Every input
was shifted by one and the real `IN4` (GPIO21) floated, which produced the
"sometimes moves, sometimes reverses" behavior.

## 0. Rules of engagement

- Work from the full 16 MB dump, never from a merged image.
- Verify the dump's SHA-256 before trusting it.
- Keep the analysis output outside tracked source (e.g.
  `my_exploration/vendor_app/`).
- Static analysis first. Live-device APIs are useful but can have side effects
  (see §11).
- Record recovered facts in the board README immediately; the next agent will
  not repeat the disassembly.

## 1. Extract the application image

The vendor partition table puts the active app at `ota_0`, offset `0x20000`,
size `0x3f0000`:

```sh
mkdir -p my_exploration/vendor_app
dd if=backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin \
   of=my_exploration/vendor_app/vendor_app.bin \
   bs=1 skip=$((0x20000)) count=$((0x3f0000))
```

Check it:

```sh
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 image-info my_exploration/vendor_app/vendor_app.bin
```

Output for this image:

```
Image version: 1
Entry point: 0x40379604
Segments: 6
Flash size: 16MB, mode DIO, freq 80m

Segment   Length   Load addr   File offs  Type
0         0x1348b4 0x3c1e0020  0x000018   DROM
1         0x08424  0x3fc9ff00  0x1348d4   DRAM
2         0x03310  0x40378000  0x13cd00   IRAM
3         0x1dd6f0 0x42000020  0x140018   IROM
4         0x14b58  0x4037b310  0x31d710   IRAM
5         0x00020  0x50000000  0x332270   RTC
```

## 2. The 8-byte gotcha (most important detail)

`esptool image-info`'s **"File offs" is the offset of the 8-byte segment
header, not of the segment data**. The data starts 8 bytes later.

Layout:

```
0x00  8-byte main image header
0x08  16-byte extended header (ESP32-S3)
0x18  segment 0 header (load addr, length)   <- esptool "File offs"
0x20  segment 0 data                         <- actual DROM byte 0
```

So the correct mapping is:

```
data_start = esptool_file_offs + 8
VMA(file)  = load_addr + (file_offset - data_start)
```

For this image the two useful deltas are:

| Segment | Data starts | Delta | VMA |
|---|---|---|---|
| DROM | `0x20` | `0x3c1e0000` | `file + 0x3c1e0000` |
| IROM | `0x140020` | `0x41ec0000` | `file + 0x41ec0000` |

Verification: the log string `I (%lu) %s: InitMotorPwm: initialized...` is at
file `0xb44c`, so its VMA is `0xb44c + 0x3c1e0000 = 0x3c1eb44c`. The literal
pool in IROM contains exactly `4c b4 1e 3c`, confirming the mapping.

A small helper:

```python
import struct

SEGS = [
    # esptool "File offs", load addr, length, name
    (0x00000018, 0x3c1e0020, 0x1348b4, "DROM"),
    (0x001348d4, 0x3fc9ff00, 0x08424,  "DRAM"),
    (0x0013cd00, 0x40378000, 0x03310,  "IRAM"),
    (0x00140018, 0x42000020, 0x1dd6f0, "IROM"),
    (0x0031d710, 0x4037b310, 0x14b58,  "IRAM2"),
    (0x00332270, 0x50000000, 0x00020,  "RTC"),
]

def file_to_vma(off):
    for hdr, load, length, name in SEGS:
        data = hdr + 8
        if data <= off < data + length:
            return load + (off - data)

def vma_to_file(va):
    for hdr, load, length, name in SEGS:
        data = hdr + 8
        if load <= va < load + length:
            return data + (va - load)

def find_pointer(data, vma):
    needle = struct.pack("<I", vma)
    hits, i = [], 0
    while True:
        i = data.find(needle, i)
        if i < 0:
            return hits
        hits.append(i)
        i += 1
```

## 3. Strings are cheap anchors

```sh
strings -a -t x my_exploration/vendor_app/vendor_app.bin \
  > /tmp/vendor_strings.txt

grep -iE "motor|ledc|lamp|gpio|ws2812" /tmp/vendor_strings.txt
```

This image leaks a lot:

- class and log names: `InitMotorPwm`, `LampController::LampController`,
  `SetRealtimeMotorCommand`, `motor_stop_cleanup`
- MCP tool names: `self.motor.move_forward`, `self.lamp.turn_on`, ...
- web endpoints: `/api/control`, `/api/motor/action`, `/api/debug/motor_test`
- exact error strings next to the code of interest

RTTI type names (e.g. `_ZN14LampControllerC4E10gpio_num_tE...`) also reveal class
boundaries even without a symbol table.

## 4. Find the code that references a string

Loads of string addresses on Xtensa go through `l32r`, which reads a 32-bit
literal from a nearby literal pool. The literal's value is the VMA of the
string.

1. Compute the string's VMA from its file offset (§2).
2. Search the whole image for the little-endian 4-byte pointer.
3. The hit is a literal-pool entry.
4. Disassemble the code segment and grep for `l32r` whose **loaded value** (in
   parentheses in objdump output) equals that VMA.

Example:

```sh
# "InitMotorPwm: initialized..." is at file 0xb44c -> VMA 0x3c1eb44c
grep -nE "\(0x3c1eb44c\)" irom.asm
```

Output:

```
4202022a: c1d987  l32r a12, 0x42002190 (0x3c1eb44c)
```

So the function around `0x4202022a` is `InitMotorPwm`.

The same string may be referenced through several literal pools (Xtensa
`l32r` has a limited range), so there can be more than one hit; use the
function context to pick the right one.

## 5. Disassemble a segment

```sh
# IROM data starts at file 0x140020 (0x140018 + 8), length 0x1dd6f0
dd if=my_exploration/vendor_app/vendor_app.bin \
   of=my_exploration/vendor_app/irom.bin \
   bs=1 skip=$((0x140020)) count=$((0x1dd6f0))

~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump \
  -D -b binary -m xtensa --adjust-vma=0x42000020 \
  my_exploration/vendor_app/irom.bin > my_exploration/vendor_app/irom.asm
```

Do the same for DROM if you need to resolve string starts. For code, IROM and
the two IRAM segments are the important ones.

Take a window when reading:

```sh
sed -n '50020,50120p' my_exploration/vendor_app/irom.asm
```

## 6. Read Xtensa code (practical notes)

- Function starts are visible as `entry a1, <frame-size>`.
- A call is `call8 <addr>`. With the windowed ABI the **callee's `a2` is the
  caller's `a10`**, callee `a3` = caller `a11`, and so on. This matters when
  reading constructor calls: the caller sets `a10 = this`, `a11 = gpio`.
- `movi.n a8, 32` + `addmi a8, a8, 0x4e00` means `a8 = 0x4e20 = 20000`. Large
  constants are often synthesized like this.
- Struct fields are plain stores at fixed offsets, e.g. `s32i a8, a1, 16`
  writes the first field of a stack struct. For `ledc_channel_config_t`, offset
  0 is `gpio_num` and offset 8 is `channel`.
- objdump will happily disassemble literal pools as instructions. Do not trust
  bytes between functions; anchor on `entry`, `l32r` value matches, and string
  references.

## 7. Worked example: motor pins and direction map

### 7.1 Find `InitMotorPwm`

The string `I (%lu) %s: InitMotorPwm: initialized (freq=%dHz, bits=%d, ramp=%dms)`
is at file `0xb44c` → VMA `0x3c1eb44c`.

`grep "(0x3c1eb44c)" irom.asm` found `l32r` at `0x4202022a`, inside the
function starting at `0x420200b0`.

### 7.2 Read the four channel configs

The function builds a zeroed `ledc_channel_config_t` on the stack at `a1+16`,
then fills `gpio_num` (offset 0) and `channel` (offset 8) before each
`ledc_channel_config()` call:

```
4202011d  movi a8, 12
42020123  s32i a8, a1, 16        ; gpio_num = 12   (channel defaults 0)
42020126  call8 0x42123768

4202013c  movi a8, 13
42020141  s32i a8, a1, 16        ; gpio_num = 13
42020143  movi a8, 1
42020145  s32i a8, a1, 24        ; channel = 1
42020147  call8 0x42123768

4202015d  movi a8, 14            ; gpio_num = 14
42020164  movi a8, 2             ; channel = 2
420201a2  movi a8, 21            ; gpio_num = 21
420201a9  movi a8, 3             ; channel = 3
```

Result: `ch0=GPIO12`, `ch1=GPIO13`, `ch2=GPIO14`, `ch3=GPIO21`.

### 7.3 Read the direction switch

The realtime command handler starts at `0x4202028c` and computes
`duty = (max_duty * speed) / 100` (the magic constant `0x51eb851f` is the
divide-by-100 reciprocal). It then switches on the direction argument and sets
pairs of channels:

| Direction codes | Channels | Meaning |
|---|---|---|
| 4, 7 | 0 + 2 | forward |
| 2, 13 | 1 + 3 | backward |
| 1, 5, 11 | 0 + 3 | right turn / spin |
| 3, 6, 9, 10 | 1 + 2 | left turn / spin |

Example: the forward branch at `0x42020521` sets channel 0 duty, updates it,
then sets channel 2 duty and updates it. The code at `0x42020421` sets channels
0 and 3 (right turn).

## 8. Worked example: lamp GPIO

Find the strings:

```sh
grep -n "self.lamp.get_state" /tmp/vendor_strings.txt
# 1843:fbe1 self.lamp.get_state
```

VMA = `0xfbe1 + 0x3c1e0000 = 0x3c1efbe1`. The references are inside the function
at `0x42030eec` (the `LampController` constructor: it calls `gpio_config`, then
`gpio_set_level(gpio, 0)`, and registers the three lamp tools).

Find its caller:

```sh
grep -n "call8.*0x42030eec" irom.asm
# 81103:42034735: 657bfc  call8 0x42030eec
```

Read the setup:

```
4203472c  l32r a10, 0x42003750 (0x3fcaa2f0)   ; this (static LampController)
4203472f  movi a11, 18                         ; gpio_num = 18
42034735  call8 0x42030eec
```

Because of the `call8` window shift, the callee sees `a2 = this` and
`a3 = 18`. The constructor stores it with `s32i a3, a2, 4` and then does
`gpio_set_level(gpio, 0)`.

The tool lambdas confirm active-high:

```
4202d46b  l32i a8, a3, 0        ; this
4202d46f  l32i a10, a8, 4       ; gpio
4202d471  s8i a7, a8, 0         ; power_ = true
4202d474  movi a11, 1           ; level 1
4202d476  call8 gpio_set_level
```

So: vendor lamp output = **GPIO18, active-high**. On this unit the output is
not populated; the visible light is the 7-LED strip.

## 9. Worked example: PWM frequency, resolution, ramp

The motor object's constructor writes its three PWM settings at offsets
`+92`, `+96`, `+100` (`0x420261bb` onward):

```
420261c1  movi.n a8, 32
420261c3  addmi a8, a8, 0x4e00     ; a8 = 0x4e20 = 20000
420261c6  s32i a8, a6, 92          ; freq_hz = 20000
420261c9  movi.n a8, 10
420261cb  s32i a8, a6, 96          ; duty_resolution = 10 bits
420261ce  movi a8, 50
420261d4  s32i a8, a6, 100         ; ramp = 50 ms
```

`InitMotorPwm` loads those fields into `ledc_timer_config_t` and passes `+100`
as the fade time to `ledc_set_duty_and_update`.

## 10. Cross-check with the boot log

The vendor firmware prints hardware facts at boot:

```
Board: UUID=... SKU=qebabe-xiaoche
Gpio17Ws2812Led: LED strip initialized on GPIO 38 with capacity 7 LEDs (logical 7) (RMT mode)
WS2812 RGB LED (GPIO38) initialized with 7 LEDs
```

Do not trust class names alone (`Gpio17Ws2812Led` says "17", the actual pin is
38). The runtime log is authoritative.

The vendor also prints the motor timings it loads from NVS:

```
加载电机动作配置:
  前进时间: 5000 ms
  ...
  默认速度: 100%
```

## 11. Live-device APIs: useful, but dangerous

While the vendor firmware was running, its web UI was reachable at
`http://<device-ip>/`:

- Pages: `/`, `/config`, `/help`, `/music`
- APIs: `/api/control`, `/api/motor/action`, `/api/led/config`,
  `/api/led/control`, `/api/config`, `/api/debug/motor_test`

Reading `/api/config` and `/api/led/config` is harmless and confirms the LED
count and effect G-codes. **Do not POST to unknown debug endpoints**:
`POST /api/debug/motor_test {}` immediately moved the robot. Prefer static
analysis unless you intend to make the hardware move.

## 12. What the analysis fixed

- Motor pins corrected to 12/13/14/21; the floating `IN4` was the cause of the
  random direction.
- The 7-LED strip is on GPIO38; the single-LED GPIO48 definition was wrong.
- The vendor lamp is GPIO18 but unpopulated; the lamp tools now drive the strip
  (with an override that suspends the normal status colors).
- The N20 motors need ~60 % duty to overcome static friction; non-zero speed
  commands are now mapped into 60-100 % duty.
- `McpServer::DoToolCall` now logs `Call tool: <name>` so voice-triggered calls
  are visible on the serial console.

## 13. Lessons learned

1. **Strings and boot logs give 80 % of the answer.** Look for log lines that
   print GPIO numbers, LED counts, frequencies, and endpoints before opening a
   disassembler.
2. **The segment-table off-by-8 is the #1 source of wrong disassembly.** If
   every `l32r` looks wrong, check this first.
3. **Xtensa `call8` shifts the register window.** Reading a constructor call
   with the caller's `a2`/`a3` instead of `a10`/`a11` gives a nonsense GPIO.
4. **Constants can be synthesized.** `movi.n` + `addmi` = 20000; do not expect
   to find `0x4e20` as a single immediate.
5. **Literal pools are interleaved with code.** objdump will decode them; use
   `entry` and the `(value)` annotation in `l32r` to stay oriented.
6. **Verify every recovered value against a second source.** The pin numbers
   were confirmed by both the ledc struct writes and the direction-switch
   channel pairs; the LED pin by the boot log; the lamp by the constructor call.
7. **Do not probe live debug endpoints casually.** They can move motors.
8. **Keep the full dump and checksum.** The 16 MB restore was the safety net
   for every experiment.

## 14. Reusable checklist

- [ ] Verify the backup SHA-256.
- [ ] Read the partition table; extract the app at its real offset/size.
- [ ] `esptool image-info`; write down segments, load addresses, file offsets.
- [ ] `strings -t x`; grep for motor/LED/GPIO/API/tool names.
- [ ] Map string file offset → VMA (remember data starts at `file_offs + 8`).
- [ ] Search for the 4-byte pointer; identify the literal pool.
- [ ] Disassemble the relevant segment with `--adjust-vma`.
- [ ] Grep `l32r` by loaded value to find the referencing code.
- [ ] Identify function start (`entry`), then read struct stores and calls.
- [ ] Decode `call8` arguments with the window shift (`a10`→`a2`, `a11`→`a3`).
- [ ] Cross-check with the boot log or a harmless read-only API.
- [ ] Record the confirmed pins in the board README and context doc.
- [ ] Only then change code; build, flash, and test on hardware.

## 15. Artifacts and commands used

Artifacts (regenerable, large):

```
my_exploration/vendor_app/vendor_app.bin   # app extracted from ota_0
my_exploration/vendor_app/irom.bin         # IROM segment slice
my_exploration/vendor_app/irom.asm         # full IROM disassembly
my_exploration/vendor_app/web/             # vendor web UI snapshots
```

Commands:

```sh
# Extract the app from the full dump
dd if=backups/esp32s3-16mb-1c29043ece50-2026-11-04.bin \
   of=my_exploration/vendor_app/vendor_app.bin \
   bs=1 skip=$((0x20000)) count=$((0x3f0000))

# Segment table and addresses
/Users/wenwu/.espressif/python_env/idf6.1_py3.14_env/bin/python3 -m esptool \
  --chip esp32s3 image-info my_exploration/vendor_app/vendor_app.bin

# String anchors
strings -a -t x my_exploration/vendor_app/vendor_app.bin > /tmp/vendor_strings.txt

# IROM slice and disassembly
dd if=my_exploration/vendor_app/vendor_app.bin \
   of=my_exploration/vendor_app/irom.bin \
   bs=1 skip=$((0x140020)) count=$((0x1dd6f0))

~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump \
  -D -b binary -m xtensa --adjust-vma=0x42000020 \
  my_exploration/vendor_app/irom.bin > my_exploration/vendor_app/irom.asm

# Find code referencing a string by its VMA
grep -nE "\(0x3c1eb44c\)" my_exploration/vendor_app/irom.asm
```

## 16. See also

- `my_exploration/camera-and-vision.md` — USB UVC camera, firmware support,
  vision upload path, and server configuration
- `my_exploration/vendor-web-ui-api-notes.md` — vendor web pages, HTTP APIs,
  LED G-code syntax, action names, and music-service findings
- `my_exploration/qebabe-xiaoche-project-context.md` — the full bring-up log
- `main/boards/qebabe-xiaoche/README.md` — the verified board pin map
- `docs/custom-board.md` — how board definitions are wired into the build
