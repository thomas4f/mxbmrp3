# Testing MXBMRP3

The shipping plugin is a Windows-only MSVC DLL, but its **logic** is portable and
tested on Linux with no game and no Windows. Everything here runs in CI on every
push (`.github/workflows/tests.yml`) and locally with a C++17 compiler + (for the
integration layer) mingw-w64 and Wine.

There are four layers, fastest first. Reach for the cheapest one that can
exercise your change (the table in `CLAUDE.md` → *Testing Discipline* maps a
change to its layer).

| Layer | Framework | Needs | Runtime | Runner |
|---|---|---|---|---|
| **Unit** — pure header logic | doctest | just `g++` | ~1s | `tests/unit/run_tests.sh` |
| **Integration** — real plugin, driven headless | doctest + Wine | mingw-w64, wine64 | ~30s | `tests/integration/run_tests.sh` |
| **Specialized** — persistence / fuzz / perf | bespoke | mingw-w64, wine64, python3 | ~1–3min | `tests/integration/run_*.sh` |
| **Web overlay** — rendered DOM in a real browser | Playwright | Node.js | ~5s | `tests/web/run.sh` |

Alongside the test layers, CI also runs **cppcheck** static analysis
(`.github/workflows/tests.yml`, over `mxbmrp3/` with vendored code excluded). It's
**report-only** — it annotates findings on the run and writes them to the job
summary rather than failing the build, because cppcheck versions drift between
runner images. The committed baseline is clean (intentional-pattern false
positives live in `.cppcheck-suppressions`; one-offs use inline
`// cppcheck-suppress <id>`), so any fresh annotation is a real new finding worth
a look. To reproduce locally: `cppcheck --enable=warning --platform=win64
--max-configs=1 -DGAME_MXBIKES=1 -I mxbmrp3 -i mxbmrp3/vendor
--suppressions-list=.cppcheck-suppressions --inline-suppr mxbmrp3`.

Match CI's **`--enable=warning`** when reproducing. The `.cppcheck-suppressions`
baseline is curated for that severity only — broadening to
`--enable=warning,performance,portability` surfaces extra classes CI doesn't gate
(e.g. `memsetClassFloat` on the POD `Unified::` structs, `uselessCallsSubstr`), which
look like "new findings you have to filter" but are just the wider net, not a hole in
the suppressions.

## Principles (read this before adding a test)

A handful of ideas shape the whole suite. None of them are local inventions —
each is a named, established practice, noted below so this reads as *convention
applied here*, not house style. They're worth internalising once; the per-layer
sections are just these principles applied. The tests themselves follow
**Arrange–Act–Assert**: set up the scenario, drive one callback, snapshot and
assert (see any `*_test.cpp`).

1. **Test behaviour through the real seams, not the implementation**
   (*test-through-the-public-API*; "behaviour over implementation"). The
   integration layer drives the *actual* PiBoSo callbacks into the *actual*
   compiled plugin and reads the plugin's *actual* output. It doesn't reach inside
   to poke private members or re-implement the math. This is
   **characterization / golden-master testing**: pin what the plugin *does* end to
   end, so a refactor that preserves behaviour stays green and one that breaks it
   goes red — regardless of how the internals move. A test that knows too much
   about the internals breaks on every refactor and stops being trusted.

2. **Prefer the black box; reach for the white box only when the value never
   surfaces.** Default to asserting the plugin's stable public output — the
   `/api/state` JSON snapshot (via `host.snapshot()`) — because that's a contract
   real consumers depend on, so a test against it is a test of something that
   matters. Only when a computation genuinely never reaches that output (the
   in-game-only real-time gap is the canonical case) do you open a typed
   **white-box hook** (`MXBMRP3_Test_*`) — a **seam** (Feathers), a test-only
   access point compiled out of the shipping DLL — and assert the internal value
   directly. Don't distort the product — don't add a field to the data contract
   just to make it testable — and don't leave the logic untested; add a hook. Keep
   hooks scarce: each one is a coupling to internals, so the bar is "the value
   genuinely never surfaces," not "it's easier." (See *Test-only hooks*.)

3. **Test the logic in isolation from the plumbing** (*hermetic tests*). A
   plugin-logic test must
   depend only on the plugin's *computation*, never on the HTTP server, sockets, or
   the snapshot-rebuild gating that sits in front of it in production.
   `host.snapshot()` calls `buildJsonSnapshot()` directly for exactly this reason.
   Exactly one test (`http_test.cpp`) exercises the serving path itself. When a
   test needs a workaround to satisfy machinery it isn't testing (an earlier
   version had to fire a dummy update just to defeat the rebuild gate), that's the
   signal a layer is coupled that shouldn't be — fix the seam, don't paper over it.

4. **Synthetic tests for precision, real-data golden masters for fidelity.**
   Hand-authored callback streams are deterministic and let you construct the exact
   edge case (a reused race number, a spurious lead, a DSQ) — but they're only as
   correct as *our reading* of the API. A **real captured tape** replayed
   headlessly (`replayTape()`) is the fidelity anchor: it proves the synthetic
   inputs match what the game actually sends. Keep both — they catch different
   failures. A note on golden masters, which have a deserved reputation for being
   brittle and opaque: ours assert **specific, meaning-bearing values
   cross-checked against the session log** (this rider won, this gap, this
   penalty), never a blind blob/byte diff — a *semantic* golden master, so a
   failure names what broke instead of "output changed." (See *Real-data replay*.)

5. **Keep the whole master, commit a slim fixture.** A recording captures *every*
   callback because at record time you don't know which feature you'll test next —
   and slimming is one-way (you can't recover dropped events without re-recording).
   So archive the full **master** (git-ignored, `tests/integration/tapes/`) and commit a
   small per-test **fixture** carrying only the event types that test needs
   (`slim_tape.py`, gzipped). Never slim a master in place.

6. **Push each test to the cheapest layer that can still exercise it** (the
   **test pyramid**: many fast unit tests, fewer integration, fewest browser
   e2e). A pure formatting helper is a ~1s unit test, not a 30s Wine round-trip.
   The mapping from "what you changed" to "which layer" lives in `CLAUDE.md` →
   *Testing Discipline*; the fast layers exist so there's no excuse to skip a test
   because "the real one is slow."

7. **A gap you can see is a managed risk; a gap you can't is a latent bug.**
   `API_COVERAGE.md` is a behavioral **coverage manifest** of every callback and
   its status. It's deliberately a manifest, not a line-coverage percentage: the
   cross-build is a *different* configuration from the shipping MSVC DLL, so a
   coverage number would measure the test build, not the product — and the goal is
   that untested *surface* is visible, not that every line is hit. When you find a
   gap you can't close now, write it down (there and/or as a `Known gap` note)
   rather than leaving it silent.

## Layer 1 — Unit tests (`tests/unit/`)

Pure, platform-independent functions (color math, time/score formatting, hex
parsing) compiled straight from the production header and checked with
[doctest](https://github.com/doctest/doctest). No game, no singletons, no
Windows.

```bash
./tests/unit/run_tests.sh              # build + run all
./tests/unit/run_tests.sh -tc='*hex*'  # doctest filter
```

Add a case to `tests/unit/test_plugin_utils.cpp` (or a new `tests/unit/test_*.cpp`, then
list it in `run_tests.sh`). A function belongs here iff it depends on nothing but
the C++ standard library — anything reaching into `PluginData` or the game API is
an integration test instead. Current TUs: `test_plugin_utils.cpp` (color/time/hex
helpers) and `test_notice_priority.cpp` (`hud/notice_priority.h` — the masked-
notice display-timer decision). Exactly one TU defines the doctest impl + `main`
(`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`); every other TU just `#include "doctest.h"`
with no config macro, or the impl is defined twice and the link fails.

## Layer 2 — Integration tests (`tests/integration/tests/`)

These are the heart of the suite. They **cross-compile the whole plugin to a real
Windows DLL** (mingw-w64), load it under Wine, drive the **real PiBoSo callbacks**,
and assert on the plugin's own state snapshot. This is golden-master/
characterization testing: it exercises the entire pipeline (api-export layer →
adapters → `PluginData` change detection → `buildJsonSnapshot`) and catches
*logic* regressions, not just portability breakage.

**Plugin logic is tested in isolation from the serving layer.** A logic test reads
`host.snapshot()`, which calls `buildJsonSnapshot()` **directly** (via a test hook)
— no HTTP server, no socket, no snapshot-rebuild gating. So a plugin-logic test
depends only on the plugin's computation, never on the server machinery. (An
earlier version routed everything through the live HTTP server and one test had to
fire a dummy update just to defeat the rebuild gate — accidental coupling that's
now gone.) The JSON *contract* it reads is still the plugin's own stable public
output, so asserting it isn't coupling to the overlay — the overlay is a separate
consumer with its own layer. The single `http_test.cpp` owns the serving path: it
starts the real server, fetches over a socket, and checks it serves exactly what
`snapshot()` builds. Internal state that never reaches the snapshot (e.g. the
real-time gap) is read through its own typed hook — see *Test-only hooks* below.

```bash
./tests/integration/run_tests.sh                  # build DLL + run every tests/*.cpp
./tests/integration/run_tests.sh race sessions    # subset by basename
TEST_DEBUG=1 ./tests/integration/run_tests.sh race  # dump the driver trace on failure
MXBMRP3_TEST_TIMEOUT=30 ./tests/integration/run_tests.sh  # tighten the per-test abort cap
```

**Per-test timeout.** Each test binary runs under a wall-clock cap (default **120s**,
printed as `(cap Ns)`) so a *hung* test — a deadlock or infinite loop — is aborted with a
clear `TIMED OUT` line instead of silently burning CI minutes until the job-level cap. A
healthy test finishes in ~1–10s (the runner prints each one's elapsed time), so the default
is pure headroom. Override with `MXBMRP3_TEST_TIMEOUT=<seconds>` to tighten it locally or
loosen it for a genuinely slower run. The specialized runners have the same knob with their
own defaults: `MXBMRP3_PERF_TIMEOUT` (180s), `MXBMRP3_PERSIST_TIMEOUT` (60s),
`MXBMRP3_FUZZ_TIMEOUT` (60s/case), `MXBMRP3_CALLBACK_FUZZ_TIMEOUT` (300s).

Each `tests/*.cpp` is a self-contained doctest binary with its own plugin
lifecycle and HTTP port, run in an isolated Wine process with a clean save dir.
The current suite:

| Test | What it pins |
|---|---|
| `smoke_test.cpp` | lifecycle survives: Startup → DrawInit → Draw → Shutdown |
| `race_test.cpp` | standings order (from the classification array, not insertion), gaps (`Leader`/`+1.500`), best-lap formatting, an overtake re-derive, a DSQ (state + event log) |
| `sessions_test.cpp` | across practice→race→race2: race vs non-race gap semantics, a penalty, a lapped rider, the **reused-race-number stale-state** trap, and the #240 spurious-lead guard |
| `lap_test.cpp` | per-rider last lap + the **fastest-lap chip/event**: appears on a session best, moves when beaten, final-lap handling |
| `sectors_test.cpp` | best-sectors board: per-sector fastest-first rider ranking derived from lap splits (non-race) |
| `ideal_lap_test.cpp` | `idealLapMs` = the sum of a rider's **best sectors** across laps (faster than any real lap) |
| `posdelta_split_test.cpp` | `posDeltaSplit` from `RaceSplit`: positions a rider gained/lost since the last split |
| `trackpos_test.cpp` | **real-time leader gap** from `RaceTrackPosition`, read via the `MXBMRP3_Test_GetRealTimeGap` white-box hook (never in `/api/state`); tracks live; a lapped rider → gap 0 |
| `trackpos_stale_test.cpp` | a rider outside the **~10-closest** track-position batch keeps a **frozen** gap, not one recomputed from a stale position (the leader-dropout corruption) |
| `livegaps_test.cpp` | the overlay live-gap data contract: per-rider `liveGapMs`/`liveGapValid` (valid for leader/active, false for dropped-out/lapped) — always emitted; the on/off is a client-side overlay setting |
| `session_format_test.cpp` | race-**format** clock: pure-laps/time/time+laps `format` string, and the **finish-before-timer** overtime state machine (`00:00` freeze → N TO GO → FINAL LAP → CHECKERED) |
| `spectate_test.cpp` | the camera/spectate chip follows the spectated rider through `SpectateVehicles` |
| `sessionstate_test.cpp` | `RaceSessionState` green snapshots the grid; session started/ended events |
| `director_test.cpp` | auto-director **battle detection** splits two close groups at the gap break; director advisory inert by default |
| `director_lock_test.cpp` | auto-director **rider lock (hold)** release rules: the lock survives ordinary standings churn but is released when a new session (session-generation bump) resets the field |
| `director_broadcast_test.cpp` | auto-director **broadcast measurement**: replays a real tape with an injected sim-clock (from tape timestamps) so the wall-clock shot pacing plays out, then parses the director's own cut log to report cut count/rate, shot-length spread, shot-type + camera mix, and per-rider screen time — asserting it lands in a plausible broadcast band and rotates across the field (not glued to the leader). Uses the `MXBMRP3_Test_DirectorSetNowMs` clock hook + `replayTapeTimed()` |
| `reset_test.cpp` | **Reset All scope** (#212/#214): per-profile HUD settings revert to factory default, global sections (Rumble/Hotkeys) untouched |
| `reset_profile_test.cpp` | per-profile **operations** on the profile-diff (`[HudName:Profile]`): active-profile / per-HUD reset scope, copy-to-all, and switch-profile persistence |
| `autoswitch_test.cpp` | **auto-by-session profile switch**: with the flag armed, the active profile follows the session type (Practice/Qualify/Race); with it off, a session change no longer overrides a manual pick |
| `stats_test.cpp` | player **personal-best lap** persists to the stats JSON (faster-replaces-only) + top speed and the `finiteOrZero` +Inf write guard |
| `version_test.cpp` | update-checker version ordering (numeric, not lexicographic) |
| `updater_test.cpp` | update install pipeline (backup→extract→verify→**rollback**) + **locked-file retry**: aborts intact when the target is held; a transient lock is recovered by the move retry |
| `settings_migration_test.cpp` | a version-mismatched INI (missing / `=4` / `=99` version line) keeps the user's HUD settings instead of silently wiping them |
| `settings_tab_test.cpp` | the settings menu **remembers its open tab**: the focused tab round-trips through save→load (by name, in `[Profiles] activeTab`), and an unknown/unavailable tab name is ignored (no empty tab) |
| `settings_sections_test.cpp` | every section `captureToCache()` produces is actually **serialized** to the INI (via `MXBMRP3_Test_CapturedSections`) — belt-and-suspenders guard on the per-HUD serializer registry (the old capture/apply/`hudOrder` "third hardcoded list" / FriendsHud silent-revert trap, now structurally one list) |
| `settings_idempotency_test.cpp` | **apply-path coverage (defaults)**: `save→load→save` is byte-identical (and a second round too), forcing `applyProfile` to read back every serialized enum/float/int/bitmask at its default and re-capture it — an asymmetric parse/clamp/format bug diverges the files |
| `settings_apply_values_test.cpp` | **apply-path coverage (non-defaults)**: `[Hud:Practice]` overrides carrying non-default enum/float/int values survive a load→save round-trip only if `applyProfile` applied them to the live HUD (re-captured as a sparse diff) — closes the idempotency test's default-only blind spot (`stringToX`/`validateX`/`std::stoi`) |
| `analytics_wiring_test.cpp` | analytics **event wiring** via the dry-run capture seam (no network): app_started is the always-sent tier (anon id + feature flags + `isDebug`); a full launch enqueues session_end + custom, a minimal launch drops both, a crash bypasses the gate. Analytics is compiled into the test DLL but never auto-inits; capture mode makes the real senders no-ops |
| `http_test.cpp` | the **serving path**: the real HTTP server answers `/api/state` and it byte-matches the direct `snapshot()` |
| `http_robust_test.cpp` | slow-loris / partial / malformed clients don't wedge the server or stall the game-thread snapshot |
| `replay_test.cpp` | the tape read/dispatch machinery: a `TapeWriter`-synthesized tape round-trips through `replayTape()` (no game needed) |
| `replay_golden_test.cpp` | **real-data golden master** (solo): replays a real 1-lap MXB Club capture, asserts the reconstructed result |
| `replay_golden_multi_test.cpp` | **real-data golden master** (24-rider Farm14 race): the whole pipeline at once — winner, time gaps, fastest-lap chip on a non-winner, a real penalty, a lapped rider, DSQ/DNS/retired |

### Writing a new integration test

The harness (`tests/integration/harness/`) makes a test read like the scenario it
describes. A minimal one:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"   // provides main() + dllPath()
#include "plugin_host.h"        // loads the DLL, drives callbacks, returns JSON
#include "assertions.h"         // checkStandings / hasEvent / riderByNum

TEST_CASE("my scenario") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\myscenario\\");  // clean per-test save dir
    REQUIRE(host.startHttp());                             // start the web server via test hook

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
    });

    auto d = host.state();          // parsed /api/state (nlohmann::json)
    REQUIRE(d.is_object());
    checkStandings(d, {
        { 1, 10, "Alice", "Leader", "1:30.000" },
        { 2, 22, "Bob",   "+1.500", "1:31.000" },
    });

    host.shutdown();
}
```

Then drop the file in `tests/integration/tests/` — the runner finds it automatically
(no list to edit) and CI picks it up. Conventions that matter:

- **One plugin lifecycle per file.** The plugin is stateful (it re-derives
  standings on each update), so drive successive phases sequentially against one
  running instance and snapshot after each — don't use `SUBCASE` for phases (it
  re-enters the case body, re-running Startup).
- **Save dir** is `Z:\tmp\mxbmrp3-tests\<name>\` (the runner wipes the tree and
  pre-creates each dir). Keep it distinct so tests don't share a `settings.ini`.
- **Add a callback** the harness doesn't expose yet by adding a driving helper to
  `PluginHost` (and its struct to `plugin_api.h`, byte-compatible with
  `vendor/piboso/mxb_api.h`). **Add a JSON field** to assert by extending
  `assertions.h`. Keep the shared shape in the harness, the scenario in the test.

The harness pieces:
- `plugin_api.h` — the `SPlugins*` structs the tests drive (mirror the real ABI).
- `plugin_host.h` — `PluginHost`: `LoadLibrary` + export resolution, the callback
  drivers, `startHttp()` (via the `MXBMRP3_Test_StartHttp` hook — no settings
  seeding needed), and `state()` returning parsed JSON.
- `assertions.h` — `checkStandings()`, `hasEvent()`, `riderByNum()`.
- `integration_main.h` — shared `main()` that takes the DLL path positionally.
- `doctest.h` — vendored single-header framework.

### Test-only hooks

Some internal actions aren't reachable through a game callback (reset-to-defaults,
copy-profile, force a save, (re)load settings from disk, compare versions). They're
exposed as `MXBMRP3_Test_*` exports in `mxbmrp3/core/test_hooks.cpp`, gated
entirely on `MXBMRP3_TEST_BUILD` — so they **don't exist in the shipping DLL**.
Add a hook there when a test needs to invoke an internal action the game API can't
trigger.

**Hooks also cover internal state that never reaches the JSON.** Test what a
computation *is*, not just what the overlay renders. The real-time gap
(`RaceTrackPosition` → `updateRealTimeGaps`) is in-game-only — read by
`StandingsHud`, not emitted in `/api/state`. Rather than force it into the data
contract (a product decision) or leave it fuzz-only, `trackpos_test.cpp` reads it
directly via `MXBMRP3_Test_GetRealTimeGap` and asserts the algorithm (a follower's
gap is how much later it reaches a point the leader stamped). White-box, in the
plugin's own units — the right seam for internal logic the black-box snapshot
can't see.

Not every integration test asserts on `/api/state` — a settings test asserts on
the re-saved `settings.ini` instead. `reset_test.cpp` is the pattern: start the
plugin, perturb a few anchor keys in the INI on disk, pull them into live state
with the `MXBMRP3_Test_LoadSettings` hook (the "set live state" seam), run the
reset, re-save, and diff the file with `harness/ini.h`. It runs in one process —
no capture-default-in-a-separate-run dance.

> **Known gap:** only Reset *All* is asserted. Per-profile and per-HUD resets clear
> the *profile diff*, not the shared base section, so perturbing a base-section
> value doesn't exercise them (the "`m_hudDefaults` is not a clean factory
> snapshot" property). Covering those cleanly needs perturbing a profile-diff
> section; tracked in `tests/integration/API_COVERAGE.md`.

### Real-data replay (callback tapes)

The integration tests drive **synthetic** callback streams — deterministic and
great for targeted/edge scenarios, but only as faithful as our reading of the
API. The fidelity anchor is a **callback tape**: a recording of the *real*
callbacks the game sends, replayed headlessly and asserted.

Producing and playing tapes:

- **In-plugin recorder** (`mxbmrp3/core/event_recorder.{h,cpp}`, MX Bikes only) —
  the main plugin records every callback to a binary `MXBHREC` file when a
  developer sets the hidden `[Recorder] enabled=1` INI key (no HUD, no hotkey);
  tapes land in `<save>/mxbmrp3/tapes/`. The only way to *capture* a real tape
  (needs the game). This replaces the old standalone `mxbmrp3_record.dlo` plugin,
  which used its own process + console window — closing that console `ExitProcess`ed
  the game without a clean `Shutdown()`, crashing the main plugin's teardown.
- **`tools/mxbmrp3_replay/`** — replays a tape into the plugin in **real time** (`--speed`),
  e.g. into a live plugin with the web server on so you can preview the overlay
  against real data in a browser. A manual dev/preview tool.

For **automated** testing, `PluginHost::replayTape()` reads that same format and
dispatches each event into the plugin's real exports, then a test asserts the
resulting `snapshot()` — headless, in CI, under Wine. Two tests use it:

- `replay_test.cpp` — a round-trip on a tape synthesized with `harness/tape.h`'s
  `TapeWriter` (proves the read/dispatch machinery without needing a game).
- `recorder_test.cpp` — a full round-trip through the **in-plugin recorder**: drive
  a live race that the recorder captures to a `.tape`, then replay that tape into a
  fresh plugin instance and assert identical standings (proves the record path, not
  just replay).
- `replay_golden_test.cpp` / `replay_golden_multi_test.cpp` — the **real-data
  golden masters**: replay actual in-game captures and assert the plugin
  reconstructs the result, every value cross-checked against the session log.
  One is a solo 1-lap finish (MXB Club); the other is a **full 24-rider race**
  (Farm14) that exercises the whole pipeline at once — winner, time gaps, the
  fastest-lap chip on a non-winner, a real Cutting penalty, a lapped rider, and
  DSQ/DNS/retired states. These are the fidelity anchors for the synthetic tests.

The captured tape lives gzipped under `tests/integration/tests/fixtures/` (recorder
format, slimmed to the state-changing events — telemetry/vehicle/draw/track-
position dropped, verified to yield the identical `/api/state` as the full 9 MB
capture); `run_tests.sh` unpacks fixtures before the run. Assert the *final*
classification + key events, not every frame — real timing is noisy.

> **Maintenance:** `harness/tape.h` must stay byte-identical to
> `mxbmrp3/core/event_recorder.{h,cpp}` (EventType values, `FileHeader`/
> `EventHeader` layout, the `RaceClassification`/`RaceTrackPosition` packings).
> A recorded tape is coupled to the `mxb_api.h` struct layout at record time —
> record fresh after an API change.

## Layer 3 — Specialized runners (`tests/integration/`)

Different modalities that don't fit the snapshot-assertion shape, each its own
script:

| Runner | Kind | Asserts |
|---|---|---|
| `run_persist_test.sh` | property | flips every boolean setting, then that all survive a save→load→save round-trip (the per-HUD registry "silently reverts on restart" write-back trap) |
| `run_fuzz.sh` | survival | a corpus of malformed `settings.ini` + the six JSON config files must never crash or abort the load |
| `run_fuzz_callbacks.sh` | survival | every DLL-boundary callback survives adversarial sizes/counts/bytes (found + guards a real `TrackCenterline` OOB read) |
| `run_perf.sh` | baseline | times the hot callbacks at a full 50-rider grid against the 240fps budget; gross-regression gate |
| `run_installer_test.sh` | outcome | builds `packaging/mxbmrp3.nsi` with makensis, drives `Setup.exe` + the uninstaller headless under Wine, asserts the install/uninstall/registry/data-wipe mechanics (see below) |

These use `loader.cpp` (a bare, assertion-free host that just loads + runs the
plugin) rather than doctest, because they measure survival/timing over many runs,
not a single asserted outcome.

### Installer mechanics (`run_installer_test.sh`)

The one runner that tests the **packaging** rather than the plugin: it compiles
`packaging/mxbmrp3.nsi` with `makensis`, then drives the produced `Setup.exe` and
its uninstaller headless under Wine and asserts the on-disk + registry outcomes
(files laid down, `mxbmrp3_data` tree, Add/Remove keys, per-game path keys, the
`/FRESH=1` and `/UDATA=1` savepath-data wipes, partial-uninstall repoint, full
key removal, and that the HKLM write-probe leaves no stray key). It needs only
`makensis` + `wine` — not the mingw cross-build.

It drives the installer's **`/ELEVATED` command-line path** (the process the
on-demand elevation relaunch spawns), because that child takes the whole game
selection on the command line and runs the *same* install/uninstall Section,
registry and data-wipe code — so the mechanics are exercised without needing to
drive nsDialogs wizard pages headlessly.

> **Known gaps (manual Windows check).** Wine has no UAC and doesn't enforce ACLs
> for a normal user, so three things the runner can't reach stay a manual pass on
> real Windows (P1 matrix, and `packaging/mxbmrp3.nsi`): the writability probe
> actually **triggering** the elevated relaunch (Wine dirs are writable, so it
> never fires); the genuine **UAC prompt** and cross-account (standard user →
> admin credentials) elevation, including that the savepath resolves to the
> *launching* user's Documents; and the per-user **HKCU** hive branch (Wine always
> permits the HKLM write, so `useMachineReg` is always 1 here — it's the same
> `WRITE_UNINSTALL_REG` macro with a different root). The interactive pages
> themselves render correctly (verified once by hand).

## Layer 4 — Web overlay (`tests/web/`)

The browser/OBS overlay (`mxbmrp3_data/web/`) is the one piece the C++ layers
can't reach: they assert the plugin's `/api/state` JSON (what the overlay
*receives*); these assert what the overlay *draws* from it (tower ordering; battle-card
**live gaps**, reached by freeing the shared bottom slot via the localStorage
CONFIG override). They drive the overlay's built-in **`?demo` mode** — a synthetic 22-rider warmup + race that
feeds the same snapshots into `render()` the live SSE stream would — in headless
Chromium via [Playwright](https://playwright.dev), and assert the rendered DOM
(tower fills, positions are contiguous `1..N` and ascend on screen, real roster
names come through, the race phase shows `Leader` on P1, no uncaught JS errors).

```bash
./tests/web/run.sh              # install deps on first use, then run
./tests/web/run.sh --headed     # watch it drive the overlay
```

No game, no plugin, no network — just Node.js. See `tests/web/README.md` for the
gotchas (rows are `translateY`-slotted over a stable DOM order, so ranking is read
by on-screen Y; tests live outside `mxbmrp3_data/web/` because that folder ships
to users). Adding a case is one `test(...)` in `tests/web/tests/overlay.spec.js`.

## Coverage

`tests/integration/API_COVERAGE.md` is the coverage manifest — every game callback and
internal action with its status (asserted / driven / survival / untested) and the
test that covers it. It's a behavioral manifest, not a line-coverage number: the
goal is that gaps are **visible**, not that every line is hit. Update it when you
add a test.

## The cross-build itself

`tests/integration/README.md` documents the mingw build engine (incremental + parallel +
ccache) and exactly how the test DLL diverges from the shipping MSVC build (all
gated by `MXBMRP3_TEST_BUILD` / `_MSC_VER`, so the shipping build is byte-for-byte
unchanged). Manual in-game testing on Windows stays the final check for rendering,
input, and anything the headless build can't exercise.
