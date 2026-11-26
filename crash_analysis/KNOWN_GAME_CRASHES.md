# Known MX Bikes crashes

A quick reference for working out which MX Bikes crash you hit and whether there's a
workaround. These are crashes in the game itself, caught by the MXBMRP3 plugin's crash
handler. They are not plugin bugs, and only PiBoSo (the game's developer) can fix them.
(A few fault inside a shared component such as the Visual C++ runtime, but the bad input
still comes from the game.)

<sub>Generated from `crash_analysis/known_game_crashes.json` by `tools/gen_known_crashes.py` - do not edit. Edit the JSON, then run `python3 tools/gen_known_crashes.py`.</sub>


## Which crash did you hit?

1. Open **Event Viewer** (Start → type "Event Viewer") → **Windows Logs → Application**.
2. Find the most recent **Error** whose *Faulting application name* is `mxbikes.exe`
   (Event ID 1000).
3. Note the **Faulting module name** and **Fault offset** (e.g. `mxbikes.exe` +
   `0x00000000001f1b63`, or `msvcr90.dll` + `0x0000000000036ede`).
4. Find that `module + offset` in the table below. The offset is written exactly as Event
   Viewer shows it (16 zero-padded hex digits), so you can paste the **Fault offset** value
   straight into your browser's Find (Ctrl+F) to jump to it. On an older game build, open
   **Older builds** at the end first.

**Can't find it?** The plugin also saves a crash dump to
`Documents\PiBoSo\MX Bikes\mxbmrp3\crashes\`. Send that over privately (it can contain
your Windows user name, computer name and recent chat lines) and it can be identified
straight from the dump. It's matched by the crash's machine code, which stays the same even
when the offset moves between builds, then added here.


## Crashes on the current build

*Current build: beta21f, `0x6B032CE7`, built 2026-11-22. Crashes in a shared component such as the Visual C++ runtime have the same offset on every build. Older builds are folded at the end.*

| Fault offset | Crash |
|---|---|
| `mxbikes.exe+0x00000000001f1b63` | [Physics contact blow-up (bad table index)](#physics-contact-blow-up-bad-table-index) |
| `mxbikes.exe+0x000000000011d6d3` | [Session teardown null dereference (leaving a session / connecting)](#session-teardown-null-dereference-leaving-a-session--connecting) |
| `mxbikes.exe+0x0000000000240a9c` | [OpenGL render buffer write (vertex-fill overrun)](#opengl-render-buffer-write-vertex-fill-overrun) |
| `mxbikes.exe+0x00000000001fefde` | [Out-of-range array index read at session end](#out-of-range-array-index-read-at-session-end) |
| `mxbikes.exe+0x000000000003b830` | [Null dereference in network / session-end code](#null-dereference-in-network--session-end-code) |
| `mxbikes.exe+0x00000000002a4530` | [64-bit pointer truncated to 32-bit (stack above 4 GB)](#64-bit-pointer-truncated-to-32-bit-stack-above-4-gb) |
| `mxbikes.exe+0x00000000000045b0` | [Session-teardown crash via input/message pump (stale pointer)](#session-teardown-crash-via-inputmessage-pump-stale-pointer) |
| `msvcr90.dll+0x0000000000036ede` | [Offline track-load crash (bad string in per-profile data)](#offline-track-load-crash-bad-string-in-per-profile-data) |

### Physics contact blow-up (bad table index)

**Fault offset:** `mxbikes.exe+0x00000000001f1b63`

**When:** While riding. Fires on a physics contact -- anything from a hard hit into something solid (e.g. a fence) to ordinary ground contact like landing or riding up a hill. You do not have to hit anything.

**What to do:** None known. Only PiBoSo can fix it.

### Session teardown null dereference (leaving a session / connecting)

**Fault offset:** `mxbikes.exe+0x000000000011d6d3`

**When:** Not while riding. When leaving a session, connecting to a server, or sitting idle between sessions -- it strikes during the game's teardown back to the menus, so it looks like a 'crashed while doing nothing / between races' crash.

**What to do:** None known. Only PiBoSo can fix it.

### OpenGL render buffer write (vertex-fill overrun)

**Fault offset:** `mxbikes.exe+0x0000000000240a9c`

**When:** While the game is rendering during a session (warmup or race). Not tied to an impact, a particular track, or the menus.

**What to do:** Suspected ReShade-related: every captured instance is on a machine running ReShade as an OpenGL injector (a proxy opengl32.dll in the MX Bikes folder), which sits directly in this render path. Remove or disable ReShade (delete/rename that opengl32.dll) and re-test -- unconfirmed, but the strongest lead. Also worth trying: a clean GPU-driver reinstall (DDU + a different NVIDIA driver version) and disabling overlays (Steam / Discord / GeForce Experience).

### Out-of-range array index read at session end

**Fault offset:** `mxbikes.exe+0x00000000001fefde`

**When:** At the end of an online session, as the race finishes and the game returns you to the menus.

**What to do:** None known. Only PiBoSo can fix it.

### Null dereference in network / session-end code

**Fault offset:** `mxbikes.exe+0x000000000003b830`

**When:** At the end of an online race, as the results come in and the game tears the session down.

**What to do:** None known. Only PiBoSo can fix it.

### 64-bit pointer truncated to 32-bit (stack above 4 GB)

**Fault offset:** `mxbikes.exe+0x00000000002a4530`

**When:** Seemingly at random, and only on some launches -- it depends where Windows happens to place the game's stack in memory that run (ASLR), not on anything you do. Tends to hit early in a session.

**What to do:** Force the game's stack to load below 4 GB via Windows Exploit Protection: Windows Security -> App & browser control -> Exploit protection settings -> Program settings -> add mxbikes.exe -> set 'High-entropy ASLR' to Override + Off. That keeps the truncated pointer valid so it stops faulting. Only PiBoSo can fix the underlying truncation.

### Session-teardown crash via input/message pump (stale pointer)

**Fault offset:** `mxbikes.exe+0x00000000000045b0`

**When:** Not while riding -- when leaving or ending a session, during the game's teardown back to the menus. Looks like a 'crashed after the race / between sessions' crash.

**What to do:** None known. Only PiBoSo can fix it.

### Offline track-load crash (bad string in per-profile data)

**Fault offset:** `msvcr90.dll+0x0000000000036ede`

**When:** During track load -- while the track is loading, before you get on track. May present as a crash, as the game hanging at the loading screen, or as a Windows BEX64 / STATUS_STACK_BUFFER_OVERRUN report with no dump. All three are the same defect; which one you get depends on the process's memory layout that launch.

**What to do:** Repair the crashing track's trainer file with the trainer repair page (`tools/trnfix/web/index.html` in the plugin's repository; it runs offline in your browser and only rewrites the damaged bytes). Or delete or rename that file under `Documents\PiBoSo\MX Bikes\profiles\[your profile]\trainers\`, which always works but loses that track's best lap. Riding the track again can damage the new trainer too. Removing plugins does not fix an already damaged file.

## Not MX Bikes - third-party software

*These faults are captured by the plugin's crash handler but are **not** MX Bikes or plugin bugs - they come from other software injected into the game. Identify them by the module on the crash stack; the exact fault offset is machine/Windows-specific, so don't match on it.*

### OBS game-capture hook crash

**Identify by:** OBS's `graphics-hook64.dll` on the crash stack

**When:** Only when recording/streaming with OBS 'Game Capture'. Fires during a session; unrelated to riding. If you don't run OBS game capture you will not see this one.

**What to do:** This is OBS, not MX Bikes or the plugin. Update OBS; switch its capture from 'Game Capture' to 'Window Capture' or 'Display Capture'; don't hide/show (toggle) the Game Capture source mid-session; and try disabling VSync. See OBS issues #9168 / #11403 / #11003.

## Older builds

<details>
<summary>Fault offsets on earlier game builds, for anyone still running one. The crashes are the same ones described above.</summary>

**beta21f, `0x6AF5803B`, built 2026-11-12**

| Fault offset | Crash |
|---|---|
| `mxbikes.exe+0x00000000001f2553` | [Physics contact blow-up (bad table index)](#physics-contact-blow-up-bad-table-index) |
| `mxbikes.exe+0x000000000011d733` | [Session teardown null dereference (leaving a session / connecting)](#session-teardown-null-dereference-leaving-a-session--connecting) |
| `mxbikes.exe+0x000000000024148c` | [OpenGL render buffer write (vertex-fill overrun)](#opengl-render-buffer-write-vertex-fill-overrun) |
| `mxbikes.exe+0x00000000001ff9ce` | [Out-of-range array index read at session end](#out-of-range-array-index-read-at-session-end) |
| `mxbikes.exe+0x000000000003b8a0` | [Null dereference in network / session-end code](#null-dereference-in-network--session-end-code) |
| `mxbikes.exe+0x00000000002a4f40` | [64-bit pointer truncated to 32-bit (stack above 4 GB)](#64-bit-pointer-truncated-to-32-bit-stack-above-4-gb) |
| `mxbikes.exe+0x00000000000045b0` | [Session-teardown crash via input/message pump (stale pointer)](#session-teardown-crash-via-inputmessage-pump-stale-pointer) |

**beta21f, `0x6ADE6D44`, built 2026-10-25**

| Fault offset | Crash |
|---|---|
| `mxbikes.exe+0x00000000001f2f83` | [Physics contact blow-up (bad table index)](#physics-contact-blow-up-bad-table-index) |
| `mxbikes.exe+0x000000000011d7c3` | [Session teardown null dereference (leaving a session / connecting)](#session-teardown-null-dereference-leaving-a-session--connecting) |
| `mxbikes.exe+0x0000000000241ebc` | [OpenGL render buffer write (vertex-fill overrun)](#opengl-render-buffer-write-vertex-fill-overrun) |
| `mxbikes.exe+0x00000000002003fe` | [Out-of-range array index read at session end](#out-of-range-array-index-read-at-session-end) |
| `mxbikes.exe+0x000000000003b8a0` | [Null dereference in network / session-end code](#null-dereference-in-network--session-end-code) |
| `mxbikes.exe+0x00000000002a5950` | [64-bit pointer truncated to 32-bit (stack above 4 GB)](#64-bit-pointer-truncated-to-32-bit-stack-above-4-gb) |
| `mxbikes.exe+0x00000000000045b0` | [Session-teardown crash via input/message pump (stale pointer)](#session-teardown-crash-via-inputmessage-pump-stale-pointer) |

**beta21e, `0x6A21833D`, built 2026-06-04**

| Fault offset | Crash |
|---|---|
| `mxbikes.exe+0x00000000001f1923` | [Physics contact blow-up (bad table index)](#physics-contact-blow-up-bad-table-index) |
| `mxbikes.exe+0x000000000011d753` | [Session teardown null dereference (leaving a session / connecting)](#session-teardown-null-dereference-leaving-a-session--connecting) |
| `mxbikes.exe+0x000000000024085c` | [OpenGL render buffer write (vertex-fill overrun)](#opengl-render-buffer-write-vertex-fill-overrun) |
| `mxbikes.exe+0x00000000001fed9e` | [Out-of-range array index read at session end](#out-of-range-array-index-read-at-session-end) |
| `mxbikes.exe+0x000000000003b940` | [Null dereference in network / session-end code](#null-dereference-in-network--session-end-code) |
| `mxbikes.exe+0x00000000002a42f0` | [64-bit pointer truncated to 32-bit (stack above 4 GB)](#64-bit-pointer-truncated-to-32-bit-stack-above-4-gb) |
| `mxbikes.exe+0x00000000000045b0` | [Session-teardown crash via input/message pump (stale pointer)](#session-teardown-crash-via-inputmessage-pump-stale-pointer) |

</details>

---
*Technical detail for each crash (faulting instruction, mechanism, byte signature,
reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump or extend
this list.*
