---
name: desk-bot-01-docs
description: Maintain the Desk Bot 01 ESP32-S3 exploration documentation in /Users/wenwu/work/my-xiaozhi-esp32/my_exploration. Use when adding or updating robot hardware/firmware/server/TTS/camera/vendor findings, archiving old exploration, fixing links, or deciding where a new fact belongs.
---

# Desk Bot 01 Docs

Keep the documentation **current, focused, and easy to provide as context**.
The active docs are a small index plus focused pages; anything superseded goes
to `.archive/`.

## Start here

1. Read `/Users/wenwu/work/my-xiaozhi-esp32/my_exploration/README.md`.
2. Read `/Users/wenwu/work/my-xiaozhi-esp32/my_exploration/MAINTENANCE.md`.
3. Find the owning page below. Do not add old exploration to the active docs.

## Active layout

| Path | Owns |
|---|---|
| `my_exploration/README.md` | Index, current state, quick start, critical rules |
| `my_exploration/robot/hardware.md` | Device ID, USB ports, eFuse, backup, partitions, verified pins |
| `my_exploration/robot/firmware.md` | Build, flash, recovery, MCP tools, takeover checklist |
| `my_exploration/server/README.md` | Self-hosted stack, OTA/WS, activation, logs |
| `my_exploration/server/tts.md` | TTS choices, latency, local `say`, barge-in |
| `my_exploration/camera/README.md` | Current camera status, firmware support, vision |
| `my_exploration/vendor/README.md` | Vendor BOM/wiring and inventory |
| `my_exploration/MAINTENANCE.md` | Doc conventions and update checklist |
| `my_exploration/tools/` | Helper scripts (e.g. serial capture) |

## Archive layout

| Path | Holds |
|---|---|
| `my_exploration/.archive/README.md` | Index of archived material |
| `.archive/history/` | Dated bring-up narrative / old decisions |
| `.archive/reference/` | Old guides and background research |
| `.archive/vendor/` | Reverse engineering, vendor web UI, C3 notes |
| `.archive/camera/` | Old USB-host test, detailed formats, camera images |
| `.archive/vendor-artifacts/` | Large/local vendor binaries, disassembly, web snapshots |
| `.archive/vendor-assets/` | Large RAR and incompatible robot-dog image |

Server-side code/config lives in `/Users/wenwu/work/xiaozhi-esp32-server`.
Link to it; do not copy large configs here. Local TTS service:
`my_explorations/local-say-tts/` in that repo.

## Workflow

1. **Classify the fact.** Current hardware → `robot/hardware.md`; build/flash →
   `robot/firmware.md`; server/OTA/activation → `server/README.md`; TTS/latency/
   barge-in → `server/tts.md`; camera → `camera/README.md`; vendor BOM/wiring →
   `vendor/`; current status → `README.md`.
2. **Edit the owning page**, keeping it under ~250 lines. Add/update
   `Last verified: YYYY-MM-DD`.
3. **Archive superseded material.** Move old deep dives, dated narratives, and
   background guides to the matching `.archive/` subfolder with `git mv`, and
   add/update `.archive/README.md`.
4. **Update the index** (`README.md`) if current state, active docs, or archive
   links changed.
5. **Check links and paths** with `rg`; update the old-path stub
   `desk-bot-01-project-context.md` if a target moved.
6. **Do not delete** docs or artifacts without asking. Move them to `.archive/`
   and leave a pointer if they were linked externally.

## Rules

- Active docs contain current facts only; `.archive/` contains history.
- Do not duplicate server/vendor docs; link instead.
- Keep commands copy-pasteable with the real serial port, IDF path, and server
  repo path.
- Large binaries stay local-only: `*.bin`, `.archive/vendor-artifacts/irom.asm`,
  `.archive/vendor-assets/*.rar` are gitignored.
- Critical safety rules belong in `README.md` and `robot/firmware.md`:
  no merged image at `0x0`, preserve NVS/`id`, no `erase-flash`, do not flash
  the vendor robot-dog image.
- If the layout changes, update this skill and `MAINTENANCE.md`.

## Useful checks

```sh
cd /Users/wenwu/work/my-xiaozhi-esp32
git status --short
git diff --cached --check
rg -n "my_exploration/(history|reference|vendor/artifacts|vendor/assets)" my_exploration --glob '!my_exploration/.archive/**'
python3 - <<'PY'
import re
from pathlib import Path
bad=[]
for p in Path('my_exploration').rglob('*.md'):
    for m in re.finditer(r'\[[^\]]*\]\(([^)]+)\)', p.read_text()):
        t=m.group(1).strip()
        if t.startswith(('http://','https://','#','mailto:')): continue
        t=t.split('#',1)[0]
        if t and not (p.parent/t).resolve().exists(): bad.append((str(p),t))
print('broken links:',len(bad))
PY
```

Keep the active set small enough that a new agent can read `README.md` plus one
focused page and start working.
