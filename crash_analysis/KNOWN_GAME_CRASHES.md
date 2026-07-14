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

*Newest build first. Catalogue current as of 2026-07-02.*

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
| `msvcr90.dll+0x0000000000036ede` | **Offline track-load crash (bad string in per-profile data)** | During track load -- while the track is loading, before you get on track. (Both captures so far were offline Testing, but that may just be where it happened to be seen.) | For the track that crashed, rename or move just its trainer/ghost file in Documents\PiBoSo\MX Bikes\profiles\[your profile]\trainers\ (named after the track), then re-test; keep it as a backup to restore after a patch. Only PiBoSo can fully fix the underlying parser. |

## Not MX Bikes — third-party software

*These faults are captured by the plugin's crash handler but are **not** MX Bikes or plugin bugs — they come from other software injected into the game. Identify them by the module on the crash stack; the exact fault offset is machine/Windows-specific, so don't match on it.*

| Identify by | Crash | When | Workaround |
|---|---|---|---|
| OBS's `graphics-hook64.dll` on the crash stack | **OBS game-capture hook crash** | Only when recording/streaming with OBS 'Game Capture'. Fires during a session; unrelated to riding. If you don't run OBS game capture you will not see this one. | This is OBS, not MX Bikes or the plugin. Update OBS; switch its capture from 'Game Capture' to 'Window Capture' or 'Display Capture'; don't hide/show (toggle) the Game Capture source mid-session; and try disabling VSync. See OBS issues #9168 / #11403 / #11003. |

---
*A blank workaround means none is known yet. The same crash can appear under several builds at
different offsets, because PiBoSo's compiler relocates code each build; the offset is
per-build while the underlying bug is the same. Full technical detail (faulting instruction, mechanism,
byte signature, reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump
or extend this list.*
