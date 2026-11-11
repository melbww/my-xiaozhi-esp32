# Documentation Maintenance

These docs were split from one long context file on 2026-11-09. The goal is
**current facts in focused pages, superseded material in `.archive/`**.

## Rules

1. **Start at [README.md](README.md)**. It is the single index and current-state
   page. Update its status table when the robot/server state changes.
2. **Put current facts in the owning focused doc.** Do not pile every new
   finding into the index or one log.
3. **Put dated narrative in `.archive/history/`.** Use one file per month,
   e.g. `.archive/history/2026-11-bringup-log.md`; add a short dated section
   only for experiments/incidents that are no longer current.
4. **Each focused doc starts with `Last verified: YYYY-MM-DD`.** Update it when
   you verify or change the facts in that doc.
5. **One topic per page.** If a page grows past ~250 lines, split it and add a
   link from the index.
6. **Link, don’t duplicate.** Cross-link related pages and the board README
   (`main/boards/desk-bot-01/README.md`).
7. **Use relative links** inside `my_exploration/`; use repo-relative paths in
   commands.
8. **Keep commands copy-pasteable.** Use the real serial port, IDF path, and
   server repo path; put host-specific values in one place.
9. **Archive, don’t delete.** When a doc stops providing active information,
   move it to `.archive/` with `git mv` and leave a pointer if it was linked
   externally. Do not delete a doc or artifact without asking.
10. **Server-specific detail lives in the server repo.** Link to
    `/Users/wenwu/work/xiaozhi-esp32-server` docs instead of copying large
    configs; keep only the robot-facing facts here.

## Where new information goes

| New information | Put it in |
|---|---|
| Pins, board revision, backup, partitions, eFuse | [robot/hardware.md](robot/hardware.md) |
| Build/flash/recovery/MCP tools | [robot/firmware.md](robot/firmware.md) |
| Server stack, OTA/WS, activation, logs | [server/README.md](server/README.md) |
| TTS, latency, barge-in, voice choices | [server/tts.md](server/tts.md) |
| Camera, USB power, vision pipeline | [camera/README.md](camera/README.md) |
| Framework layers, NVS persistence, OTA/cloud selection, display/wake-word wiring | [docs/framework-layers.md](docs/framework-layers.md) |
| Vendor BOM/wiring | [vendor/](vendor/) |
| Dated experiments / decisions (not current) | `.archive/history/YYYY-MM-*.md` |
| Background research / old guides | `.archive/reference/` |
| Deep vendor analysis / old web UI | `.archive/vendor/` |

## Update checklist

- [ ] Did I update the owning focused doc?
- [ ] Did I update `Last verified`?
- [ ] Did I update the status table in [README.md](README.md)?
- [ ] Did I archive superseded material under `.archive/` instead of leaving it active?
- [ ] Are links and file paths still valid?
- [ ] Did I avoid duplicating server/vendor docs?

## Related skill

The OpenCode skill **`desk-bot-01-docs`** encodes the same workflow for
future agents. If the layout changes, update the skill too.
