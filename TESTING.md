# Testing MXBMRP3

The shipping plugin is a Windows-only MSVC DLL, but its **logic** is portable and
tested on Linux with no game and no Windows. Everything here runs in CI
(`.github/workflows/tests.yml` — on demand via **Run workflow**, as the release
workflow's gate, and automatically on pull requests in the free public mirror; see
the workflow header for why there is no push trigger) and locally with a C++17
compiler + (for the integration layer) mingw-w64 and Wine.

There are six layers, fastest first. Reach for the cheapest one that can
exercise your change (the table in `CLAUDE.md` → *Testing Discipline* maps a
change to its layer). Layers 1–5 are asserted and CI-gated; Layer 6 (visual) is
an instrument you point at a change, not a gate.

**To run everything at once, use CTest** — every gate below plus the invariant
lints is registered in `CMakeLists.txt` (which builds nothing; it exists only to
register tests):

```bash
cmake -S . -B build/tests                             # once
ctest --test-dir build/tests --output-on-failure      # everything
ctest --test-dir build/tests -L fast                  # no mingw/wine needed
ctest --test-dir build/tests -j 4 -R settings         # parallel, by name
```

A gate whose toolchain is absent exits 3 and CTest reports it **SKIPPED** rather
than failed — `SKIP_RETURN_CODE`, CTest's own convention — so the suite is useful
on a box with nothing but `g++`. Prefer this over hand-chaining the per-layer
scripts: the chain is long enough to exceed an automated runner's command
timeout, which is how a full verification ends up stranded half-done.

**A SKIP is not a pass, and it is not a result.** It means that check did not
run, so whatever it guards is unverified. On a dev box or in an agent session,
the expected response is to INSTALL the tool and get a real answer, not to
report the suite green with skips in it:

```bash
./tools/install_deps.sh --list        # groups -> what each provides
./tools/install_deps.sh cppcheck      # then the group you need
```

The skip semantics exist so a contributor with only `g++` can still run the fast
layer — not as a way to opt out of a gate that is inconvenient to provision. The
SessionStart hook (`.claude/hooks/session-start.sh`) installs every group up
front and prints a `MISSING tools:` line naming anything it could not, so an
absent tool is visible once at startup instead of only as a SKIPPED line
scrolling past mid-run.

| Layer | Framework | Needs | Runtime | Runner |
|---|---|---|---|---|
| **Unit** — pure header logic | doctest | just `g++` (+ CMake) | ~1s run (~20s cold compile) | `ctest -R '^unit'` |
| **Integration** — real plugin, driven headless | doctest + Wine | mingw-w64, wine64 | ~2 min warm (ccache); ~5-8 min cold (full cross-build + one Wine binary per `tests/*.cpp`) | `tests/integration/run_tests.sh` |
| **Specialized** — persistence / fuzz / perf / installer | bespoke | mingw-w64, wine64, python3 | ~1–3min | `tests/integration/run_*.sh` |
| **Web overlay** — rendered DOM in a real browser | Playwright | Node.js | ~40s | `tests/web/run.sh` (see the browser caveat below) |
| **Memory safety** — ASan/UBSan over the portable memory surface | doctest + a targeted harness | g++/clang, libasan | ~seconds | `ctest -R unit-asan` + `tests/asan/run.sh` |

Alongside the test layers, CI also runs **cppcheck** static analysis
(`.github/workflows/tests.yml`, over `mxbmrp3/` with vendored code excluded). It is
**blocking**: the committed baseline is at zero findings, so any new one FAILS the
build. It was report-only until that baseline was actually driven to zero —
findings nobody has to act on are findings nobody reads, and two error-severity
ones had accumulated unnoticed in job summaries (a `danglingLifetime` in
`PluginThread::flush()` that turned out to mark a real hang, and an out-of-bounds
copy count in `RecordsHud`). The accepted cost is that cppcheck versions drift
between runner images, so a toolchain bump can surface a finding unrelated to
your diff. To land a legitimate one: fix it, add an inline
`// cppcheck-suppress <id>` with a reason, or — last resort — a documented entry
in `.cppcheck-suppressions` (which holds only project-wide intentional patterns).
To run it locally: **`./tests/run_cppcheck.sh`** — the same script CI invokes, so
the flags can't drift from what you reproduce (`--report` prints findings without
failing).

CI also runs the **enforced invariant checks**, which likewise FAIL the build: `tests/integration/check_game_configs.sh` (GPB/KRP syntax),
`check_visibility_gates.sh` (HUD `isVisibleAnySurface()` gates),
`check_api_guards.sh` (DLL-export exception barriers), `check_thread_safety.sh`
(clang `-Wthread-safety` over annotated mutexes — see `core/thread_safety.h`),
`check_mt_flags.sh` (the other half of the threading invariant: a plain `bool`
member in a class owning a `std::thread` must be `std::atomic`,
`MXB_GUARDED_BY`, or carry an `mt-plain:` reason — `-Wthread-safety` only sees
*annotated* members), `check_style.sh` (file
hygiene: tabs / trailing whitespace / CRLF / final newline, mirroring
`.editorconfig`), `check_session_hook.sh` (the SessionStart hook's own
behaviour — eight configurations asserting rc, complaint count, ready state and
whether it provisioned; a broken hook leaves a box unprovisioned, and an
unprovisioned box reports SKIPPED, which reads like a passing suite),
`tools/check_vendored_manifest.py` (vendored.json vs the
vendored sources), and `tools/check_docs.py` — which now validates path references in
**first-party source comments** as well as in the .md files, closing an asymmetry
where a dangling path failed the build inside a doc but rotted silently inside a
header (two already had).
Each script's header documents the invariant and its escape-hatch annotation;
CLAUDE.md → *Maintenance Invariants* maps rule → enforcement.

Two more gates round out the analysis side: **`python-lint`** (`ruff check .`
over the repo's Python — the dev tools and doc checkers) and
**`shim-constants`** (`tests/unit/shim/regen_constants.sh --check`, which
re-derives the shim's copied API constants and fails if they drifted from the
vendored headers, so a hand-edited constant can't silently disagree with the
game's).

### CodeQL (deep static analysis, opt-in)

**`./tests/integration/run_codeql.sh`** runs GitHub's own CodeQL security
queries over the C++ tree — the `cpp-code-scanning` suite, the same one the
codeql-action evaluates. It is analysis, not a test layer: like cppcheck it reads
the code rather than running it, which is why it sits here and not as a seventh
layer above.

It exists because **`codeql.yml` can only run on the public mirror** (code
scanning needs Advanced Security, which the private repo doesn't have) and the
mirror only receives code at release time — so without a local run, the first
CodeQL scan of any change is its release. That is literally how v1.28.0 shipped
and then collected three alerts.

It is the **only opt-in gate**: a bare `ctest` skips it, even where the CodeQL
bundle is installed. Labels select rather than exclude, so `slow` alone wouldn't
have kept a 15-minute scan out of the default run once anyone had installed the
tool once — and a suite that costs a quarter of an hour after a one-line edit is
a suite people stop running.

```bash
./tools/install_deps.sh codeql                                  # ~1 GB bundle, once
MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql          # via CTest
./tests/integration/run_codeql.sh                                # or directly
./tests/integration/run_codeql.sh --keep-db                      # reuse the database
```

Budget ~10–15 min: it rebuilds the plugin clean under the CodeQL extractor, then
evaluates the queries. Reach for it **before a release**, or after touching a
parser, a trust boundary, or a dependency — not in an edit-compile-test loop.

Two guards protect the result from being falsely green, and both exist because
the failure happened:

- **Empty database.** An incremental build compiles nothing the extractor can
  observe, producing a database that analyzes to zero findings. `CCACHE_DISABLE=1`
  plus `-B` prevents it; a missing `db-cpp/` fails loudly.
- **Partial database.** The subtler one: a run that extracted **42 of 444 files**
  reported `no findings` and exited 0, indistinguishable from a real pass — the
  code the finding lived in simply wasn't in the database. The script now reads
  CodeQL's own scanned-file count and fails below a floor (a healthy run scans
  ~307). Raise the floor as the tree grows; never lower it to make a run pass.

Accepted findings live in `tests/integration/codeql_baseline.txt` — printed as
`KNOWN` and excused, everything else fails. Entries take a source-path substring
as well as a sink, because results are keyed on the *sink*, and a sink like
`logger.cpp` is shared by every log line in the plugin. The file is currently
empty by design: prefer deleting the flow to accepting it.

The script pins **`--enable=warning`** for a reason. The `.cppcheck-suppressions`
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
   Only the two http tests (`http_test.cpp`, `http_robust_test.cpp`) exercise the
   serving path itself. When a
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
ctest --test-dir build/tests -R '^unit'                  # build + run all three flavours
cmake --build build/tests --target unit_tests \
  && ./build/tests/tests/unit/unit_tests -tc='*hex*'     # doctest filter
```

Add a case to `tests/unit/test_plugin_utils.cpp` (or a new `tests/unit/test_*.cpp`, then
list it in `tests/unit/CMakeLists.txt`). A function belongs here iff it depends on
nothing but the C++ standard library — anything reaching into `PluginData` or the
game API is an integration test instead. The authoritative TU list is
`MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt` (regenerate this census with
`ls tests/unit/test_*.cpp`); that list also compiles the production
`mxbmrp3/core/ui_config.cpp` under test. What each pins:

- `test_plugin_utils.cpp` — color/time/hex helpers in `core/plugin_utils.h` (also owns the doctest impl + `main`)
- `test_notice_priority.cpp` — `hud/notice_priority.h`, the masked-notice display-timer decision
- `test_analytics_remote_config.cpp` — the remote sampling cost lever (`parseFullSample`/`shouldSendFull`): fails open to full, deterministic 0.0/1.0 endpoints
- `test_analytics_endpoint.cpp` — App-Key → Aptabase ingest-region routing (unknown/self-hosted → "" = no send)
- `test_director_airtime.cpp` — the director's two airtime helpers: the lull round-robin (`pickNextAirtimeNum`), whose cursor keys on race number rather than grid position, and the dead-air floor (`pickBaselineSubject`), which hands the camera to the broadcaster's own rider once forced rotation is off — and degrades to the leader when that rider is gone, so "Max shot = Off" can never mean dead air
- `test_session_charts_math.cpp` — the race-progression chart derivations in `hud/session_charts_math.h`
- `test_tooltip_length.cpp` — every settings tooltip fits the 2-line/~120-char render limit (compiles the real tooltip table)
- `test_update_asset_select.cpp` — the updater's release-asset picker (the symbols-zip-matched-first regression)
- `test_ui_config.cpp` — INI-only grid-overlay defaults + the `majorEvery` clamp
- `test_render_frame_buffer.cpp` — the plugin-worker-thread triple buffer: the producer never writes the displayed slot; `acquire()` returns the latest published frame
- `test_crash_stack_format.cpp` — the crash handler's backtrace string formatting + the whole-frame `MAX_STACK_CHARS` budget
- `test_hud_sw_renderer.cpp` — golden-frame sampling of the companion window's software renderer (`core/hud_sw_renderer.cpp` compiled natively): quad fill, per-quad alpha, the texel×color modulate (white-icon tinting), `.fnt` text against a real shipped font, and the scale-viewport mapping
- `test_fmx_scoring.cpp` — FMX trick scoring (`core/fmx_manager` math): rotation scale floors at 1×, air/ground tricks scale with duration (floored) and distance
- `test_segment_cumulative.cpp` — cumulative custom-segment timing: a contiguous run aggregates like the official splits; on-sector identity; isolated-arc fallback
- `test_blue_flag_detect.cpp` — the blue-flag/lapping proximity core (`core/blue_flag_detect.h`): start/finish wraparound, directionality, the deliberately asymmetric backmarker-vs-lapper eligibility, stale-sample rejection, the first-lapper-wins ordering the director depends on, and output clearing (the containers are reused every rebuild). The end-to-end wiring stays pinned by `blueflag_test.cpp`
- `test_settings_serde.cpp` — the 25 enum<->string converter pairs in `core/settings_serde.h`, the entire on-disk representation of every enum setting. They are twin hand-written switch statements, and only their agreement makes a setting survive a restart: a typo on one side saves fine and silently reads back as the default, which no compiler catches because both halves are individually well-formed. Asserts round-trip for every value of every enum, that unknown text returns the CALLER's default (checked against two different defaults, so a hardcoded fallback can't pass), that matching is exact — no trimming or case folding, which is what a hand-edited INI hits — and that every `toString` default arm emits text the load side accepts. Compiles the real header natively via `tests/unit/shim/`. Mutation-tested: a typo'd save string, a swapped pair, and a converter ignoring its default parameter are each caught
- `test_director_scoring.cpp` — the auto-director's story-score formulas (`core/director_scoring.h`). Asserts the RANKING (overtake > battle > drop > lapper > leader baseline) rather than the magic numbers, because the ranking is what makes the director cut to the right thing and the multipliers are meant to stay tunable. Also pins `posWeight`'s monotonicity (call sites compare positions instead of weights on the strength of it), the boost caps, and the battle/lapper crossover at 60% closeness — which is a real consequence of widening the battle-gap setting
- `test_director_detect.cpp` — the overtake/drop edge detectors (`core/director_detect.h`). The two cases worth the file: a rider ahead **pitting or retiring** must not read as a pass (the detector compares relative order, not absolute positions, precisely so it can't), and a drop's position cutoff applies to where the slide **started**, not where it ended — gating on the current position would discard the front-runner-slid-to-the-back story that is the whole point
- `test_camera_resolve.cpp` — spectate camera-name matching and role->index resolution (`handlers/camera_resolve.h`). The director's whole camera choice, extracted from two file-statics plus an inline switch in `spectate_handler.cpp` so the cases that a whole-race test reaches clumsily cost ~1s here: candidate PRIORITY inside a list ("Free-Roam" must beat a lower-indexed "Orbit"), the Auto fallback resolving BY NAME rather than assuming index 0, Free-Roam's deliberate refusal to fall back at all, and the bounded walk over a blob whose `numCameras` overstates its contents. Mutation-tested: a first-match lookup, an index-0 Auto fallback, and a Free-Roam that falls back are each caught. The DLL-level wiring stays pinned by `spectate_cameras_test.cpp`
- `test_standings_context_window.cpp` — the standings pagination (`hud/standings_context_window.h`): which slice(s) of the classification the table draws. Extracted from `rebuildRenderData()` because the interesting cases are exactly the ones a Wine test reaches awkwardly — the window is clamped at BOTH ends and each clamp has to hand its lost rows to the other side, so a rider in P4 (rows lost to the pinned top block) and a rider in last place (rows lost off the end) are the two that silently render short. Beyond the hand-written cases it sweeps field size × rider position × top count × row count asserting the standing contract: a field at least as large as the table always yields exactly `rowCount` rows, always including the rider's own, never duplicated, never out of range
- `test_records_window.cpp` — the records table's context window (`hud/records_window.h`): which slice of the fetched records surrounds the player's own PB. A sibling of `test_standings_context_window.cpp` and deliberately NOT merged with it — this window centres on a PB that is *inserted between* records and may sit past the end of them entirely (slower than everything fetched), which is its own branch, and it always reserves a row for the player's own line. Extracted from `rebuildRenderData()` because reaching these cases under Wine also needs a live records fetch: the player just below the pinned top block, on the last fetched record, past the end, and a list shorter than the table. Sweeps records × rows × player position asserting the window never overlaps the pinned block, never points past the records, and is exactly full whenever enough records exist to fill it
- `test_plate_geometry.cpp` — the standings race-number plate box and the nudge that centres the number in it (`hud/plate_geometry.h`). Reported from a screenshot: the number sat 1px below the plate's top edge and 5px above its bottom. **Not a font bug**, which is the part worth keeping — a row's text is drawn with its glyph CELL top-aligned to the row origin, so the cell centre sits `(lineHeight - fontSize)/2` above the row centre; ordinary text hides that because descenders fill the space below (a row highlight looks balanced), while digits have no descender and the generator centres the cap/digit band in the cell, so a race number occupies only that band. Measured across all six shipped fonts the offset was identical to within 1.3 points, which is what ruled the fonts out. The cases pin the property rather than the constants — after the nudge the cell centre must coincide with the plate centre at any row/font ratio — plus the degenerate case where the font fills the row and a negative nudge would push the number out of the plate
- `test_lap_log_plan.cpp` — the Lap Log's row planning (`hud/lap_log_plan.h`): which rows are drawn and in what order. Extracted from `rebuildRenderData()` for one specific trap — the two reserved rows (live current lap, out-of-window best lap) are taken from the recent-lap budget in an ORDER that matters, because the "is the best lap already on screen" scan runs against the budget the current-lap row has already reduced. So turning live timing ON can push the best lap out of the window and cost a *third* slot; swapping those two steps still passes a naive "does it show 5 rows" check. Sweeps maxDisplayLaps × history size × the flag space asserting the constant-height contract (placeholders keep the box the same size as laps come in) and that no lap row points past the history. The `maxDisplayLaps == 1` overflow is pinned as the pre-existing behaviour it is, not silently changed
- `test_radar_fade.cpp` — the radar's auto-hide fade (`hud/radar_fade.h`): how visible the radar is given who is nearby. Pins the wrap fold at the start/finish line — two riders at trackPos 0.99 and 0.01 are two hundredths apart, not ninety-eight, and without the fold the radar blinks off exactly as a rider crosses the line — and the fact that riders are gated TWICE on different distances (straight-line, then along the racing line), so a rider standing metres away across a barrier but half a lap back does not light it up. Also covers the `trackLength == 0` fallback that applies before session data arrives, and sweeps track position × distance × track length asserting the result never leaves [0,1] (it multiplies the configured opacity)
- `test_peak_marker.cpp` — the shared "max marker" state machine (`hud/peak_marker.h`), which replaced six character-for-character copies across LeanWidget (lean and steer, both sides), BarsWidget and RumbleHud. Pins the branch that reads like a bug and isn't: climbing to a new peak HIDES the marker, because until the reading retreats the marker would sit under the live needle — a "fix" here makes every gauge show a permanently redundant marker. Also the threshold as a deadband on *both* sides (a single trip point flickers at the top of an arc), the linger countdown clearing the held peak on expiry, and why `snapOnImpact` gates on the linger rather than the value: a still-visible marker holds an earlier, higher peak that the impact value must not overwrite, while a *held-but-hidden* peak from a steady input is exactly the case it exists for
- `test_standings_gap_plan.cpp` — the standings gap-column decision (`hud/standings_gap_plan.h`): which value each cell shows, in which style, with which tint. The ~170-line nested if/else that used to sit in `rebuildRenderData()`, now returning a plan the caller formats. Pins the rules a plausible edit breaks silently: a LAPPED or FINISHED rider must fall back to the official gap (a live time difference between riders on different laps reads as seconds when the truth is "+1 lap"); a rider outside the game's ~10-closest track-position batch has a stale `realTimeGap` and must not go live; the leader is exempt from that FRESHNESS requirement but still needs a strictly positive live gap like anyone else (the half that is easy to over-generalise); and a stale PLAYER reference must fall the whole column back rather than skew every row by a zero offset. Plus scope/tint filtering and a totality sweep over the flag space. The end-to-end strings stay pinned by `race_test.cpp` and `livegaps_test.cpp`
- `test_thread_detach_grace.cpp` — the spin-then-detach teardown POLICY (`core/thread_detach_grace.h`), shared by the three singletons that own a worker thread. The fault it guards is a scheduler race and cannot be asserted headlessly (the 150ms is a measurement, 5/24 → 0/24 in `teardown_test` under saturation); what IS a decision, and is pinned here in milliseconds, is that the grace is paid only when the finished flag actually landed, that the spin is bounded so a flag that never lands cannot hang process exit, that the thread is detached and never joined, and that an already-joined thread costs nothing
- `test_text_wrap.cpp` — the settings tooltip box's greedy word wrap (`hud/settings/text_wrap.h`), extracted from a lambda in `rebuildRenderData()` that wrapped and emitted in one loop. Pins what that loop could not state: a word longer than the line is hard-broken, the break-space is consumed rather than re-emitted, the inclusive `rfind` boundary that lets a line use its full width, and — the one that matters for the guard below — that TRUNCATION and an ELLIPSIS are different events, since a line with 3 or fewer characters to give up drops text with no "..." at all. Also sweeps widths 1..30 asserting no line ever exceeds the requested width, and that degenerate widths terminate instead of looping
- `test_cpp_js_parity.cpp` — the C++ side of the cross-renderer mirror vectors (`tests/fixtures/cpp_js_parity.json`): `PluginUtils::isColorDark` and `session_charts_math.h` `formatSecs` against the SAME golden file `tests/web/tests/parity.spec.js` asserts on the overlay JS — a one-sided edit fails one of the two suites

Exactly one TU defines the doctest impl + `main`
(`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`); every other TU just `#include "doctest.h"`
with no config macro, or the impl is defined twice and the link fails.
The `unit_tests_asan` target rebuilds the same suite under AddressSanitizer +
UBSan — see *Layer 5 — Memory safety* below; a new unit TU gets that coverage
automatically (`MXB_UNIT_SOURCES` is shared by all three targets, no second list).

### Line coverage (this layer only)

`./tests/unit/coverage.sh [floor]` builds the `unit_tests_cov` target with gcov
instrumentation, runs it, and prints a per-file report via
[gcovr](https://gcovr.com/). CI runs it with a floor of `95`, a **ratchet**:
raise the floor when coverage improves, never lower it to turn a red build green.

The number is deliberately **scoped to this layer** — the production code
actually linked into the unit binary — and must not be quoted as a project-wide
figure. Most of the plugin is only reachable across the PiBoSo DLL boundary,
where a line percentage would be expensive to obtain and misleading anyway (a
large share of those lines are render calls with no headless observable). The
honest coverage artifact for that surface is
[`tests/integration/API_COVERAGE.md`](tests/integration/API_COVERAGE.md), which
tracks every export by hand and marks the gaps ⚪/🟠 rather than hiding them in
an average.

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
consumer with its own layer. `http_test.cpp` owns the serving path: it
starts the real server, fetches over a socket, and checks it serves exactly what
`snapshot()` builds (`http_robust_test.cpp` covers that path's survival against
hostile clients). Internal state that never reaches the snapshot (e.g. the
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
The runner auto-discovers every `tests/integration/tests/*.cpp`, and this table is
a full census of them — `tools/check_docs.py` (CI) fails if a test file exists
without a row here, so a new test can't land invisibly:

| Test | What it pins |
|---|---|
| `smoke_test.cpp` | lifecycle survives: Startup → DrawInit → Draw → Shutdown |
| `race_test.cpp` | standings order (from the classification array, not insertion), gaps (`Leader`/`+1.500`), best-lap formatting, an overtake re-derive, a DSQ (state + event log) |
| `sessions_test.cpp` | across practice→race→race2: race vs non-race gap semantics, a penalty, a lapped rider, the **reused-race-number stale-state** trap, and the #240 spurious-lead guard |
| `racenum_reuse_test.cpp` | **raceNum reuse inherits NOTHING** (the `PerRider<>` registry): rich per-rider state on every registered surface (gap, lap series, live-gap active bit, real-time gap, split/S-F references) → `RaceRemoveEntry` → re-add the same number → every surface reads fresh |
| `lap_test.cpp` | per-rider last lap + the **fastest-lap chip/event**: appears on a session best, moves when beaten, final-lap handling |
| `sectors_test.cpp` | best-sectors board: per-sector fastest-first rider ranking derived from lap splits (non-race) |
| `ideal_lap_test.cpp` | `idealLapMs` = the sum of a rider's **best sectors** across laps (faster than any real lap) |
| `posdelta_split_test.cpp` | `posDeltaSplit` from `RaceSplit`: positions a rider gained/lost since the last split |
| `trackpos_test.cpp` | **real-time leader gap** from `RaceTrackPosition`, read via the `MXBMRP3_Test_GetRealTimeGap` white-box hook (never in `/api/state`); tracks live; a lapped rider → gap 0 |
| `trackpos_stale_test.cpp` | a rider outside the **~10-closest** track-position batch keeps a **frozen** gap, not one recomputed from a stale position (the leader-dropout corruption) |
| `telemetry_companion_test.cpp` | **the producer half of the visibility gate**: telemetry history buffers keep accumulating for a HUD visible only on the OPEN companion window, and stop when the window is closed or the HUD is hidden on both surfaces. Read via `MXBMRP3_Test_TelemetryHistoryDepth` (the buffers never reach `/api/state`). Verified to fail against the pre-fix `isVisible()` gate. The last case covers the **consumer** half — the show edge that clears stale samples — and carries a POSITIVE CONTROL (`REQUIRE(depth > 0)` right before the edge) because the first version of it passed with the bug present: the harness's own first `draw()` flips spectate↔on-track, which calls `clearTelemetryData()` and emptied the buffers for reasons unrelated to visibility |
| `benchmark_companion_test.cpp` | the same gate for the **benchmark profiler's collection switch** (`bm.active`), from a real report: enabled only on the companion window, the profiler rendered its tables and never filled them. Latching `active` from the `setVisible()` override missed `setCompanionVisible()`, which is not virtual. Visibility cannot see this — the widget draws either way — so it reads `active` via `MXBMRP3_Test_BenchmarkMetricsActive`. The last case is the one that constrains the FIX rather than the bug: **closing the companion window** with no toggle touched must stop collection, which a setter-side fix would miss. Verified to fail against the `isVisible()` gate (2 of 4 cases) |
| `spectate_click_test.cpp` | **click-to-spectate is offered on exactly the riders it can reach**: `PluginData::isRiderSpectatable()` is the one gate for Standings / Map / Event Log / Session Charts (DNS/retired/DSQ/pitted/unknown-rider all excluded, and a pit exit re-enables it), plus the Event Log end-to-end — a row is clickable only when the event names a rider AND that rider is still reachable, so a retirement's own row (and the earlier rows about the same rider) go inert; plus **auto-hide drops the click targets with the rows** — the input block runs before the auto-hide check and rebuildRenderData() doesn't run while hidden, so regions left behind kept hit-testing an invisible HUD |
| `chart_sectors_test.cpp` | **Session Charts at sector resolution** (`ELEM_SECTOR_POINTS`, off by default): three completed-lap samples become nine, the LIVE in-progress lap extends the series on each RaceSplit (before any lap completes), practice stays per-lap (off-race ranking is by best lap, which has no sector analogue), one rider's broken splits fall the WHOLE field back to per-lap rather than ranking a sector against a lap, and the snapshot carries the per-sector series (plus the `sectorCount` stride) the web overlay draws from |
| `hazard_reach_test.cpp` | a **wrong-way** hazard is scanned for further ahead (`hazardWrongWayAwarenessDistance`, 250m) than a **stationary** one (`hazardAwarenessDistance`, 100m), because an oncoming rider closes the gap at roughly double the rate and the 1.5s wrong-way confirmation eats most of the warning. Pinned as a matched pair at ONE distance — a wrong-way rider and a crashed rider both 208m ahead, only the first in reach — so it can't pass by simply raising the threshold for everyone |
| `blueflag_test.cpp` | **blue-flag detection semantics**, via the `MXBMRP3_Test_IsRiderBlueFlagged`/`IsRiderLapping`/`RiderLappingTarget` hooks: proximity threshold, the leader/lead-lap cases, the same-lap early-out, and pit exclusion. Written **test-first** against the original O(n²) implementation, so it pins behaviour across the scratch-array refactor rather than describing it |
| `livegaps_test.cpp` | the overlay live-gap data contract: per-rider `liveGapMs`/`liveGapValid` (valid for leader/active, false for dropped-out/lapped) — always emitted; the on/off is a client-side overlay setting |
| `session_format_test.cpp` | race-**format** clock: pure-laps/time/time+laps `format` string, and the **finish-before-timer** overtime state machine (`00:00` freeze → N TO GO → FINAL LAP → CHECKERED) |
| `timing_reference_test.cpp` | Timing HUD via the `MXBMRP3_Test_Timing*` hooks: progressive reference selection (S1 → S1+S2 → whole lap, tracking the lap timer's track-position sector from the first flying lap), pit-exit timer reset, INVALID shown for a cut lap but suppressed on a pit out-lap, freeze on the first flying lap after a garage start, grid-start timing from the gate drop + the green-flag grace window, and panel height a whole number of grid bands |
| `spectate_test.cpp` | the camera/spectate chip follows the spectated rider through `SpectateVehicles` |
| `spectate_cameras_test.cpp` | `SpectateCameras` **wiring**: a director camera-role request posted through the real entry point is resolved to an index and written back via `*piSelect` with the "I changed it" return; the request is consumed exactly ONCE (at ~140 calls/s a sticky request would pin the camera every frame); no re-cut when already on the wanted camera; Free-Roam absent leaves the camera alone; manual-camera (Orbit/Free/Free-Roam) detection that pauses the director, including re-resolution on a camera-LIST change so a stale flag can't survive a track change. The per-case resolution rules are unit-tested in `test_camera_resolve.cpp` |
| `vehicle_data_test.cpp` | `RaceVehicleData` — the ONLY telemetry source while spectating/in replay (RunTelemetry is player-only). Only the DISPLAY rider's frames land (others arrive at the same rate and must not overwrite the screen), `active=0` frames are ignored, lean is NEGATED into roll (opposite sign conventions; a dropped negation mirrors the bike), and on track it stands down entirely for RunTelemetry's full frame. Read via `MXBMRP3_Test_BikeTelemetry` — telemetry is not in the snapshot. Mutation-tested: dropping the negation and dropping the display-rider filter are both caught |
| `deinit_test.cpp` | `EventDeinit` / `RaceDeinit` **clear the world**: a populated 3-rider session goes empty, repopulates cleanly afterwards, and nothing survives into a DIFFERENT event that reuses a race number. Also that either callback is safe with nothing to clear (the game fires them without a matching init). The per-rider mirror of this is `racenum_reuse_test.cpp` |
| `run_split_test.cpp` | `RunSplit` is a **deliberate no-op** (RaceSplit owns all split handling): driving it creates no current-lap split state and leaves the splits RaceSplit computed untouched. Asserted through `MXBMRP3_Test_CurrentLapSplits`, not the snapshot — current-lap splits never reach `/api/state`, and a snapshot-only version of this test passed a mutation that made RunSplit start recording |
| `sessionstate_test.cpp` | `RaceSessionState` green snapshots the grid; session started/ended events |
| `benchmark_registry_test.cpp` | the benchmark profiler's **registry survives a session teardown**: `PluginData::clear()` (RaceDeinit / EventDeinit) used to wipe `BenchmarkMetrics`, and since HUDs are registered only once in `HudManager::initialize()` that left `hudCount` at 0 for the rest of the run — every report read "HUDs profiled: 0" with an empty table. Callbacks re-register lazily, which is worse than going empty: a stale cached index lands in whichever slot re-registered first, so timings appear under the **wrong callback name** |
| `director_test.cpp` | auto-director **battle detection** splits two close groups at the gap break; director advisory inert by default |
| `director_lock_test.cpp` | auto-director **rider lock (hold)** release rules: the lock survives ordinary standings churn but is released when a new session (session-generation bump) resets the field |
| `director_broadcast_test.cpp` | auto-director **broadcast measurement**: replays a real tape with an injected sim-clock (from tape timestamps) so the wall-clock shot pacing plays out, then parses the director's own cut log to report cut count/rate, shot-length spread, shot-type + camera mix, and per-rider screen time — asserting it lands in a plausible broadcast band and rotates across the field (not glued to the leader). Uses the `MXBMRP3_Test_DirectorSetNowMs` clock hook + `replayTapeTimed()`. Most cases set the full-auto 8 s/25 s pacing explicitly, since that is no longer the shipped default; one case replays the same tape under the **shipped defaults** (Max shot Off) so the out-of-box show is measured too — no `maxshot` cuts, no onboard cameras, still cutting. Tapes record no `SpectateVehicles`, so no home rider is adopted on a replay and that case is the leader-fallback *degrade* path; the return-home behaviour is `director_home_test`'s |
| `director_home_test.cpp` | **"Max shot = Off"**, the forced-rotation switch: with it off the director never cuts on a timer — it holds the broadcaster's own rider through a quiet race, still lets a story take the camera, and returns *home* when the story ends. Covers all four `forcedRotation()` gates (race variety cut, lull round-robin, non-race dip, rider-lock camera cycle), **each against its own rotation-on control** — without those controls "the camera didn't move" would pass on a director that never moves it. Also pins that the camera is on Auto/Trackside whenever it's Off — including the two states that hold a camera without cutting (a rider lock, and a shot already dipped into an onboard when the setting changed), which are corrected back to the TV shot — that a manual pick re-homes the broadcast, and that an out-of-range `maxShotSec` clamps *into* the range rather than reading as Off (a hand-edited `3` must mean "cut fast", not its inverse) |
| `director_events_test.cpp` | director **transparency events**: shot decisions and state changes reach the event log as Director-typed entries, state transitions carry the director button's state colors (cuts keep the per-type default), and they're emitted **unconditionally** — the in-game toggle and the overlay filter at *display* time (raw-data contract). A final case pins that **only one rider is logged as the race winner**: `SHOT_FINISH` covers the whole run-in to the flag, not just the win, so after the winner celebration the finish lock cuts to riders still racing and each of those was announced as the winner too. Counts the winner lines rather than checking one rider, and carries a positive control (`REQUIRE` the winner line exists) because "nobody was called the winner" passes trivially if the finish window never opened |
| `settings_layout_test.cpp` | **the settings panel's emitted click regions** — the one large render surface with no other coverage (quads/strings never reach `/api/state`, and region ORDER and TYPE are behaviour because a click is resolved by hit-testing them in order). Written against the PRE-conversion build to pin the General tab across routing its hand-rolled `< value >` blocks onto `addCycleControl`, and it did: the signature came back byte-identical. Two layers — structural (each control's tooltip row exists exactly once, survives unrelated additions) plus an exact golden of the whole sequence (catches a reordered or dropped region). Reads via `MXBMRP3_Test_SettingsRegionSignature`. Note Steam renders DISABLED under Wine, so the golden also pins the `enabled=false` path: tooltip row, no arrow regions |
| `reset_test.cpp` | **Reset All scope** (#212/#214): per-profile HUD settings revert to factory default, global sections (Rumble/Hotkeys) untouched |
| `reset_profile_test.cpp` | per-profile **operations** on the profile-diff (`[HudName:Profile]`): active-profile / per-HUD reset scope, copy-to-all, and switch-profile persistence |
| `autoswitch_test.cpp` | **auto-by-session profile switch**: with the flag armed, the active profile follows the session type (Practice/Qualify/Race); with it off, a session change no longer overrides a manual pick |
| `stats_test.cpp` | player **personal-best lap** persists to the stats JSON (faster-replaces-only) + top speed and the `finiteOrZero` +Inf write guard |
| `pb_scope_test.cpp` | the **all-time-PB notice follows the active PB scope**, not the per-bike write: under the default `PBScope::CATEGORY` the reference is the fastest lap across the whole **class**, so a first lap on a second bike in that class stores a PB for that bike (asserted on disk — storage stays per-bike) yet must NOT fire the green notice unless it beats the class best (`PersonalBestUpdate::beatsScopedBest`); a first lap in a *different* class still notifies |
| `odometer_test.cpp` | **odometer/distance accumulation**: distance integrates speed over the wall-clock gap between telemetry ticks, so the test injects the odometer clock (`MXBMRP3_Test_StatsSetNowUs`) for exact per-tick dt — accumulation is exact, the **~100m dirty-coalescing** marks dirty once then resets, a +Inf/NaN sample adds nothing (finiteOrZero), a >0.5s gap is discarded, and the total persists finite on the leave-track flush |
| `fmx_test.cpp` | **FMX trick detection + scoring** through the real RunTelemetry path under the injectable FMX clock (`Fmx::clockNow()` / `MXBMRP3_Test_FmxSetNowUs`, 10ms sim steps): a sub-debounce hop banks nothing (airborne debounce); a sustained airborne full-pitch rotation classifies as **BACKFLIP**, survives the 0.75s landing grace, and banks a non-zero score into the session when the 2s chain window expires; a crash during grace fails the trick without touching the session score (state via `MXBMRP3_Test_FmxState`) |
| `records_parse_test.cpp` | records provider (MXB-only): canned CBR / MXB-Ranked responses through the **real parse path** (`MXBMRP3_Test_RecordsParse`) — field mapping incl. seconds→ms + date truncation, malformed/truncated/empty JSON rejected without crashing (zero records), absurd values handled sanely (multi-KB names truncated, negative/wrong-typed times, >MAX_RECORDS capped); plus the **fetch worker** via the stub seam (`MXBMRP3_Test_RecordsSetFetchStub`: sleep + canned response, no network) completing through the real thread, and **shutdown mid-fetch** pinning the join contract — `HudManager::clear()` joins the fetch thread (now owned by `RecordsFetcher`, `core/records_fetcher.{h,cpp}`) *before* nulling the cached HUD pointers the worker touches (TimingHud) |
| `version_test.cpp` | update-checker version ordering (numeric, not lexicographic); plus the load-time **API handshake** — `GetModID`/`GetModDataVersion`/`GetInterfaceVersion` resolve under the exact export names the game looks up and answer what `mxb_api.cpp`'s static_asserts agreed to |
| `updater_test.cpp` | update install pipeline (backup→extract→verify→**rollback**) + **locked-file retry**: aborts intact when the target is held; a transient lock is recovered by the move retry |
| `settings_migration_test.cpp` | a version-mismatched INI (missing / `=4` / `=99` version line) keeps the user's HUD settings instead of silently wiping them |
| `settings_tab_test.cpp` | the settings menu **remembers its open tab**: the focused tab round-trips through save→load (by name, in `[Profiles] activeTab`), and an unknown/unavailable tab name is ignored (no empty tab) |
| `settings_sections_test.cpp` | every section `captureToCache()` produces is actually **serialized** to the INI (via `MXBMRP3_Test_CapturedSections`) — belt-and-suspenders guard on the per-HUD serializer registry (the old capture/apply/`hudOrder` "third hardcoded list" / FriendsHud silent-revert trap, now structurally one list) |
| `settings_idempotency_test.cpp` | **apply-path coverage (defaults)**: `save→load→save` is byte-identical (and a second round too), forcing `applyProfile` to read back every serialized enum/float/int/bitmask at its default and re-capture it — an asymmetric parse/clamp/format bug diverges the files |
| `settings_apply_values_test.cpp` | **apply-path coverage (non-defaults)**: `[Hud:Practice]` overrides carrying non-default enum/float/int values survive a load→save round-trip only if `applyProfile` applied them to the live HUD (re-captured as a sparse diff) — closes the idempotency test's default-only blind spot (`stringToX`/`validateX`/`std::stoi`) |
| `settings_defer_test.cpp` | **deferred auto-save**: `markDirty()` applies a change live but writes *nothing* to disk; `flushIfDirty()` (the leave-track flush) then writes exactly once; a flush with nothing dirty is a no-op — the "no settings write while the player is on track" contract |
| `companion_decouple_test.cpp` | **per-surface companion decoupling** on the live StandingsHud (via the `MXBMRP3_Test_Standings*` hooks): mirror-while-unconfigured → snapshot-on-first-edit (diverge) → clear-reverts-to-mirror; a diverged HUD persists its `companion*` keys through the real serializer while a configured-but-equal HUD writes **none** (upgrade-safe sparse save); per-surface render routing (game-frame suppression, companion filtering + offset, X-close fallback); and a HUD hidden in-game but shown on the companion still updates. The last case is the opposite rule — **HelmetOverlayHud never reaches the companion at all** (`BaseHud::rendersOnCompanion`), because it is a full-screen in-game effect with no position or scale to decouple. It asserts on the FRAME, not a visibility flag, since the flag was never the bug; it needs ON_TRACK (the overlay draws nothing while spectating) and the visor tint (helmet parts need a texture variant no headless run stages); and it sets the helmet visible on BOTH surfaces, because the companion instance is snapshotted from the game flag and setting only the game one leaves the frame flat for the wrong reason — which is how the first version of it passed against a mutant. A sixth case covers the opposite half of the same cluster — the Director tab's **Visible** row must edit the FOCUSED surface, driven through the real click path (hit-test → `dispatchRegion`) because the bug lived in the dispatch, and asserting both that the companion flag changed and that the game flag did not |
| `standings_layout_test.cpp` | StandingsHud's two text-placement paths — `rebuildRenderData` (full rebuild) and `rebuildLayout` (the drag fast path, which repositions existing primitives) — **agree** on where the race number sits inside its plate. The plate number is the one column needing a vertical nudge, and putting that nudge in the rebuild path alone left it centred at rest and jumping high mid-drag. A screenshot cannot catch it (both frames are individually plausible), so the case reads the number's inset as a fraction of plate height (`MXBMRP3_Test_StandingsPlateInsetY`) after a rebuild and again after a move, and requires them equal. Needs a real classification, not just `addEntry` — placeholder rows draw no plate at all, which is why the inset read is `REQUIRE`d in band before the comparison |
| `gamepad_layout_test.cpp` | gamepad widget interior stays pinned to the fontSize-sized controller frame — golden bottom/right-extent signature (guards the #256 `LineHeights::NORMAL` regression that slid the buttons off the controller face); fake controller via `MXBMRP3_Test_FakeGamepad` |
| `map_render_test.cpp` | MapHud **world-ribbon cache is transparent**: a real 2D track emits non-empty, all-finite quads in every view mode, and default-view geometry is bit-for-bit reproducible across a detail round-trip and rotate/zoom visits; the detail **20-200% dial** has real range, **adaptive** mode normalizes quad count across track lengths (fixed mode scales with length), legacy `detail=AUTO\|HIGH\|LOW` INI values migrate to scale/adaptive; a degenerate 1D track never produces a non-finite vertex |
| `xinput_thread_test.cpp` | XInput **I/O thread**: the rumble send policy (first-send, idle-silence, transition-to-zero, disabled-guard) and 8-bit quantization survive the move off-thread — asserted on the command `setVibration()` posts, with the I/O thread stopped so it can't drain the post first |
| `settings_click_test.cpp` | the settings-menu **click path**, headless: a click routed through the real `handleClick` → hit-test `m_clickRegions` → `dispatchRegion` → `applySteppedControl` seam (`MXBMRP3_Test_SettingsClickStepped`), pinning the `SteppedControl` descriptors' clamp + hold-repeat acceleration tiers |
| `stripchart_parity_test.cpp` | the four strip-chart HUDs (Telemetry, Rumble, Performance, Session Charts) stay **quad/string-identical** after their shared grid-line / axis-label / history-polyline blocks moved into the `BaseHud` strip-chart helpers |
| `rumble_effect_test.cpp` | rumble **effect math** (the values users tune): telemetry→channel mapping through the real RunTelemetry path — zero telemetry is silent, slip ramps map correctly, a suspension spike scales by the per-bike profile JSON, airborne suppresses ground effects, malformed profile JSON falls back without crashing |
| `plugin_thread_test.cpp` | the **`[Advanced] pluginThread=1` worker thread**: every game-state callback applied on a separate thread is functionally equivalent to the sync path — the same synthetic race produces the same standings (with a `pluginThreadFlush()` barrier before asserting) |
| `plugin_thread_golden_test.cpp` | threaded twin of `replay_golden_test`: the same real full-race callback capture (the committed `*.tape.gz` fixture) reconstructs the **identical** golden result with the worker on — no event dropped, reordered, or raced across the queue |
| `plugin_thread_latency_test.cpp` | the worker's whole point: a 60 ms stall injected into `produceFrame` (via `MXBMRP3_Test_SetProduceDelayMs`) is paid by the game's Draw in sync mode but **not** in threaded mode; performance metrics stay live off-thread |
| `plugin_thread_abort_test.cpp` | worker killed by an escaping exception (via `MXBMRP3_Test_PluginThreadAbortWorker`): routing falls back inline immediately, the stranded backlog is drained in order, and threaded mode latches off (no respawn loop) |
| `plugin_thread_switch_test.cpp` | **runtime legacy↔threaded switch** (the RELOAD_CONFIG path): flip the `[Advanced] pluginThread` flag and the next Draw's `reconcileEnabled()` starts/stops the worker — standings stay correct in sync, then threaded, then sync again, on one running instance |
| `plugin_thread_teardown_test.cpp` | teardown with the worker **still running** and a callback still queued: `shutdown()` joins the worker first and drains the queue inline — clean return, no hang, no use-after-free |
| `plugin_thread_flush_test.cpp` | `flush()` **terminates** when the worker never drains (via `MXBMRP3_Test_PluginThreadSwallowBatches`, which discards batches already taken off the queue — the window where a dying worker destroys an in-flight sentinel). Both waits were unbounded, so the calling thread parked forever *and* the abort self-heal that runs on it could never fire. Bounding them lets `flush()` return with the sentinel still queued, so the sentinel is heap-owned and captured by value — a later drain running it is safe rather than a use-after-free. A **watchdog test**: unfixed, it hangs to the per-test timeout instead of failing an assertion |
| `analytics_wiring_test.cpp` | analytics **event wiring** via the dry-run capture seam (no network): app_started is the always-sent tier (anon id + feature flags + `isDebug`); a full launch enqueues session_end + custom, a minimal launch drops both, a crash bypasses the gate. Analytics is compiled into the test DLL but never auto-inits; capture mode makes the real senders no-ops |
| `http_test.cpp` | the **serving path**: the real HTTP server answers `/api/state` and it byte-matches the direct `snapshot()` |
| `http_robust_test.cpp` | slow-loris / partial / malformed clients don't wedge the server or stall the game-thread snapshot |
| `http_gating_test.cpp` | `onDataChanged`'s two change classes: frequent types (Standings) don't rebuild the snapshot while nothing is consuming, rare transitions (RaceEntries/SessionData) rebuild anyway, and frequent types still rebuild once a client is active; plus the **build-side coalescing window**: a burst of frequent changes collapses to a couple of rebuilds rather than one each (the SSE loop pushes at most once per throttleMs and discards intermediates, so a rebuild per change was whole-grid serialization on the game thread that no client could receive), the deferred change still lands once the window elapses, and a rare transition is never deferred |
| `replay_test.cpp` | the tape read/dispatch machinery: a `TapeWriter`-synthesized tape round-trips through `replayTape()` (no game needed) |
| `recorder_test.cpp` | the **in-plugin recorder** end-to-end: disabled (default) writes nothing; enabled, a known synthetic stream produces a well-formed `MXBHREC` tape (raw bytes asserted: magic, framing, per-type counts, the compound packings) that replays back to the same standings |
| `replay_golden_test.cpp` | **real-data golden master** (solo): replays a real 1-lap MXB Club capture, asserts the reconstructed result |
| `replay_golden_multi_test.cpp` | **real-data golden master** (24-rider Farm14 race): the whole pipeline at once — winner, time gaps, fastest-lap chip on a non-winner, a real penalty, a lapped rider, DSQ/DNS/retired |
| `teardown_test.cpp` | shutdown/unload **under load**: HTTP/SSE server live + the real 24-rider tape churning standings, then Shutdown → `FreeLibrary` (static destruction) is clean; plus the unload-**without**-Shutdown (auto-save backstop) path — guards the analytics-reported AV-on-teardown class (core + HTTP path only; Discord/Steam/records are compiled out of the test DLL) |

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
- `ini.h` — INI parse/diff helpers for the settings/persistence tests.
- `tape.h` — the callback-tape format (byte-identical twin of the in-plugin
  recorder) + `TapeWriter` for synthesizing tapes.
- `zipwrite.h` — in-memory zip builder (the updater test's download stand-in).
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
resulting `snapshot()` — headless, in CI, under Wine. The core users:

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

The same tapes are reused by other tests: `plugin_thread_golden_test.cpp` replays
the solo capture with the worker thread on (identical-result equivalence),
`teardown_test.cpp` replays the 24-rider capture to load the shutdown path, and
`director_broadcast_test.cpp` replays it under an injected sim-clock via
`replayTapeTimed()`.

The captured tapes live gzipped under `tests/integration/tests/fixtures/` (recorder
format, slimmed to the state-changing events — telemetry/vehicle/draw/track-
position dropped, verified to yield the identical `/api/state` as the full
multi-megabyte captures); `tests/integration/run_tests.sh` unpacks fixtures before the run. Assert the *final*
classification + key events, not every frame — real timing is noisy.

> **Maintenance:** `harness/tape.h` must stay byte-identical to
> `mxbmrp3/core/event_recorder.{h,cpp}` (EventType values, `FileHeader`/
> `EventHeader` layout, the `RaceClassification`/`RaceTrackPosition` packings).
> **Enforced:** both files `static_assert` the same literal sizes/offsets (the
> "tape contract" blocks), so a one-sided edit fails to compile; a deliberate
> format change updates both, bumps `FileHeader::version`, and re-records.
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
| `run_perf.sh` | baseline | runs two drivers against a full 50-rider grid on a long/complex ~2400m circuit (`perf_scenario.h`, a heavier superset of the real Farm14 tape): `perf_driver` (isolated per-callback cost) + `map_perf_driver` (the interleaved MAP hot loop). Gates the gross-regression avg **and** both the general Draw p99 and the worst map-ON Draw p99 under the 480fps budget. Also runs `bench_driver` + `tools/benchmark_report.py` as a report↔analyzer contract check |
| `run_tape_bench.sh` | real-data | replays a committed `.tape.gz` (default the multiplayer Farm14 24-rider capture) through `tape_bench_driver` (PluginHost), profiles the reconstructed real field with every HUD visible, and runs `tools/benchmark_report.py` — the per-HUD **render footprint** (`default` vs `max` settings). Not gated (FPS is a tight-loop artifact); it's an inspection tool. The slim real fixtures are standings-only; pass `tests/fixtures/synthetic_positions_22riders.tape.gz` to also light up the **map/telemetry** (that fixture is synthetic — real in FORMAT, generated headless by `make_positions_tape.sh` since a real capture needs the game) |
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

> **Local green is not proof of CI green here.** `run.sh` uses whatever Chromium
> is already installed (it cannot download in a sandbox); CI installs the
> revision `@playwright/test` pins. Those differ, and a mobile-layout assertion
> once passed locally and failed in CI on the same commit. Assert a **property**
> — does not overflow, fills the width, element is hidden — never an exact pixel,
> because pixels are a property of the browser build.

No game, no plugin, no network — just Node.js. See `tests/web/README.md` for the
gotchas (rows are `translateY`-slotted over a stable DOM order, so ranking is read
by on-screen Y; tests live outside `mxbmrp3_data/web/` because that folder ships
to users). Adding a case is one `test(...)` in `tests/web/tests/overlay.spec.js`.

## Layer 5 — Memory safety (`tests/asan/`)

Answers one question: **is the plugin corrupting memory?** Two shipped crashes
were access violations in innocent heap walks — the signature of heap corruption,
where the dump's `module+offset` shows the *victim*, never the *writer*.
AddressSanitizer instead faults **at the corrupting write**, with the writing and
allocating stacks. Two native pieces (no game, no Windows, no Wine — just
g++/clang + libasan) gate CI via the `memory-safety` job in
`.github/workflows/tests.yml`:

- **The whole unit suite under ASan + UBSan** — `ctest -R unit-asan` builds the
  same TUs with `-fsanitize=address,undefined`, so every surface the unit tests
  already exercise is checked for out-of-bounds / use-after-free / UB, not just
  for correct results. A new unit test gets this coverage automatically
  (`MXB_UNIT_SOURCES` is shared — no second list).
- **A targeted harness** (`tests/asan/memory_safety_fuzz.cpp` + `tests/asan/run.sh`)
  aimed at the fixed-buffer / index surface behind the two shipped heap-corruption
  crashes: `RaceEntryData`'s fixed buffers over hostile names/numbers, the
  leader-timing `clamp((int)(trackPos*100), 0, 99)` index over NaN/Inf/huge/random
  bit patterns, and churn of the two crash-site container types.

The **faithful** pass is the separate `memory-safety-msvc` CI job: it builds the
real plugin DLL with MSVC `/fsanitize=address` (`MXBMRP3_ASAN=ON`, `Debug` config —
exempt from the Release analytics-key requirement, so no secrets) and drives it
through the real
DLL-boundary callbacks with `callback_fuzzer.cpp` on a Windows runner, covering
the live `PluginData`/`StatsManager`/HUD/HttpServer pipeline the portable layer
can't compile. It runs automatically in the free public mirror but is **opt-in**
in the metered private repo (the `asan_msvc` checkbox on Run workflow — a Windows
runner burns minutes at 2x). `tests/asan/run_asan_msvc.ps1` reproduces it locally
on Windows.

Where a memory-safety test goes: adversarial cases for a fixed buffer or index
computation belong in `memory_safety_fuzz.cpp`; anything expressible as a normal
unit test is already covered by the `ASAN=1` rerun. Note the honest limit: ASan
catches spatial (out-of-bounds) and temporal (use-after-free / double-free)
errors on the paths actually executed — it does **not** catch pure data races.
`tests/asan/README.md` has the full policy, the MSVC ASan-runtime (`/MDd`) note,
and the no-rebuild PageHeap option for in-the-wild reproduction.

## Layer 6 — Visual (`tools/mxbmrp3_hud_window/companion_demo.sh`)

Answers the question the other five can't: **does it still look right?** The
companion window renders the plugin's live quads and strings with the in-process
software renderer (`core/hud_sw_renderer`), drawing text from the game's own
pre-rasterized `.fnt` atlases — the same glyph data the game samples. So a capture
is not an approximation of the in-game HUD; it is the same primitives through the
same font metrics, and it can be **pixel-diffed**.

```bash
tools/mxbmrp3_hud_window/companion_demo.sh out.png                # default scene
tools/mxbmrp3_hud_window/companion_demo.sh out.png 25 tab Map     # a settings tab
tools/mxbmrp3_hud_window/companion_demo.sh out.png 25 tab "Lap Log"  # quote multi-word
tools/mxbmrp3_hud_window/companion_demo.sh out.png 25 gamepad     # a scene mode
tools/mxbmrp3_hud_window/companion_demo.sh --verify-deterministic 25 tab Map
SHOT_RES=2560x1440 tools/mxbmrp3_hud_window/companion_demo.sh out.png
compare -metric AE before.png after.png null:                   # 0 == no change
```

**What it is for.** A refactor that claims to preserve rendering can be *shown* to,
instead of argued to: capture the affected scenes before and after and require
`AE == 0`. That is a stronger statement than a click-region golden, which pins
emission order but not a single pixel. It is equally the way to review a change
that is *supposed* to look different — the diff is the review artifact.

**What it is not.** It is not asserted and not CI-gated: there are no committed
baselines, so it proves nothing on its own the way `ctest` does. Treat it as an
instrument (like `run_tape_bench.sh`), and keep the baseline you diff against in
the same session — a capture from a different toolchain or font revision is not a
valid baseline.

**Three ways a diff lies, all of them quiet** — the reason this section is longer
than the tool:

- **A capture rendering WITHOUT assets is the quiet failure.** Icons fall back to
  `[x]`/`[ ]` text — **72,784 px** for one scene — and the result is a plausible,
  fully-formed frame that passes the blank-frame guard easily (it is nowhere near
  uniform). Blankness is the loud failure; this one looks right. The demo now
  refuses to launch if any staged asset directory is empty. If you drive the
  window some other way, stage `plugins/mxbmrp3_data` relative to the working
  directory and check it, or you are diffing the wrong renderer.

- **Read the bounding box, not the pixel count.** A count tells you nothing about
  whether a diff is your change:

  ```bash
  compare before.png after.png -compose src diff.png
  convert diff.png -fuzz 5% -trim -format '%wx%h%O\n' info:   # where, not how much
  ```

  (The 4th version component would otherwise put a floor under every cross-branch
  diff — it is stamped from `git rev-list --count HEAD`, so a branch and its base
  always differ there. `companion_demo.sh` pins it via `MXBMRP3_VER_BUILD`, so
  captures are directly comparable. Drive the window another way and that floor
  comes back, ~185 px wherever the version shows.)

- **An async repaint can make a scene undiffable, and a fixed hold does NOT save
  you.** Something background repaints over the panel title, worth ~10,654 px.
  It is *not* a function of the hold: two runs at the SAME hold differed by that
  amount — at hold 6 on one machine, at hold 12 on another — so "always use the
  same hold" only moves which machine it bites. The demo seeds `updateMode=off`
  to remove one async source; that is an improvement, **not a fix** — the repaint
  has still been observed with it seeded. Screen a scene before trusting it:

  ```bash
  tools/mxbmrp3_hud_window/companion_demo.sh --verify-deterministic 25 tab Map
  VERIFY_N=5 tools/mxbmrp3_hud_window/companion_demo.sh --verify-deterministic 25 gear
  ```

  Treat a pass as "no divergence in N runs", never as "cannot race": at N=2 this
  passed on a scene that a later capture showed differing by 10,654 px — both
  samples had landed on the same side of the race. **So when a diff comes back
  non-zero, re-capture both sides before believing it.** A stale capture that
  caught the race is indistinguishable from a rendering regression; that is
  exactly how a 10,839 px "regression" here resolved into 185 px of version
  string plus one flaky frame.
- **Multi-word tab names must stay quoted** through to the exe
  (`... 6 tab "Lap Log"`). Unquoted, the extra word is dropped, the tab silently
  falls back to **General**, and you diff the wrong panel — reporting `AE=0` as a
  pass for a tab you never rendered.
- **A blank capture used to report success.** `import` exits 0 on an all-black
  grab, so a window that never mapped produced `==> wrote out.png` and a
  0-pixel diff against another blank. The script now requires the frame to have
  non-trivial standard deviation, retries the racy mapping, and on failure
  deletes the file and exits non-zero rather than leaving a blank for someone to
  diff against.

Needs the `screenshot` dep group (`./tools/install_deps.sh screenshot` — Xvfb +
ImageMagick) on top of the mingw/wine toolchain. `tools/mxbmrp3_hud_window/README.md`
documents the window itself, the `.fnt` layout, and the known limitations
(input still targets the game window; the `mxbmrp3_replay --window` path does not
map under Xvfb).

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
unchanged). Manual in-game testing on Windows stays the final check for input and
anything the headless build can't exercise — rendering is no longer on that list
(Layer 6 above), though the game's own GPU path is.
