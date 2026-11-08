# The Opening Checklist

> General architecture teaching note; unrelated to the Qebabe robot bring-up.
> See [../README.md](../README.md) for the project index.

*An architecture lesson in two passes: first a story, then the map.*

## The Parable

A small coffee company had one menu it was proud of and a habit of opening cafés wherever a space was available: a bookstore corner, a train-station kiosk, a gym lobby, a rooftop terrace.

At first, each café wrote its own recipe cards. That seemed sensible, because every kitchen was different. The bookstore had a large espresso machine and no blender. The train kiosk had a blender and one small fridge. The gym had a cold-brew tap and no oven. Within a year there were more recipe cards than cafés. When the company fixed the cold-brew ratio, the fix reached three shops and missed eleven. A new hire trained at the bookstore transferred to the gym and spent her first shift looking for an oven that had never existed. On opening day at the train station, the card said "use the second grinder." There was no second grinder. The shift lead ground beans by hand for two hundred commuters and ran out before nine.

The district manager called a meeting. "The menu has to be kitchen-blind," she said. "It can ask for an espresso machine, a grinder, a cold line, a display board, a card reader. It cannot name a café, and it cannot go into the back room and grab equipment by itself."

So every café got the same opening checklist, with a labeled slot for each kind of equipment. If a café had no blender, the slot was marked NONE, and the menu simply did not offer smoothies. If there was no display board, the staff called names the old way. Nobody pretended the equipment was there.

That fixed the recipes but not the morning rush. At the gym café, a new hire took an order, started steaming milk, and handed over a drink before the customer had paid. Another barista remade the same order. The display board showed a name that had already left. A customer got a cold latte and a receipt for someone else's muffin.

The shift lead taped an order-state card next to the register:

**ORDERED → PAID → QUEUED → MAKING → READY → HANDED OFF**

Only certain moves were allowed. You could not go from ORDERED straight to HANDED OFF. You could not go back from READY to MAKING without a manager's override. One shift lead owned the order rail and called the moves. Anyone who wanted to change an order wrote a slip and clipped it to the rail. No one reached over the counter to rearrange cups.

The rail itself had a limit. During the rush, cups piled up faster than the baristas could make them, and the rail overflowed onto the floor. So the company split it into lanes: walk-in orders went register → hot line → pickup; app orders went tablet → cold line → pickup. Each lane had a fixed number of slots. When a lane was full, the shift lead paused that lane or dropped a promotional drink, rather than stopping every lane at once. A cold-line backup no longer silenced the register.

The company also stopped arguing about delivery apps. One app was a live phone order: the customer stayed on the line while the barista repeated the drink back. The other sent numbered bags with a printed receipt and expected a confirmation code. Both produced the same ticket for the kitchen. The baristas did not care which app the order came from.

Before opening a new café, the district manager did two last things. She opened the company catalogue, found that café's page, printed exactly one equipment sheet, and packed exactly one kit. Never two. Two espresso machines would fight over the same water line and the same three feet of counter. She also checked the café's name and address in the loyalty app. Those had to stay fixed: every receipt and saved card pointed at that identity. Renaming an existing café would break the records; a new café needed a new entry.

The hundredth café opened in a bookstore with no blender, one small fridge, and a menu that worked anyway.

## What It Was Explaining

That coffee company is **xiaozhi-esp32**, specifically its firmware architecture. Firmware is the software that runs on the device itself. XiaoZhi is firmware for Espressif's ESP32 family of chips, written in C and C++ with Espressif's IoT Development Framework (ESP-IDF). It turns many different boards into the same voice assistant. The device wakes on a phrase, captures microphone audio, streams it to a cloud service, and plays the reply. The cloud handles speech recognition, language-model reasoning, and speech synthesis, or a realtime speech-to-speech loop on capable hardware. The device also drives an optional display and LED, exposes device-side tools through the Model Context Protocol (MCP), and can update itself over the air.

**Scope.** I read "make sense of this project" as "understand how the firmware is put together," not as a tour of every board, the cloud service, or the MCP ecosystem.

The central idea is simple to state and easy to underestimate: **one hardware-independent application core, built for exactly one board at a time.**

### The layers that matter

| Layer | Key files | What it owns |
| --- | --- | --- |
| Entry point | `main/main.cc` | Initializes NVS (non-volatile storage) and starts the application |
| Runtime core | `main/application.h`, `main/application.cc` | Singleton, main event loop, activation, protocol lifecycle, display/audio orchestration |
| State rules | `main/device_state_machine.h`, `main/device_state_machine.cc`, `main/device_state.h` | Twelve states and the legal transitions between them |
| Board contract | `main/boards/common/board.h`, `board.cc` | Abstract hardware interface plus safe defaults for optional hardware |
| Board catalog | `main/boards/**` | Pins, codecs, displays, buttons, networking, and one `DECLARE_BOARD` per board |
| Build selection | `config.json`, `scripts/build.py`, `main/Kconfig.projbuild`, `main/CMakeLists.txt` | Chooses one board and variant, target chip, and configuration |
| Audio | `main/audio/**` | Codecs, wake word, voice activity detection, Opus encode/decode, bounded queues, AEC |
| Transport | `main/protocols/**` | Abstract `Protocol` plus WebSocket and MQTT/UDP implementations |
| UI and tools | `main/display/`, `main/led/`, `main/assets/`, `main/mcp_server.*`, `main/ota.*` | Display and emotion, LED, locales and assets, device MCP tools, OTA |

### The board contract

`Board` in `main/boards/common/board.h` is the checklist every café fills in. It declares pure virtual methods for the things every device must provide:

- `GetBoardType()`
- `GetAudioCodec()`
- `GetNetwork()`
- `StartNetwork()`
- `GetNetworkStateIcon()`
- `SetPowerSaveLevel()`
- `GetBoardJson()`
- `GetDeviceStatusJson()`

Everything else is optional. `GetDisplay()` returns a `NoDisplay` by default, `GetLed()` returns a `NoLed`, and `GetCamera()` returns `nullptr`. `GetBatteryLevel()` and `GetTemperature()` return `false` when the hardware does not exist. That is the slot marked NONE: the core can ask, and a missing capability degrades cleanly instead of crashing.

The rule that keeps this honest is that core code includes `board.h`, never a concrete board class or a board's `config.h`. A board can know about the core; the core cannot know which board it is running on.

### One board per build

Each board directory has a `config.json`, a `config.h`, and a source file ending in `DECLARE_BOARD(BoardClass)`. That macro defines `create_board()`, and `Board::GetInstance()` calls it once. A firmware image must contain exactly one such factory. Two factories would both try to define `create_board()`, and the build would fail. Two equipment kits would fight over the same water line.

The full selection chain is:

`main/boards/<board>/config.json` → `scripts/build.py` → `main/Kconfig.projbuild` (`BOARD_TYPE_*`) → `main/CMakeLists.txt` → board sources and `config.h`

A board's `config.json` names its reported type, its target chip, and one or more build variants. Each variant can append `sdkconfig` options. `scripts/build.py --list-boards` is the authoritative way to see what exists, and the canonical build command is:

```sh
python3 scripts/build.py <board-directory> --name <variant-name>
```

This chain is also why board identity is sensitive. The reported board type and name are part of the OTA contract: the server uses them to decide which firmware belongs on that device. Changing an existing board's pins or identity to support different hardware would silently break updates. Add a uniquely named board or release variant instead.

Note that `build.py` changes local `sdkconfig` and build state. Do not assume a build directory still represents the board you built last.

### One shift lead, one legal order sequence

`Application::Run()` is the single main event loop. It waits on a FreeRTOS event group and handles network changes, wake words, voice activity, state changes, playback draining, chat toggles, and scheduled callbacks. FreeRTOS is the small real-time operating system the firmware runs on.

`DeviceStateMachine` defines twelve states:

`unknown`, `starting`, `wifi_configuring`, `idle`, `connecting`, `listening`, `speaking`, `notifying`, `upgrading`, `activating`, `audio_testing`, `fatal_error`

Only some transitions are legal. For example, `starting` can go to `wifi_configuring` or `activating`; `activating` can go to `upgrading`, `idle`, or back to `wifi_configuring`; `listening` can go to `speaking` or `idle`. An illegal move is logged and rejected rather than forced.

All runtime state changes go through `Application::SetDeviceState()`. Code running outside the main task — audio callbacks, network callbacks, timers — posts work with `Application::Schedule()` or sets an event bit. It does not reach over the counter and mutate display or audio state directly.

The state machine is the order-state card; the event loop is the shift lead. The card says which moves are legal. The shift lead decides when to call them.

### Short lanes in the sound booth

`AudioService` runs separate input, output, encode, and decode tasks. The two data flows are:

- microphone → audio processing → Opus encoder → send queue → server
- server → decode queue → Opus decoder → playback queue → speaker

Microphone audio is encoded with Opus (an audio codec) as 16 kHz mono audio in 60 ms frames. Incoming audio carries its own sample rate and frame duration and is resampled to the codec's output rate when needed. The queues are bounded to seconds of audio, not unlimited: roughly 1.2 seconds of decode packets and 2.4 seconds of send packets, with a small number of encode and playback tasks in flight. When a lane fills, the pipeline drops or defers work instead of consuming all available memory.

AEC (acoustic echo cancellation) can be off, on the device, or on the server. That choice affects whether the device can listen while speaking in a realtime, full-duplex mode.

### Two delivery apps

`Protocol` is the shared ticket format. `WebsocketProtocol` behaves like a live phone call: a persistent two-way connection with JSON events and binary audio. `MqttProtocol` behaves like numbered parcels: MQTT carries control messages while UDP carries audio.

The choice is made during activation from the OTA configuration. If MQTT settings are present, MQTT is used; otherwise WebSocket is used if configured; if neither is present, the code falls back to MQTT. The application does not care which transport is active, but both must preserve the same message semantics.

### One voice turn, end to end

1. `app_main()` initializes NVS and calls `Application::Initialize()`.
2. `Initialize()` creates the one board from the build-time factory, sets up the display and audio service, and starts networking asynchronously.
3. When the network is ready, the device enters `activating`. An activation task checks assets and firmware updates, reads the OTA configuration, chooses the protocol, and signals completion. The main loop then moves to `idle`.
4. A wake word or button press raises an event. The device goes `connecting`, opens the audio channel, and then goes `listening`.
5. Microphone audio passes through voice activity detection and the wake-word pipeline, is encoded as Opus, and is sent to the server.
6. Server JSON events and TTS audio arrive. The device enters `speaking`, decodes the audio, and plays it. MCP messages can invoke device tools such as LED, servo, or GPIO control.
7. Playback drains and the device returns to `idle`. With AEC available, it can stay in a realtime listening loop instead.

### What the design buys

- **Adding a board is additive.** New hardware gets a new board implementation and a new entry in the build chain; existing boards are untouched.
- **Behavior is written once.** A fix to conversation flow, audio handling, or display behavior reaches every board that shares the core.
- **Optional hardware is safe.** A device without a screen, LED, camera, or battery gauge still runs the same application.
- **Transports are interchangeable.** WebSocket and MQTT/UDP sit behind the same interface.
- **OTA identity stays stable.** Board type and variant names are explicit, validated, and tied to release artifacts.

### Limits and common misconceptions

- **It is not one universal binary.** It is one source tree. Every firmware image contains exactly one board implementation, chosen at build time.
- **Abstraction does not erase hardware differences.** Optional capabilities are represented by safe defaults, but board code still has to check what actually exists.
- **The event loop is not automatically thread-safe.** Calling `SetDeviceState()` from an audio or network callback can run display and audio mutations on the wrong task. Schedule the work onto the main task instead.
- **The state machine checks legality, not wisdom.** A transition can be legal and still be triggered at the wrong moment.
- **The analogy's "dropping a tape" is a simplification.** Real queues have explicit capacities and drop or backpressure behavior; they do not magically preserve quality under overload.
- **The README's board counts drift.** It quotes 138 board directories and 171 variants; this checkout currently has about 170 board implementations and roughly 200 named variants. `python3 scripts/build.py --list-boards` is the source of truth.

### A reading path

If you want to build a mental model in an afternoon, read in this order:

1. `main/main.cc` — ten lines, the whole boot sequence.
2. `main/device_state_machine.cc` — the legal order-state card, short and readable.
3. `main/boards/common/board.h` — the opening checklist.
4. One concrete board, such as `main/boards/bread-compact-wifi/` — the equipment kit.
5. `main/application.h`, then `Initialize()`, `Run()`, `HandleNetworkConnectedEvent()`, and `InitializeProtocol()` in `application.cc`.
6. `main/audio/audio_service.h` and `main/audio/README.md` — the lanes and belts.
7. `docs/websocket.md`, `docs/mqtt-udp.md`, and `docs/mcp-protocol.md` — the delivery apps and the tool contract.

## Story Map

| Story element | Real concept |
| --- | --- |
| Menu and recipe cards | Application behavior in the hardware-independent core |
| Opening checklist with labeled slots | `Board` abstract interface |
| Slot marked NONE | Optional capabilities: `NoDisplay`, `NoLed`, null camera, absent battery |
| Menu cannot name a café or fetch equipment itself | Core depends on `board.h`, never a concrete board or its `config.h` |
| Exactly one equipment sheet and one kit | Exactly one board per build and one `DECLARE_BOARD` factory |
| Company catalogue | `config.json` → `build.py` → Kconfig → CMake selection chain |
| Order-state card | `DeviceStateMachine` legal transitions |
| One shift lead and one order rail | `Application::Run()` event loop, event bits, `Schedule()` |
| Fixed slots per lane | Bounded audio queues and separate capture/playback paths |
| Two delivery apps | `WebsocketProtocol` and `MqttProtocol` behind `Protocol` |
| Café name and address in the loyalty app | Board identity used for OTA compatibility |

**Where the analogy stops:** the menu and equipment sheet are compiled together into one firmware image. There is no runtime discovery or hot-swapping of boards; you cannot change kitchens after boot. The order-state card validates moves but does not plan them. The coffee shop can improvise; firmware has to be deterministic. And despite the "one shift lead" image, the firmware does run multiple FreeRTOS tasks — the main loop is the single place where application state is mutated, not the only task in the system.

## Make It Stick

**Takeaway:** *Write the menu once, describe every kitchen through the same checklist, choose one kitchen before opening, let one shift lead call only legal order states, and keep the lanes short.*

**Questions:**

1. You are adding a board with a battery gauge but no display. Which links in the build chain must change, and what must the core code never learn about?
2. A network callback running on another FreeRTOS task wants to move the device to `idle` and update the screen. What is the correct mechanism, and why is calling the state change directly from that callback risky?

If you explain the architecture back in your own words, I can check your model and fill in whichever layer still feels vague.

---

*Source note: written against this checkout in November 2026. File paths, board counts, and default behavior can change; verify board and variant names with `python3 scripts/build.py --list-boards`.*
