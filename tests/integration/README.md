# Headless cross-platform test build

Cross-compiles the plugin to a **loadable Windows x64 DLL** using mingw-w64, so a
non-Windows host (CI, a Linux dev box, Wine) can exercise the real plugin data
pipeline end-to-end — not just the isolated pure functions in `../unit`.

This is **not** the shipping build. The shipping `.dlo` is built with MSVC (see
the root `CLAUDE.md`). This is a parallel *portability* configuration used only
for testing.

> **Where the tests live and how to run them: [`../../TESTING.md`](../../TESTING.md).**
> This file covers only the cross-build itself — the build engine and how it
> diverges from the shipping MSVC build.

## What runs here

The cross-build powers the Layer-2 and Layer-3 tests (see `../../TESTING.md`):

```
./run_tests.sh          # every doctest integration test in tests/ (smoke/race/sessions/director/reset/version)
./run_persist_test.sh   # settings round-trip property test
./run_fuzz.sh           # config-file survival fuzzing
./run_fuzz_callbacks.sh # DLL-boundary callback survival fuzzing
./run_perf.sh           # CPU performance baseline (50-rider grid)
./run_installer_test.sh # NSIS installer install/uninstall mechanics (makensis + Wine)
./run_tape_bench.sh     # per-HUD render footprint over a real tape (inspection, not gated)
```

This directory also holds the **enforced invariant checks** (compile/grep
passes, no Wine, CI fails on violations): `check_game_configs.sh` (GPB/KRP
feature-macro syntax), `check_visibility_gates.sh` (HUD `isVisibleAnySurface()`
gates), `check_api_guards.sh` (DLL-export exception barriers),
`check_thread_safety.sh` (clang `-Wthread-safety` over the annotated mutexes),
`check_mt_flags.sh` (a plain `bool` in a thread-owning class),
`check_hud_raw_cache.sh` (raw `Unified::` members cached in a HUD),
`check_style.sh` (tabs/trailing-WS/CRLF/final newline) and
`check_session_hook.sh` (the SessionStart hook's own behaviour).
Each script's header documents its invariant and escape-hatch annotation.

Every `*.cpp` under `core/`, `handlers/`, `hud/`, `diagnostics/` (minus
`discord_manager.cpp`, which `mxbmrp3/CMakeLists.txt` drops under
`MXBMRP3_TEST_BUILD` because `GAME_HAS_DISCORD` is 0 there and the TU would only
drag the SDK in), plus `mxb_api.cpp` and the miniz `.c` files, compiles clean
into a genuine PE32+ DLL exporting the full PiBoSo plugin API — ~150 translation
units. `build.sh` prints the exported-symbol count on each link; a sudden drop
means a TU quietly stopped being compiled. Under Wine it runs the real lifecycle:
all managers initialize, settings load/save round-trips, HUDs rebuild render
primitives, and the HTTP overlay server starts on :8080. Feature parity with the
shipping build **except** Discord Rich Presence and Aptabase analytics (see
below). All of it is wired into CI (`.github/workflows/tests.yml`).

## Requirements

```
./tools/install_deps.sh mingw wine     # from the repo root
```

`tools/install_deps.sh` is the single source of truth for the toolchain (CI, the
SessionStart hook and `DEVELOPMENT.md` all call it) and applies the two fixups
that are easy to miss by hand: the **posix** mingw threading alternative
(`std::thread`/`std::mutex` need it) and the `/usr/bin/wine` launcher. `python3`
is needed for the config-fuzz / persist runners; `ccache` is optional but
installed with the `mingw` group.

## Build

```
./build.sh            # incremental parallel build -> build/mxbmrp3_test.dlo
./build.sh clean      # remove the build tree and the DLL
./build.sh -B         # force full rebuild
```

`build.sh` is a thin wrapper over **CMake**, which replaced the Makefile that
used to live here — that Makefile was a second, independent description of the
same source tree sitting alongside the vcxproj's explicit list, with nothing
comparing the two. Both are gone: `mxbmrp3/CMakeLists.txt` is the single
definition for every toolchain. The build tree is `build/cross/`, configured from
the `cross` preset so the toolchain file and `MXBMRP3_TEST_BUILD` live only in
`CMakePresets.json`. It doesn't rebuild everything every time:

- **Incremental** — CMake/Ninja-style dependency tracking recompiles only the
  affected TUs + the link when you edit one `.cpp` or header.
- **Parallel** — the build runs at `-j$(nproc)`.
- **ccache** (optional) — caches objects by content hash, so unchanged TUs are
  served instantly even after a clean.

Approximate timings (4 cores):

| Scenario | Time |
|---|---|
| Clean build, cold cache | ~120s |
| One `.cpp` changed | ~1s |
| Clean rebuild, warm ccache | ~1s |

(A flat "recompile everything serially" script was ~370s every time.)

## How it differs from the MSVC build

All divergences are gated in-source by `MXBMRP3_TEST_BUILD` or `_MSC_VER`, so
**the shipping MSVC build is byte-for-byte unchanged**:

| Area | MSVC (shipping) | This build | Why |
|---|---|---|---|
| Discord Rich Presence | on | **off** | `std::thread::native_handle → HANDLE` cast assumes win32 threads |
| Aptabase analytics | on | **off** | external service; adds nothing under test |
| Steam friends | on | **on** | SEH FFI wrappers made portable; runtime hook is inert without `steam_api64.dll` (as under Wine) |
| XInput controller *name* lookup | WinRT | no-op | mingw ships no WinRT headers; real XInput state is unaffected |
| SEH crash guards (`__try/__except`) | native SEH | run unguarded | SEH is MSVC-only; a compiler shim keeps the wrappers compiling |
| `Xinput.h` include | as-is | case shim | Linux is case-sensitive; mingw ships lowercase `xinput.h` |

Source changes that support this (all no-ops on MSVC):
- `game/game_config.h` — `MXBMRP3_TEST_BUILD` disables Discord + analytics
- `core/seh_compat.h` — portable `SEH_TRY` / `SEH_EXCEPT_ALL` (real SEH on MSVC,
  runs unguarded elsewhere)
- `core/steam_friends_manager.cpp`, `handlers/spectate_handler.cpp` — use those
  macros instead of raw `__try/__except`
- `core/xinput_reader.cpp` — WinRT name lookup behind `#ifdef _MSC_VER`
- `hud/timing_hud.cpp` — added missing `#include <algorithm>` (a real latent
  bug: it was relying on a transitive MSVC include)
- `tests/integration/shim/Xinput.h` — case-only forwarding header (no source edit)

## Test-only exports

`core/test_hooks.cpp` adds `MXBMRP3_Test_*` exports (start the web server, reset
settings, compare versions, force a save) used by the tests. The whole file is
gated on `MXBMRP3_TEST_BUILD`, so these exports **never exist in a shipping DLL**.
It's compiled only under `MXBMRP3_TEST_BUILD`; mxbmrp3/CMakeLists.txt removes it
from the source list for every shipping target.
