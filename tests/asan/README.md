# Memory-safety (AddressSanitizer) testing

This directory answers one question: **is the plugin corrupting memory?**

Two shipped crashes on 1.27.4.39 —

| fault | resolves to |
|---|---|
| `mxbmrp3.dlo+0x378d8` | `std::_Tree::_Erase_tree` freeing `PluginData::m_leaderTimingPoints` |
| `mxbmrp3.dlo+0xeaab4` | `StatsManager::save()` iterating `m_bikeOdometers` |

— were access violations in **innocent heap walks** (a `std::map` teardown; a
`std::map` iteration). That's the signature of a **heap corruption**: some earlier
write trampled a heap block, and the crash only surfaced later when unrelated code
walked the damaged region. A bare `module+offset` (all our analytics captures)
shows the *victim*, never the *writer* — and at ~0.17 % of sessions the crash is
far too rare to reproduce on demand.

**AddressSanitizer (ASan) closes that gap.** It instruments every memory access at
build time and faults **at the corrupting write**, with the writing *and*
allocating call stacks. So instead of reproducing a 1-in-600 wild crash, we run the
suspect code under ASan over adversarial + realistic input and let the sanitizer
prove the negative (clean) or hand us the culprit line (a hit). See the main
discussion for why this is the right tool; the short version: it converts an
intractable "catch the rare crash" into a tractable "prove the code is clean."

There are two layers, by necessity of the toolchain.

---

## Layer 1 — native ASan (runs here + in CI)

Two complementary pieces, both host g++/clang + `libasan` (no game, no Windows, no
Wine), both fast enough to gate every push. The CI `memory-safety` job runs both.

**(a) The whole unit suite under ASan** — reuses the existing pure-logic tests, so
every portable surface they already exercise (`sanitizeUntrusted`, `fitText`,
formatting, hex/config parsing, notice priority, session-charts math, director
airtime, update-asset selection, …) is checked for out-of-bounds / use-after-free /
UB, not just for correct output. New unit tests get sanitizer coverage for free.

```sh
ASAN=1 ./tests/unit/run_tests.sh
```

**(b) A targeted fixed-buffer / index harness** (`memory_safety_fuzz.cpp` +
`run.sh`) aimed at the code behind the two shipped crashes — the fixed buffers and
index math a heap/stack overflow would live in, driven over adversarial input:

```sh
./tests/asan/run.sh            # build + run
CXX=clang++ ./tests/asan/run.sh
```

It exercises the **portable** memory surface that compiles without the Win32 header
graph:

- **`RaceEntryData`'s fixed buffers** (`formattedRaceNum[8]`, `truncatedName[4]`,
  `name[100]`) over hostile rider names (empty, over-length, multi-byte UTF-8,
  emoji) and race numbers (`0`, `999`, `INT_MAX`, `INT_MIN`, negatives).
- **The leader-timing position-index clamp** — the clamp from
  `plugin_data_standings.cpp` that bounds `(int)(trackPos * NUM_TIMING_POINTS)` to
  `[0, NUM_TIMING_POINTS-1]` (`NUM_TIMING_POINTS = 100`), fed
  NaN/Inf/huge/negative/denormal floats and 500 K random bit patterns, with the
  clamped result used to index a **real `std::array<LeaderTimingPoint, NUM_TIMING_POINTS>`**.
  If the clamp ever fails to bound the index, ASan faults on the out-of-bounds write.
- **Churn of the two crash-site container types** (`map<int, array<…,100>>` and
  `map<string,double>`): build / mutate / prune / clear, mirroring the erase
  pattern in `updateRealTimeGaps`.

The compat shim (`msvc_compat.h`, `-include`d) supplies `strncpy_s`/`_TRUNCATE`;
`-D'__declspec(x)='` neutralizes the game header's dllexport. Both are test-only —
no shipped TU is affected.

**Coverage boundary:** Layer 1 cannot drive the plugin's *own* map-mutation code
(`updateRealTimeGaps`, `StatsManager::save`) natively — those translation units
pull in the whole HUD / HttpServer / cpp-httplib / Steam graph, which doesn't
build off-Windows. That live-callback path is Layer 2.

---

## Layer 2 — MSVC ASan on the real DLL (faithful; Windows)

This runs the **actual shipping binary** under ASan, driven through the **real
callbacks** by the existing boundary fuzzer — so it covers the live `PluginData` /
`StatsManager` map lifecycle that Layer 1 can't. MSVC's ASan is production-grade;
this is the definitive test.

**In CI:** the `memory-safety-msvc` job in `.github/workflows/tests.yml` does exactly
this on a `windows-latest` runner — it builds the `MXB-Debug` DLL with
`/fsanitize=address` and runs the boundary fuzzer against it. It runs **automatically
in the free public mirror** and is **opt-in in the metered private repo** (tick
"Also run the Windows MSVC AddressSanitizer job" on Run workflow); see the workflow
header for the full policy. `MXB-Debug` is exempt from the Release analytics-key
requirement, so it needs no secrets. To reproduce locally on Windows,
`run_asan_msvc.ps1` automates the same flow; the steps are:

1. **Build the plugin DLL with ASan** — `/fsanitize=address` via the `EnableASAN`
   MSBuild property, `MXB-Debug|x64` (the config that maps the plugin project; there
   is no plain `Debug` mapping). `BasicRuntimeChecks=Default` / `LinkIncremental=false`
   make the Debug config ASan-compatible:
   ```powershell
   msbuild mxbmrp3.sln /p:Configuration=MXB-Debug /p:Platform=x64 `
     /p:EnableASAN=true /p:BasicRuntimeChecks=Default /p:LinkIncremental=false
   ```
   Output: `build\MXB-Debug\mxbmrp3.dlo`. `EnableASAN=true` also flips the DLL's CRT
   from static (`/MTd`) to dynamic debug (`/MDd`) via `mxbmrp3/Directory.Build.targets`
   — see the runtime note below.
2. **Build the boundary fuzzer with ASan** (it `LoadLibrary`s the DLL). Match its CRT
   to the DLL's (`/MDd`) so both share the one ASan runtime — see the runtime note below:
   ```powershell
   cl /std:c++17 /MDd /Zi /fsanitize=address tests\integration\callback_fuzzer.cpp /Fe:callback_fuzzer.exe
   ```
3. **Run**, pointing the fuzzer at the ASan DLL, with millions of iterations
   (arg 3 is a native savepath, replacing the fuzzer's Wine-only `Z:\` default):
   ```powershell
   $env:ASAN_OPTIONS = "abort_on_error=1:halt_on_error=1"
   .\callback_fuzzer.exe build\MXB-Debug\mxbmrp3.dlo 5000000 $env:TEMP\mxbfuzz\
   ```
4. **Replay real sessions** (optional, higher fidelity — *not* run by the CI job or
   the script by default): record a session with the in-plugin recorder
   (`[Recorder] enabled=1`), then replay the `.tape` through the ASan DLL. See
   `TESTING.md` → tapes.

A clean run over the fuzzer (and, optionally, real tapes) is strong, self-standing
evidence the plugin does not corrupt memory. A hit prints the writing + allocating
stacks — the culprit line, deterministically.

> **ASan-runtime note:** the `MXB-Debug` config links the CRT statically (`/MTd`),
> whose ASan runtime is **per-module**. When an EXE `LoadLibrary`s an instrumented
> DLL, that per-module static runtime doesn't resolve across the boundary on recent
> MSVC toolsets (VS 2022 17.14+ / the VS "18" toolset `windows-latest` now ships) —
> the load fails with **error 127 (`ERROR_PROC_NOT_FOUND`)**. The fix is to build
> **both** modules with the *dynamic* debug runtime (`/MDd`) so they share the single
> `clang_rt.asan_dynamic-x86_64.dll`. `mxbmrp3/Directory.Build.targets` does this for
> the DLL — it overrides `RuntimeLibrary` to `MultiThreadedDebugDLL` **only** when
> `EnableASAN=true` (inert for every normal build, so the shipping DLL is unchanged);
> the fuzzer is compiled `/MDd` to match. The dynamic runtime DLL lives in the
> toolset bin (on `PATH` after vcvars/msvc-dev-cmd), and the CI run step also stages
> it next to the fuzzer and puts the VC debug-CRT redist on `PATH` as
> belt-and-suspenders.

---

## Layer 3 — PageHeap, in the wild (no rebuild)

When you can't rebuild — e.g. asking an affected user to help — full **PageHeap**
via `gflags` instruments the process heap so any out-of-bounds write faults *at the
write*, and the plugin's own crash handler captures the dump:

```
gflags /p /enable mxbikes.exe /full     # enable; play until it crashes
gflags /p /disable mxbikes.exe          # revert (it's RAM-hungry while on)
```

Resolve the resulting `crashes\*.dmp` against `mxbmrp3.pdb` (or
`tools/mdmp_analyze.py`) to get the culprit stack.

---

## Limits (honest)

- ASan finds bugs **on the paths actually executed** — coverage (fuzzer breadth +
  real tapes) is what makes a clean result meaningful. Strong evidence, not a proof
  for all possible inputs.
- It catches **spatial** (out-of-bounds) and **temporal** (use-after-free,
  double-free) errors on heap and stack. It does **not** catch pure **data races** —
  that's ThreadSanitizer. Given the plugin's containers are designed game-thread-
  only, ASan is the right first tool; TSan is a possible follow-up.
- ~2× slower / ~3× RAM while instrumented. Test-only; never shipped.
