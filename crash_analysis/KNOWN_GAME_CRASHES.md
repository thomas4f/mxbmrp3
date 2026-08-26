<!-- GENERATED from known_game_crashes.json by tools/gen_known_crashes.py -- DO NOT EDIT BY HAND.
     Edit known_game_crashes.json, then run: python3 tools/gen_known_crashes.py -->
# Known MX Bikes crashes

A quick reference for working out which MX Bikes crash you hit and whether there's a
workaround. These are crashes in the game itself, caught by the MXBMRP3 plugin's crash
handler. They are not plugin bugs, and only PiBoSo (the game's developer) can fix them.
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

*Newest build first. Catalogue current as of 2026-08-21.*

### Build `0x6A21833D` · beta21e · built 2026-06-04 · newest seen

| Fault offset | Crash | When | Workaround |
|---|---|---|---|
| `mxbikes.exe+0x00000000001f1923` | **Physics contact blow-up (bad table index)** | While riding. Fires on a physics contact -- anything from a hard hit into something solid (e.g. a fence) to ordinary ground contact like landing or riding up a hill. You do not have to hit anything. | - |
| `mxbikes.exe+0x000000000011d753` | **Session teardown null dereference (leaving a session / connecting)** | Not while riding. When leaving a session, connecting to a server, or sitting idle between sessions -- it strikes during the game's teardown back to the menus, so it looks like a 'crashed while doing nothing / between races' crash. | - |
| `mxbikes.exe+0x000000000024085c` | **OpenGL render buffer write (vertex-fill overrun)** | While the game is rendering during a session (warmup or race). Not tied to an impact, a particular track, or the menus. | Suspected ReShade-related: every captured instance is on a machine running ReShade as an OpenGL injector (a proxy opengl32.dll in the MX Bikes folder), which sits directly in this render path. Remove or disable ReShade (delete/rename that opengl32.dll) and re-test -- unconfirmed, but the strongest lead. Also worth trying: a clean GPU-driver reinstall (DDU + a different NVIDIA driver version) and disabling overlays (Steam / Discord / GeForce Experience). |
| `mxbikes.exe+0x00000000001fed9e` | **Out-of-range array index read at session end** | At the end of an online session, as the race finishes and the game returns you to the menus. | - |
| `mxbikes.exe+0x000000000003b940` | **Null dereference in network / session-end code** | At the end of an online race, as the results come in and the game tears the session down. | - |
| `mxbikes.exe+0x00000000002a42f0` | **64-bit pointer truncated to 32-bit (stack above 4 GB)** | Seemingly at random, and only on some launches -- it depends where Windows happens to place the game's stack in memory that run (ASLR), not on anything you do. Tends to hit early in a session. | Force the game's stack to load below 4 GB via Windows Exploit Protection: Windows Security -> App & browser control -> Exploit protection settings -> Program settings -> add mxbikes.exe -> set 'High-entropy ASLR' to Override + Off. That keeps the truncated pointer valid so it stops faulting. Only PiBoSo can fix the underlying truncation. |
| `mxbikes.exe+0x00000000000045b0` | **Session-teardown crash via input/message pump (stale pointer)** | Not while riding -- when leaving or ending a session, during the game's teardown back to the menus. Looks like a 'crashed after the race / between sessions' crash. | - |

### Build-independent (faults in a shared component, same offset on every build)

| Fault offset | Crash | When | Workaround |
|---|---|---|---|
| `msvcr90.dll+0x0000000000036ede` | **Offline track-load crash (bad string in per-profile data)** | During track load -- while the track is loading, before you get on track. May present as a crash, as the game hanging at the loading screen, or as a Windows BEX64 / STATUS_STACK_BUFFER_OVERRUN report with no dump. All three are the same defect; which one you get depends on the process's memory layout that launch. | Delete or rename the crashing track's trainer file under Documents\PiBoSo\MX Bikes\profiles\[your profile]\trainers\ -- the only workaround with evidence behind it, and it costs you that track's lap. Riding the track again re-creates the trainer, which may be damaged again. Do NOT expect a file-content check to tell you which trainers are safe: the outcome depends on the LOADING process's memory layout as well as the file, so the same file crashes with one plugin set and loads cleanly with another. A tool that scanned for a byte signature was tried and withdrawn -- it flagged one damaged shape and missed the minimal repro entirely (see 'trigger'). Removing plugins does not rescue an already-damaged file either; poisoned files have been observed failing with no plugins loaded at all. Only PiBoSo can fix the serialiser. CONFIRMED REPAIR (2026-08-21): zero the name slack -- everything from the bike name's NUL terminator at 0x62.. up to 0x92 -- which puts zeros exactly where every trainer that loads has zeros, needs no donor file, and leaves the rest of the file untouched. Implemented in tools/trnfix/web/index.html, a single offline HTML page. (A native .exe carried the same logic and the byte-level instruments -- graft bisection, leaked-pointer scan -- and was deleted once the bisection was finished; git history has it.) Tested in game on the bisected file and it loads; one file and one reporter, so judge a new sample over ~5 loads. Deleting or renaming the trainer remains the fallback that cannot fail. Grafting 0x0-0x86 from a trainer that loads is the proven-in-game version of the same repair, but it needs a good donor for the same track and bike. |

## Not MX Bikes - third-party software

*These faults are captured by the plugin's crash handler but are **not** MX Bikes or plugin bugs - they come from other software injected into the game. Identify them by the module on the crash stack; the exact fault offset is machine/Windows-specific, so don't match on it.*

| Identify by | Crash | When | Workaround |
|---|---|---|---|
| OBS's `graphics-hook64.dll` on the crash stack | **OBS game-capture hook crash** | Only when recording/streaming with OBS 'Game Capture'. Fires during a session; unrelated to riding. If you don't run OBS game capture you will not see this one. | This is OBS, not MX Bikes or the plugin. Update OBS; switch its capture from 'Game Capture' to 'Window Capture' or 'Display Capture'; don't hide/show (toggle) the Game Capture source mid-session; and try disabling VSync. See OBS issues #9168 / #11403 / #11003. |

---
*A blank workaround means none is known yet. The same crash can appear under several builds at
different offsets, because PiBoSo's compiler relocates code each build; the offset is
per-build while the underlying bug is the same. Full technical detail (faulting instruction, mechanism,
byte signature, reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump
or extend this list.*
