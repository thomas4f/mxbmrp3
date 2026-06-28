<!-- GENERATED from known_game_crashes.json by tools/gen_known_crashes.py -- DO NOT EDIT BY HAND.
     Edit known_game_crashes.json, then run: python3 tools/gen_known_crashes.py -->
# Known MX Bikes crashes

A quick reference for working out **which MX Bikes crash you hit and whether there's a
workaround**. These are crashes in the game itself, caught by the MXBMRP3 plugin's crash
handler. They are **not plugin bugs**, and only PiBoSo (the game's developer) can fix them.
(A few fault inside a shared component such as the Visual C++ runtime, but the bad input
still comes from the game.)


## Which crash did you hit?

1. Open **Event Viewer** (Start → type "Event Viewer") → **Windows Logs → Application**.
2. Find the most recent **Error** whose *Faulting application name* is `mxbikes.exe`
   (Event ID 1000).
3. Note the **Faulting module name** and **Fault offset** (e.g. `mxbikes.exe` +
   `0x00000000001f1923`, or `msvcr90.dll` + `0x0000000000036ede`).
4. Find that `module + offset` in the tables below. The offset is written exactly as Event
   Viewer shows it (16 zero-padded hex digits), so you can paste the **Fault offset** value
   straight into your browser's Find (Ctrl+F) to jump to it. Each offset belongs to one game
   build, so it appears in exactly one section.

**Can't find it?** The plugin also saves a crash dump to
`Documents\PiBoSo\MX Bikes\mxbmrp3\crashes\`. Send that over and it can be identified
straight from the dump. It's matched by the crash's machine code, which stays the same even
when the offset moves between builds, then added here.


## Crashes by build

*Newest build first. Catalogue current as of 2026-06-26.*

### Build `0x6A21833D` · beta21e · built 2026-06-04 · newest seen

| Fault offset | Crash | When | Workaround |
|---|---|---|---|
| `mxbikes.exe+0x00000000001f1923` | **Physics contact blow-up (bad table index)** | During riding -- both hard impacts with a solid object (e.g. a fence) AND ordinary ground/terrain contact with nothing hit (video-confirmed riding up a hill, no collision). Hitting an object is NOT required. | - |

---
*A blank workaround means none is known yet. The same crash can appear under several builds at
different offsets, because PiBoSo's compiler relocates code each build; the offset is
per-build while the underlying bug is the same. Full technical detail (faulting instruction, mechanism,
byte signature, reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump
or extend this list.*
