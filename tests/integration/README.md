# Headless cross-platform test build

Cross-compiles the plugin to a **loadable Windows x64 DLL** using mingw-w64, so a
non-Windows host (CI, a Linux dev box, Wine) can exercise the real plugin data
pipeline end-to-end — not just the isolated pure functions in `../tests`.

This is **not** the shipping build. The shipping `.dlo` is built with MSVC via
`mxbmrp3.sln` (see the root `CLAUDE.md`). This is a parallel *portability*
configuration used only for testing.

> **Where the tests live and how to run them: [`../TESTING.md`](../TESTING.md).**
> This file covers only the cross-build itself — the build engine and how it
> diverges from the shipping MSVC build.

## What runs here

The cross-build powers the Layer-2 and Layer-3 tests (see `../TESTING.md`):

```
./run_tests.sh          # every doctest integration test in tests/ (smoke/race/sessions/director/reset/version)
./run_persist_test.sh   # settings round-trip property test
./run_fuzz.sh           # config-file survival fuzzing
./run_fuzz_callbacks.sh # DLL-boundary callback survival fuzzing
./run_perf.sh           # CPU performance baseline (50-rider grid)
```

117 translation units compile clean into a genuine PE32+ DLL exporting the full
PiBoSo plugin API. Under Wine it runs the real lifecycle: all managers
initialize, settings load/save round-trips, HUDs rebuild render primitives, and
the HTTP overlay server starts on :8080. Feature parity with the shipping build
**except** Discord Rich Presence and Aptabase analytics (see below). All of it is
wired into CI (`.github/workflows/tests.yml`).

## Requirements

```
sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 wine64
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
```

The **posix** thread variant is required (`std::thread`/`std::mutex`). `python3`
is needed for the config-fuzz / persist runners; `ccache` is optional.

## Build

```
./build.sh            # incremental parallel build -> build/mxbmrp3_test.dlo
./build.sh clean      # remove build/
./build.sh -B         # force full rebuild
```

`build.sh` is a thin wrapper over the `Makefile` (the real engine). Like Visual
Studio, it doesn't rebuild the whole thing every time:

- **Incremental** — `gcc -MMD` records each TU's header dependencies, so editing
  one `.cpp` (or a header) recompiles only the affected TUs + the link.
- **Parallel** — `make -j$(nproc)` compiles independent TUs concurrently.
- **ccache** (optional, `sudo apt-get install ccache`) — caches objects by
  content hash, so unchanged TUs are served instantly even after `make clean`.

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
It's compiled only by this build's source glob, not `mxbmrp3.vcxproj`.
