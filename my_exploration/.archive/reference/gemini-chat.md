# Background Research: ESP32 and XiaoZhi Cloud vs Self-Hosting

> Background research, not current setup. Current robot facts are in
> [../../README.md](../../README.md) and [../../server/README.md](../../server/README.md).

Mental model of esp32

Building a solid mental model of the ESP32 helps you understand how it operates, why it's so popular, and how to approach programming it.

Here is a comprehensive breakdown to help you visualize how the ESP32 thinks, works, and interacts with the physical world.

1. The Core Metaphor: A Smart Microcontroller with a Built-in Radio
Think of the ESP32 not just as a traditional microcontroller (like a basic Arduino/ATmega328P), but as a powerful dual-core computer shrunk onto a chip, tailor-made for IoT (Internet of Things).

Traditional MCU: "I read a sensor, I do math, I toggle a pin."

ESP32: "I read a sensor, I process data on Core 0, I run my main application logic on Core 1, I host a web server, and I upload data to the cloud via Wi-Fi/Bluetooth simultaneously."

2. Architectural Blueprint
At a high level, visualize the ESP32 chip as a central hub connected to four main domains:

Plaintext
                  +---------------------------------------+
                  |         ESP32 Microcontroller         |
                  |                                       |
                  |   +-------------------------------+   |
                  |   |     Dual-Core Xtensa CPU      |   |
                  |   |   (Core 0: Wi-Fi/BT & System) |   |
                  |   |   (Core 1: User Application)  |   |
                  |   +-------------------------------+   |
                  |                   |                   |
                  |        +----------+----------+        |
                  |        |                     |        |
                  |   +----+----+           +----+----+   |
                  |   | Memory  |           | Periph. |   |
                  |   | (SRAM/  |           | (GPIO,  |   |
                  |   |  Flash) |           |  ADC/   |   |
                  |   +---------+           |  PWM)   |   |
                  |                         +----+----+   |
                  |                              |        |
                  +------------------------------|--------+
                                                 |
                                         +-------+-------+
                                         | Radio Subsystem|
                                         | (Wi-Fi & BT)  |
                                         +---------------+
A. The Brains: Dual-Core Processor
Most standard ESP32s feature a 32-bit dual-core Tensilica Xtensa microprocessor (running around 160 or 240 MHz).

Task Division: Under the hood (especially when using the official ESP-IDF framework), Core 0 is typically reserved for background tasks, system management, Wi-Fi stacks, and Bluetooth stacks. Core 1 is left completely free for your custom application code and logic.

B. The Senses: GPIO and Peripherals
The ESP32 features a versatile matrix of General Purpose Input/Output (GPIO) pins. However, unlike simpler boards, almost every pin can be multiplexed.

The Matrix: A pin isn't hardwired to just do one thing. Through an internal "GPIO Matrix," you can route different peripheral functions (like PWM, UART, I2C, SPI) to almost any physical pin you choose.

Analog Capabilities: It includes ADCs (Analog-to-Digital Converters) and DACs (Digital-to-Analog Converters), though keep in mind that the ADC can have some non-linearity quirks at the extremes of its voltage range.

C. The Voice: Wi-Fi & Bluetooth
The defining feature of the ESP32 is its native wireless capability.

It supports Wi-Fi (802.11 b/g/n) and Bluetooth (Classic and BLE 4.2).

Mental Check: Running the radio consumes significant power. When designing battery-powered projects, managing the radio states (active, modem sleep, deep sleep) is crucial.

D. The Memory Layout
Flash (External): Usually 4MB to 16MB of external SPI flash sitting on the module to store your compiled code and file systems (like SPIFFS/LittleFS).

SRAM (Internal): Around 520 KB of internal SRAM used for running variables, the FreeRTOS heap, and network buffers.

3. The Operating Paradigm: FreeRTOS & Tasks
When you program an ESP32 (especially in C/C++ via ESP-IDF or Arduino), you aren't just writing a single loop() that runs from top to bottom forever.

Underneath is a Real-Time Operating System (FreeRTOS): Your code runs as Tasks managed by FreeRTOS.

You can create multiple independent tasks (e.g., Task A reads a temperature sensor every second; Task B listens for incoming web requests; Task C updates an OLED display).

The scheduler constantly juggles these tasks across the two cores, giving each a slice of processing time based on priorities.

4. Power States & Energy Management
If you imagine the ESP32 as a tiny workstation, it can also drop into ultra-low-power hibernation:

Active Mode: CPU, peripherals, and radio are fully powered on (draws ~80mA to 240mA+ depending on Wi-Fi activity).

Modem/Light Sleep: The CPU pauses, and the Wi-Fi/Bluetooth circuit is put to sleep. Quick to wake up.

Deep Sleep: Almost everything shuts down except for the RTC (Real-Time Clock) controller, a tiny bit of RTC memory, and optionally an ULP (Ultra Low Power) co-processor. Power consumption drops to microamps (
μA), allowing the chip to run on a small battery for months, waking up periodically based on a timer or external sensor trigger.

Summary Checklist for Working with an ESP32
Dual-Core: Expect to structure code thoughtfully; don't block the main loop with long delay() functions if you can use tasks or timers instead.

Voltage: It operates at 3.3V logic. Connecting 5V sensors directly to its GPIO pins can fry them.

Power Supply: Wi-Fi transmission causes sudden, sharp current spikes (up to 500mA+). Ensure your power source (like a 3.3V regulator or USB port) can handle transient loads without causing sudden reboots (brownouts).

What specific aspect of the ESP32 (like FreeRTOS task management, deep sleep optimization, or peripheral configuration) would you like to dive deeper into?

What’s Xiaozhi-esp32 ?

Xiaozhi-ESP32 (小智 AI) is an active, open-source project designed to turn cheap ESP32 microcontroller boards into full-fledged, real-time AI voice companions and IoT assistants.   
xiaozhi.dev

Originally created by developer Xia-Ge (虾哥) under the MIT license, it has exploded in popularity within the maker and open-source hardware communities.   
XiaoZhi AI

Key Capabilities & Architecture
Low-Latency Streaming Voice Interaction:

Uses a streaming pipeline of ASR (Speech-to-Text) 
→ LLM (Large Language Model) 
→ TTS (Text-to-Speech) over WebSocket or MQTT/UDP protocols.   
GitHub

Integrates major LLMs (such as DeepSeek, Qwen, or OpenAI) and local/cloud TTS engines for fluid, natural conversation.   
Espressif Systems

Supports offline local wake-word detection (via ESP-SR) and server-side Acoustic Echo Cancellation (AEC) so users can interrupt the assistant while it speaks.   
Espressif Systems

Model Context Protocol (MCP) Integration:

Device-Side MCP: Allows the AI model to interact directly with hardware pins to toggle LEDs, play sounds, control servos, or query sensors.   
Espressif Systems

Cloud-Side MCP: Enables the AI to perform complex actions such as web searches, executing Home Assistant smart home automations, or querying databases.   
GitHub

Rich Visual Display Support:

Compatible with OLED, LCD, and AMOLED screens to show dynamic facial expressions (animations/emojis), live subtitles, battery levels, and system status.   
GitHub

Broad Hardware Ecosystem:

Works on various Espressif chips including ESP32-S3, ESP32-C3, and ESP32-P4.   
XiaoZhi AI

Runs on tens of commercial development boards (e.g., M5Stack CoreS3, Waveshare AMOLED modules, LilyGO, Seeed XIAO ESP32-S3) as well as custom DIY breadboard setups built with an I2S microphone (like INMP441) and MAX98357A speaker amp.   
GitHub

Flexible Cloud & Self-Hosted Backends:

Users can connect devices for free to the hosted console (xiaozhi.me).   
GitHub

Developers can self-host the backend via Docker or custom open-source server implementations written in Python (xiaozhi-esp32-server), Java, or Go to route voice traffic to local LLMs (like Ollama).   
GitHub

Why it Matters
Before Xiaozhi-ESP32, building a responsive, real-time voice-interactive AI hardware companion (like an Echo Dot combined with ChatGPT) required expensive SBCs like the Raspberry Pi or complex custom software stacks. Xiaozhi brings voice-first AI agents to low-cost ($5–$15) microcontrollers with minimal latency and high customization.

Say I want to connect to Deepseek, do I need to setup deepseek account and api key; or it’s managed already?

It depends on how you plan to connect your ESP32 to DeepSeek. There are two primary deployment setups:

Option 1: Official Xiaozhi Cloud Service (xiaozhi.me)
API key needed? No.

If you flash the standard Xiaozhi firmware and connect your ESP32 to the official Xiaozhi cloud backend:

You do not need to create a DeepSeek account or manage your own API key.   
xiaozhi.dev

You simply create a free account at xiaozhi.me and pair your ESP32 using the 6-digit code shown on your device's screen.   
Waveshare

DeepSeek is included out of the box as one of the default engine options in the Xiaozhi cloud dashboard.

Option 2: Self-Hosted Server (Docker / xiaozhi-esp32-server)
API key needed? Yes.

If you run your own backend server locally or on a VPS (e.g., using the open-source xiaozhi-esp32-server project):

You must supply your own DeepSeek API key.

You will need to create a platform account at platform.deepseek.com, add funds/credit, generate an API key, and paste that key into your server’s config.yaml or environment variables.

Option 3: Direct ESP32 HTTP Calls (No Xiaozhi framework)
API key needed? Yes.

If you are writing custom ESP32 C++ or MicroPython code to ping DeepSeek directly (bypassing the Xiaozhi voice stack):

You need a DeepSeek API key hardcoded into your firmware (or stored in NVS/EEPROM).   
Deepseek Ai Guide

DeepSeek uses an OpenAI-compatible API structure, so you send requests to [https://api.deepseek.com](https://api.deepseek.com) with the header Authorization: Bearer YOUR_DEEPSEEK_API_KEY.

What’s the limit or rate limit for Option 1

For Option 1 (Official Xiaozhi Cloud Service at xiaozhi.me), the system operates on a freemium shared pool model designed to protect their cloud infrastructure from high API bills.

1. Daily & Hourly Quota Limits
Free Tier Allocation: Free accounts receive a generous daily limit of free interactions/tokens to handle casual use and hardware testing.

Continuous Rate Throttling: Rather than a strict "requests per minute" cap, Xiaozhi uses dynamic token-bucket rate limiting. If you stream long continuous voice conversations without pausing, you will temporarily hit a cooldown delay (e.g., the device will say "Server is busy" or "Quota limit reached, please wait").

Peak Hours Congestion: During peak usage hours, free tier users share processing bandwidth. High-latency or queueing can occur when many users talk to the default DeepSeek/Qwen models at the same time.

2. Custom API Keys Inside the Web Console
If you hit the free tier limits on xiaozhi.me, you do not need to set up a self-hosted server to lift them:

Log into your account on the xiaozhi.me dashboard.

Go to your Device Settings / LLM Provider Configuration.

Toggle the provider from "Free Shared Model" to Custom API Key.

Paste your personal DeepSeek API key into the web dashboard.

Result: The official Xiaozhi cloud will still handle the streaming audio pipeline (ASR, TTS, and WebSocket connectivity), but it routes all text completions to your own DeepSeek account balance. This unlocks unlimited usage bounded only by DeepSeek's standard account concurrency limits.   
Deepseek Ai Guide

Summary Comparison
Setting	Official Free Pool	Official Web Console + Personal DeepSeek API Key
Cost	100% Free	Pay DeepSeek per token (~$0.14-$0.28 / 1M tokens)
Setup	1-click binding	Paste API Key in web dashboard
Limits	Daily soft caps & peak hour throttling	High scale (2,500+ concurrent requests)
Server Maintenance	None	None
Where does TTS happen?

TTS (Text-to-Speech) happens on the Cloud/Server side, not on the ESP32 chip itself.

How the Audio Flow Works
Plaintext
[ User Speaks ]
      │
      ▼
  (ESP32 Microphone)
      │  Encodes microphone audio (using Opus codec)
      ▼
  [ Cloud Server / Xiaozhi Server ]
      │  1. ASR converts voice to text
      │  2. DeepSeek generates text response
      │  3. TTS Engine converts text into audio stream
      ▼
  (Compressed Audio Stream over WebSocket/UDP)
      │
      ▼
  (ESP32 Speaker)
      │  Decodes Opus stream & plays through I2S amplifier
      ▼
[ User Hears AI ]
Why TTS is Processed on the Server
Processing Power & Memory: Generating natural, human-sounding AI voices (like EdgeTTS, Volcengine/Doubao TTS, or FishSpeech) requires massive computational power and deep learning models. The ESP32's microsecond-level processor and tiny SRAM (around 520 KB) cannot run these models locally.

Bandwidth Efficiency: The server converts text to compressed audio (Opus format) and streams it down to the board in real time.   
DeepWiki

What the ESP32 Actually Does: The ESP32 functions as a dumb audio terminal with a light processor:

It handles local wake-word detection (via ESP-SR).   
DeepWiki

It compresses microphone audio and sends it over Wi-Fi.   
DeepWiki

It receives the raw Opus audio packets from the server, decodes them to PCM format, and pushes them directly out to your I2S speaker amplifier.

Summary by Setup Mode
Official Cloud (xiaozhi.me): Xiaozhi's backend server generates the TTS audio using high-quality cloud voice models and streams it to your ESP32.

Self-Hosted Server: Your local computer or VPS runs the TTS engine (e.g., Edge-TTS or GPT-SoVITS) and streams the audio output to the ESP32.

No limit for official TTS?

No, the official TTS service on xiaozhi.me is also rate-limited.

The official cloud backend hosts the entire voice pipeline as a unified bundle (ASR + LLM + TTS). As a result, the TTS component follows the exact same operational caps as the rest of the free service.

How TTS Limits Work on the Official Cloud
Bundle Quota (All-in-One): On the default free tier, your daily usage cap covers the entire voice turn. When you hit your daily limits or trigger rate-throttling during peak hours, both speech-to-text generation and TTS audio output pause together.

Long Output Audio Caps: Server-side TTS engines (e.g., Volcengine, EdgeTTS, or Doubao) consume significant bandwidth and cloud compute. To protect the shared server pool, long LLM responses are capped to prevent continuous, multi-minute audio streams from exhausting bandwidth.

What Happens When You Add Your Own Custom API Keys?
If you paste your personal DeepSeek or Qwen API key into the xiaozhi.me dashboard:

LLM Limits Removed: The text completion portion moves entirely to your paid account balance.

TTS Service Tier: The cloud backend continues to synthesize the text into audio and stream Opus packets down to your ESP32. While your custom LLM key removes token bottlenecks, the default TTS service provided by xiaozhi.me still subject your account to reasonable usage bounds to prevent abuse.

Custom TTS Configuration: Within the xiaozhi.me web console, you can also select or supply API credentials for third-party cloud TTS providers (such as Volcengine/Doubao or FishSpeech). Doing so routes both the intelligence layer and the voice synthesis layer through your own provider accounts, completely bypassing all free-tier restrictions.

How does this implement context window? Say I chatted with it for many days, does the have a single context windows? Or reset periodically?

The Xiaozhi-ESP32 architecture does not use a single, continuously growing context window for multi-day conversations. If it did, your conversations would quickly exceed the LLM's token limit and become prohibitively expensive to run.

Instead, context management is handled entirely on the server side (either via xiaozhi.me or your self-hosted backend). The physical ESP32 board knows nothing about chat history—it simply sends the latest audio clip and plays back the returned audio.

Here is how context, history, and memory are managed under the hood:

1. Dynamic Rolling Context Window (Short-Term Memory)
During an active session, the server maintains a sliding window of recent conversation turns (usually the last 5 to 10 back-and-forth messages).

Token Truncation: As new messages come in, the oldest dialogue turns fall off the top of the prompt payload sent to the LLM.

Cost & Latency Control: Keeping the active context short ensures fast response times (critical for voice interactions) and keeps API token usage low.

2. Session Reset Mechanisms
The system resets or clears the active sliding context window under three main conditions:

Inactivity Timeout: If you stop talking to the board for a set period (typically 5 to 15 minutes), the server closes the WebSocket session and resets the active context. The next time you say the wake word, you start a fresh session.

Wake-Word / Button Reset: In many firmware builds, triggering a hard wake-word interrupt or long-pressing a button sends a explicit reset signal (session_id renewal) to clear the active chat memory.

Server-Side Memory Summarization: Advanced backends generate a high-level summary of key details (e.g., your name, preferences, discussed topics) before closing a session, saving those facts to a database rather than keeping full transcript logs in the context window.

3. Long-Term Memory via Model Context Protocol (MCP) & RAG
To remember details across multiple days without loading full transcripts into the LLM context window:

Database Storage: Conversation logs are saved to a server database linked to your device ID or user account.

Vector Search / Memory Tools: When you ask something about past conversations (e.g., "What did we talk about yesterday?"), the backend uses MCP tools or a RAG (Retrieval-Augmented Generation) pipeline to search past logs, retrieve only the relevant snippet, and inject it into the current prompt payload.

For long term and RAG, or bahaviou customization. is it supported by the xiaozhi cloud?or id better self host backend?

For RAG, long-term memory across days, and deep behavior customization, self-hosting the backend (xiaozhi-esp32-server) is strongly recommended.

While the official Xiaozhi cloud (xiaozhi.me) offers convenient lightweight customization, it lacks true RAG or cross-session persistent memory vector stores out of the box.

Comparison Matrix
Capability	Official Xiaozhi Cloud (xiaozhi.me)	Self-Hosted Server (xiaozhi-esp32-server)
System Prompt & Persona	✅ Web console field (personality, role, system instructions)	✅ Configurable via code or config.yaml
Short-Term Context	✅ Sliding window (clears on session disconnect)	✅ Fully configurable window size & buffer strategy
Device Control (MCP)	✅ Basic board control supported	✅ Deep control (Custom local & cloud-side MCP tools)
RAG (External Documents)	❌ Not natively supported	✅ Supported (Integrate LangChain, LlamaIndex, or Vector DBs)
Long-Term Memory	❌ Session-bound only	✅ Fully supported (Connect to Mem0, Zep, SQLite, or Redis)
Local / Private LLMs	❌ API key-based cloud models only	✅ Run Ollama, vLLM, local TTS (Edge-TTS, FishSpeech)
What You Can Customize on Official Cloud (xiaozhi.me)
If you only need moderate behavior tweaking, you can stick with the cloud:

Custom System Prompts: Define tone, primary language, constraints, and instructions in the dashboard.

Device Hardware Control (MCP): Trigger local ESP32 hardware (turn on LEDs, read pin voltages, run servos).   
Hackster.io

Custom Model Providers: Plug in your own API key for DeepSeek, Qwen, or OpenAI.

Why You Need to Self-Host for RAG & Long-Term Memory
Because the ESP32 chip only handles audio capture and playback, intelligence, history, and memory logic sit on the backend server:

Long-Term Memory Integration:
With a self-hosted Python server, you can hook the dialogue pipeline into memory layers (like Mem0, Zep, or an SQLite/Redis database). When the user speaks, the server extracts user preferences (e.g., "My name is Alex", "I drink green tea") and automatically retrieves those facts in future sessions days later.   
Ailog RAG

RAG (Knowledge Bases):
If you want Xiaozhi to answer queries about custom manuals, company data, or local documentation, you can add a vector database (e.g., ChromaDB, Qdrant, or Pinecone) into your self-hosted backend flow. The server performs semantic search before passing context to DeepSeek.   
Mem0
+ 1

Cloud-Side MCP (Tools & Agents):
Self-hosting allows your server to run custom python tools—such as checking your Google Calendar, triggering Home Assistant smart lights, or fetching real-time weather via API—and feed the output to the voice stream.   
Hackster.io

Recommended Next Steps
Quick Prototype: Start on xiaozhi.me to refine your prompt persona and confirm the hardware operates cleanly over Wi-Fi.

Production Build: Spin up xiaozhi-esp32-server using Docker on a local computer, Raspberry Pi, or cheap VPS, and extend its Python backend with RAG (LangChain/LlamaIndex) or a memory engine (Mem0).

Say I want to entirely block its Chinese I/O compacity and only allow it listen and speak English, how to achieve it?

To force your Xiaozhi-ESP32 device to exclusively listen, think, and speak in English—while completely shutting down its Chinese language capacity—you need to configure three layers of the pipeline: the Speech-to-Text (ASR) language constraint, the LLM System Prompt/Persona, and the Text-to-Speech (TTS) voice model.

Here is how to achieve it across different deployment setups.

Method 1: If using the Official Cloud Console (xiaozhi.me)
If you are using the default cloud backend, you can enforce English-only behavior directly in your device dashboard:

Log into the xiaozhi.me web console.

Select your registered ESP32 device and go to Device Settings.

Update the System Prompt (Persona):
Replace the default prompt with a strict language lock. Paste a prompt like:

You are a helpful voice assistant. You must ONLY understand, process, and respond in English. If the user speaks any other language, politely remind them in English that you only speak English. Never generate Chinese characters or output.

Set the ASR Language:
Look for the ASR (Speech-to-Text) Language setting and explicitly lock it to English (en) instead of Auto or Chinese. (If left on Auto, it may misinterpret English words with accents as Mandarin phonetics).

Change the TTS Voice:
Switch your TTS voice actor from a default Mandarin voice to an English-native TTS voice (e.g., standard Microsoft Edge English voices or platform-equivalent English speakers) so it never attempts to pronounce Chinese characters.

Method 2: If Self-Hosting (xiaozhi-esp32-server)
If you run your own backend server, you have total control over the prompt templates and engine parameters.

Step 1: Lock the System Prompt in Code / Config
In your server's configuration file or prompt initialization script, ensure the system message passed to DeepSeek explicitly forbids Chinese:

YAML
system_prompt: >
  You are an English-only voice assistant. You must communicate exclusively in English.
  Do not acknowledge, translate, or respond to any input given in Chinese or other languages.
  Keep your responses concise and optimized for spoken audio output.
Step 2: Force ASR Language Parameter
When the server sends audio chunks to the Speech-to-Text service (such as OpenAI Whisper or FunASR), hardcode the language parameter to English (language="en") rather than letting it auto-detect.

Step 3: Configure an English TTS Engine
Ensure your server-side TTS module (like Edge-TTS or Piper) uses an English locale (e.g., en-US-AriaNeural or en-GB-RyanNeural). If you feed English text into a Mandarin-optimized TTS model, it will produce heavily accented, broken pronunciation.

Can I set up a simulated environment to play around on my Mac, before my ordered hardware arrives? When hardwire arrives, I can migrate and deploy to it?

Yes, absolutely. You can run a fully simulated Xiaozhi environment right on your Mac using software emulation. This lets you configure your prompts, test the API responses, and experiment with the voice pipeline before your physical board arrives.

Once your hardware is delivered, you can simply flash the firmware onto the physical ESP32 and bind it to your configuration.

Here is how to set up a local simulation environment on your Mac.

Method: Running the Xiaozhi Server & Client Simulator Locally
Since the ESP32 itself is just a thin audio client talking to a backend server, you can run the open-source xiaozhi-esp32-server locally via Python/Docker, and use a browser-based or desktop software client to simulate the microphone and speaker.

1
Clone and Set Up the Server
Local Python/Docker setup
Open your Mac's Terminal and clone the open-source server repository to your local machine:

Bash
git clone https://github.com/7in-io/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server
Verification: Running ls in your terminal shows the project folders including config.yaml and server source scripts.

2
Configure Your API Keys and Language
5 min
Open the configuration file (config.yaml or .env) and input your DeepSeek API key, along with your English-only language constraints and TTS settings.
Verification: Your API keys are saved inside the config file and your language preference is set strictly to English.

3
Run the Server Locally
Terminal
Start the server locally (either via Docker Compose or directly through Python):

Bash
python -m pip install -r requirements.txt
python main.py
Verification: Your terminal displays logs indicating the server is running on http://localhost:8000 or the configured WebSocket port.

4
Connect a Software Simulator Client
Audio Test
Use a companion web client or python desktop audio client (such as the web audio simulator provided in the repository or a virtual microphone tool) pointing to ws://localhost:8000. This allows you to use your Mac's built-in microphone and speakers to talk to the AI backend.
Verification: Speaking into your Mac's mic triggers a response, and audio plays back through your Mac speakers.

How to Migrate to the Physical Hardware Later
When your physical ESP32-S3 board arrives, migration is seamless because the device profile is completely separate from the execution environment:

Flash the Firmware: Connect your ESP32 to your Mac via USB-C and use ESP-IDF or the web flash tool to install the standard Xiaozhi firmware.

Point to Your Server (or Cloud):

If you want to keep testing locally, configure the ESP32's Wi-Fi settings via its captive portal to point to your Mac's local IP address (ws://<YOUR_MAC_IP>:8000).

If you prefer switching to the official cloud, simply log into xiaozhi.me, scan the 6-digit code displayed on your ESP32's screen, and apply your English system prompt there.

Keep Your Logic: All the persona tuning, tool integrations (MCP), and prompt engineering you did on your Mac carry right over to the physical board instantly.

If you want help with specific tools or Docker setup: