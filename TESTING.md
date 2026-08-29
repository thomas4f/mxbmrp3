# Testing MXBMRP3

The shipping plugin is a Windows-only MSVC DLL, but its **logic** is portable and
tested on Linux with no game and no Windows. Everything here runs in CI
(`.github/workflows/tests.yml` - on demand via **Run workflow**, as the release
workflow's gate, and automatically on pull requests in the free public mirror; see
the workflow header for why there is no push trigger) and locally with a C++17
compiler + (for the integration layer) mingw-w64 and Wine.

There are six layers, fastest first. Reach for the cheapest one that can
exercise your change (the table in `CLAUDE.md` → *Testing Discipline* maps a
change to its layer). Layers 1–5 are asserted and CI-gated; Layer 6 (visual) is
an instrument you point at a change, not a gate.

**To run everything at once, use CTest** - every gate below plus the invariant
lints is registered in `CMakeLists.txt` (which builds nothing; it exists only to
register tests):

```bash
cmake -S . -B build/tests                             # once
ctest --test-dir build/tests --output-on-failure      # everything
ctest --test-dir build/tests -L fast                  # no mingw/wine needed
ctest --test-dir build/tests -j 4 -R settings         # parallel, by name
```

A gate whose toolchain is absent exits 3 and CTest reports it **SKIPPED** rather
than failed - `SKIP_RETURN_CODE`, CTest's own convention - so the suite is useful
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
layer - not as a way to opt out of a gate that is inconvenient to provision. The
SessionStart hook (`.claude/hooks/session-start.sh`) installs every group up
front and prints a `MISSING tools:` line naming anything it could not, so an
absent tool is visible once at startup instead of only as a SKIPPED line
scrolling past mid-run.

| Layer | Framework | Needs | Runtime | Runner |
|---|---|---|---|---|
| **Unit** - pure header logic | doctest | just `g++` (+ CMake) | ~1s run (~20s cold compile) | `ctest -R '^unit'` |
| **Integration** - real plugin, driven headless | doctest + Wine | mingw-w64, wine64 | ~2 min warm (ccache); ~5-8 min cold (full cross-build + one Wine binary per `tests/*.cpp`) | `tests/integration/run_tests.sh` |
| **Specialized** - persistence / fuzz / perf / installer | bespoke | mingw-w64, wine64, python3 | ~1–3min | `tests/integration/run_*.sh` |
| **Web overlay** - rendered DOM in a real browser | Playwright | Node.js | ~40s | `tests/web/run.sh` (see the browser caveat below); `tests/web/lint.sh` for the eslint gate |
| **Memory safety** - ASan/UBSan over the portable memory surface | doctest + a targeted harness | g++/clang, libasan | ~seconds | `ctest -R unit-asan` + `tests/asan/run.sh` |

Two gates sit outside the layers because they cover a *tool*, not the plugin.
Both ran in CI for months without being gates - so `ctest` was green while CI ran
more than it, and that is how 19 compiler warnings per build sat unread in a CI
log. `check_docs.py` now checks that direction too, so a CI step that isn't a
gate fails the docs check:

| Gate | What it covers |
|---|---|
| `fontgen` | `tools/fontgen/test.sh` - regenerates `RobotoMono-Regular.fnt` from the source `.ttf` and asserts it is structurally identical to the shipped font (cell height, per-glyph advances, atlas dims, inflate round-trip, mip-safe glyph gaps) |
| `themeslice` | `tools/themeslice/themeslice.py --selftest` - the theme slicer's **round trip**: cut a synthetic asymmetric master into 27 slices and require each to equal its region of the source (symmetric input cannot detect a transposed cell, which is the failure that ships two corners wrong). Also asserts the bootstrap ini never clobbers an existing one, and that the seam check fires. Stdlib python, no build, ~0.2s |
| `analytics-selftest` | `tools/analytics_report.py --selftest`. Needs pandas (`./tools/install_deps.sh analytics`); without it the gate SKIPs rather than fails |

Alongside the test layers, CI also runs **cppcheck** static analysis
(`.github/workflows/tests.yml`, over `mxbmrp3/` with vendored code excluded). It is
**blocking**: the committed baseline is at zero findings, so any new one FAILS the
build. It was report-only until that baseline was actually driven to zero -
findings nobody has to act on are findings nobody reads, and two error-severity
ones had accumulated unnoticed in job summaries (a `danglingLifetime` in
`PluginThread::flush()` that turned out to mark a real hang, and an out-of-bounds
copy count in `RecordsHud`). The accepted cost is that cppcheck versions drift
between runner images, so a toolchain bump can surface a finding unrelated to
your diff. To land a legitimate one: fix it, add an inline
`// cppcheck-suppress <id>` with a reason, or - last resort - a documented entry
in `.cppcheck-suppressions` (which holds only project-wide intentional patterns).
To run it locally: **`./tests/run_cppcheck.sh`** - the same script CI invokes, so
the flags can't drift from what you reproduce (`--report` prints findings without
failing).

CI also runs the **enforced invariant checks**, which likewise FAIL the build: `tests/integration/check_game_configs.sh` (GPB/KRP syntax),
`check_visibility_gates.sh` (HUD `isVisibleAnySurface()` gates),
`check_card_anchor_coverage.sh` (every user of the card-box accessors is
swept by `card_anchor_sweep_test` - the list is hand-maintained because each
entry needs a harness id, so the SOURCE decides who belongs),
`check_title_tier.sh` (a full HUD's caption asks for `TitleTier::Large`, and
measures its width at the same tier it draws at - the tier is opt-in and
defaults to the gauge size, so a panel that forgets wears a widget caption next
to its siblings, which no geometry assertion can see because the panel lays out
consistently at whichever size it picked),
`check_api_guards.sh` (DLL-export exception barriers), `check_thread_safety.sh`
(clang `-Wthread-safety` over annotated mutexes - see `core/thread_safety.h`),
`check_mt_flags.sh` (the other half of the threading invariant: a plain `bool`
member in a class owning a `std::thread` must be `std::atomic`,
`MXB_GUARDED_BY`, or carry an `mt-plain:` reason - `-Wthread-safety` only sees
*annotated* members), `check_move_reads.sh` (no call passes `std::move(x)` and
also reads a member off `x`: argument evaluation order is unspecified, so gcc
and MSVC disagree and only MSVC ships - this shipped `position_gained` as
"Up ." with every Linux gate green; `--self-test` is its own gate),
`check_change_consumers.sh` (every `X::onDataChanged` definition carries a
`// change-gate:` annotation saying how it stays cheap on the frequent change
types - `DataChangeType::Standings` fires many times per second on a full grid,
and the lint cannot measure cost, so it makes the author answer the cost
question out loud, with the two gating models named in CLAUDE.md; its
`--self-test` is its own gate),
`check_test_hook_placement.sh` (a code mention of `MXBMRP3_Test_*` outside
`core/test_hooks.cpp` fails - that file is the one place both gated on
`MXBMRP3_TEST_BUILD` and excluded from every shipping target, so a hook defined
anywhere else silently ships in the released DLL; `friend` declarations and
`// test-hook-exempt:` lines are the escape hatches, and its `--self-test` is
its own gate),
`check_thread_join.sh` (every first-party `std::thread` member carries a
`// joined-by:` annotation naming who joins it on the orchestrated Shutdown
chain - a destructor join at DLL detach deadlocks under the loader lock and
the game does unload without calling `Shutdown()`, the path two shipped
teardown crashes came from, so the destructor may only be a `spinThenDetach`
backstop; the lint cannot trace a join, so it makes the author name one and
leaves the claim to review; `--self-test` is its own gate),
`check_style.sh` (file
hygiene: tabs / trailing whitespace / CRLF / final newline, mirroring
`.editorconfig`), `check_session_hook.sh` (the SessionStart hook's own
behaviour - eight configurations asserting rc, complaint count, ready state and
whether it provisioned; a broken hook leaves a box unprovisioned, and an
unprovisioned box reports SKIPPED, which reads like a passing suite),
`tools/check_vendored_manifest.py` (vendored.json vs the
vendored sources), and `tools/check_docs.py` - which now validates path references in
**first-party source comments** as well as in the .md files, closing an asymmetry
where a dangling path failed the build inside a doc but rotted silently inside a
header (two already had).
Each script's header documents the invariant and its escape-hatch annotation;
CLAUDE.md → *Maintenance Invariants* maps rule → enforcement.

Two more gates round out the analysis side: **`python-lint`** (`ruff check .`
over the repo's Python - the dev tools and doc checkers) and
**`shim-constants`** (`tests/unit/shim/regen_constants.sh --check`, which
re-derives the shim's copied API constants and fails if they drifted from the
vendored headers, so a hand-edited constant can't silently disagree with the
game's).

### CodeQL (deep static analysis, opt-in)

**`./tests/integration/run_codeql.sh`** runs GitHub's own CodeQL security
queries over the C++ tree - the `cpp-code-scanning` suite, the same one the
codeql-action evaluates. It is analysis, not a test layer: like cppcheck it reads
the code rather than running it, which is why it sits here and not as a seventh
layer above.

It exists because **`codeql.yml` can only run on the public mirror** (code
scanning needs Advanced Security, which the private repo doesn't have) and the
mirror only receives code at release time - so without a local run, the first
CodeQL scan of any change is its release. That is literally how v1.28.0 shipped
and then collected three alerts.

It is the **only opt-in gate**: a bare `ctest` skips it, even where the CodeQL
bundle is installed. Labels select rather than exclude, so `slow` alone wouldn't
have kept a 15-minute scan out of the default run once anyone had installed the
tool once - and a suite that costs a quarter of an hour after a one-line edit is
a suite people stop running.

```bash
./tools/install_deps.sh codeql                                  # ~1 GB bundle, once
MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql          # via CTest
./tests/integration/run_codeql.sh                                # or directly
./tests/integration/run_codeql.sh --keep-db                      # reuse the database
```

Budget ~10–15 min: it rebuilds the plugin clean under the CodeQL extractor, then
evaluates the queries. Reach for it **before a release**, or after touching a
parser, a trust boundary, or a dependency - not in an edit-compile-test loop.

Two guards protect the result from being falsely green, and both exist because
the failure happened:

- **Empty database.** An incremental build compiles nothing the extractor can
  observe, producing a database that analyzes to zero findings. `CCACHE_DISABLE=1`
  plus `-B` prevents it; a missing `db-cpp/` fails loudly.
- **Partial database.** The subtler one: a run that extracted **42 of 444 files**
  reported `no findings` and exited 0, indistinguishable from a real pass - the
  code the finding lived in simply wasn't in the database. The script now reads
  CodeQL's own scanned-file count and fails below a floor (a healthy run scans
  ~307). Raise the floor as the tree grows; never lower it to make a run pass.

Accepted findings live in `tests/integration/codeql_baseline.txt` - printed as
`KNOWN` and excused, everything else fails. Entries take a source-path substring
as well as a sink, because results are keyed on the *sink*, and a sink like
`logger.cpp` is shared by every log line in the plugin. The file is currently
empty by design: prefer deleting the flow to accepting it.

The script pins **`--enable=warning`** for a reason. The `.cppcheck-suppressions`
baseline is curated for that severity only - broadening to
`--enable=warning,performance,portability` surfaces extra classes CI doesn't gate
(e.g. `memsetClassFloat` on the POD `Unified::` structs, `uselessCallsSubstr`), which
look like "new findings you have to filter" but are just the wider net, not a hole in
the suppressions.

## Principles (read this before adding a test)

A handful of ideas shape the whole suite. None of them are local inventions -
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
   goes red - regardless of how the internals move. A test that knows too much
   about the internals breaks on every refactor and stops being trusted.

2. **Prefer the black box; reach for the white box only when the value never
   surfaces.** Default to asserting the plugin's stable public output - the
   `/api/state` JSON snapshot (via `host.snapshot()`) - because that's a contract
   real consumers depend on, so a test against it is a test of something that
   matters. Only when a computation genuinely never reaches that output (the
   in-game-only real-time gap is the canonical case) do you open a typed
   **white-box hook** (`MXBMRP3_Test_*`) - a **seam** (Feathers), a test-only
   access point compiled out of the shipping DLL - and assert the internal value
   directly. Don't distort the product - don't add a field to the data contract
   just to make it testable - and don't leave the logic untested; add a hook. Keep
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
   signal a layer is coupled that shouldn't be - fix the seam, don't paper over it.

4. **Synthetic tests for precision, real-data golden masters for fidelity.**
   Hand-authored callback streams are deterministic and let you construct the exact
   edge case (a reused race number, a spurious lead, a DSQ) - but they're only as
   correct as *our reading* of the API. A **real captured tape** replayed
   headlessly (`replayTape()`) is the fidelity anchor: it proves the synthetic
   inputs match what the game actually sends. Keep both - they catch different
   failures. A note on golden masters, which have a deserved reputation for being
   brittle and opaque: ours assert **specific, meaning-bearing values
   cross-checked against the session log** (this rider won, this gap, this
   penalty), never a blind blob/byte diff - a *semantic* golden master, so a
   failure names what broke instead of "output changed." (See *Real-data replay*.)

5. **Keep the whole master, commit a slim fixture.** Slimming is one-way, so a
   git-ignored master is archived whole and a small per-test fixture is committed
   from it. `tests/integration/tapes/README.md` has the rule, `slim_tape.py`'s
   profiles, and what is still worth capturing.

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
   coverage number would measure the test build, not the product - and the goal is
   that untested *surface* is visible, not that every line is hit. When you find a
   gap you can't close now, write it down (there and/or as a `Known gap` note)
   rather than leaving it silent.

## Layer 1 - Unit tests (`tests/unit/`)

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
nothing but the C++ standard library - anything reaching into `PluginData` or the
game API is an integration test instead. The authoritative TU list is
`MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt` (regenerate this census with
`ls tests/unit/test_*.cpp`); that list also compiles the production
`mxbmrp3/core/ui_config.cpp` under test. What each pins:

- `test_plugin_utils.cpp` - color/time/hex helpers in `core/plugin_utils.h` (also owns the doctest impl + `main`)
- `test_notice_priority.cpp` - `hud/notice_priority.h`, the masked-notice display-timer decision
- `test_analytics_remote_config.cpp` - the remote sampling cost lever (`parseFullSample`/`shouldSendFull`): fails open to full, deterministic 0.0/1.0 endpoints
- `test_analytics_endpoint.cpp` - App-Key → Aptabase ingest-region routing (unknown/self-hosted → "" = no send)
- `test_analytics_theme.cpp` - the `panel_theme` label (none/shipped/custom/missing, so a user's own theme name never ships) **and** the drift guard: it walks `mxbmrp3_data/themes/` both ways, so adding a shipped theme without listing it in `AnalyticsTheme::kShippedThemes` fails here instead of silently filing its users under "custom"
- `test_director_airtime.cpp` - the director's two airtime helpers: the lull round-robin (`pickNextAirtimeNum`), whose cursor keys on race number rather than grid position, and the dead-air floor (`pickBaselineSubject`), which hands the camera to the broadcaster's own rider once forced rotation is off - and degrades to the leader when that rider is gone, so "Max shot = Off" can never mean dead air
- `test_session_charts_math.cpp` - the race-progression chart derivations in `hud/session_charts_math.h`
- `test_tooltip_length.cpp` - every settings tooltip fits the 2-line/~120-char render limit (compiles the real tooltip table)
- `test_update_asset_select.cpp` - the updater's release-asset picker (the symbols-zip-matched-first regression)
- `test_ui_config.cpp` - INI-only grid-overlay defaults + the `majorEvery` clamp
- `test_render_frame_buffer.cpp` - the plugin-worker-thread triple buffer: the producer never writes the displayed slot; `acquire()` returns the latest published frame
- `test_crash_stack_format.cpp` - the crash handler's backtrace string formatting + the whole-frame `MAX_STACK_CHARS` budget
- `test_hud_sw_renderer.cpp` - golden-frame sampling of the companion window's software renderer (`core/hud_sw_renderer.cpp` compiled natively): quad fill, per-quad alpha, the texel×color modulate (white-icon tinting), `.fnt` text against a real shipped font, and the scale-viewport mapping
- `test_hud_sw_assets.cpp` - malformed `.fnt`/`.tga` input to the same renderer's two binary parsers. Both read **user-supplied** files from the scanned asset dirs, so their hardening is a trust boundary: a bad header must be rejected with the frame untouched (dimension caps, magic, compression type, decompressed size), while a sound header with a truncated payload decodes what it has *in bounds* - the RLE/copy-loop guards, whose teeth are the `unit-asan` gate (deleting one is a heap-buffer-overflow there)
- `test_fmx_scoring.cpp` - FMX trick scoring (`core/fmx_manager` math): rotation scale floors at 1×, air/ground tricks scale with duration (floored) and distance
- `test_segment_cumulative.cpp` - cumulative custom-segment timing: a contiguous run aggregates like the official splits; on-sector identity; isolated-arc fallback
- `test_blue_flag_detect.cpp` - the blue-flag/lapping proximity core (`core/blue_flag_detect.h`): start/finish wraparound, directionality, the deliberately asymmetric backmarker-vs-lapper eligibility, stale-sample rejection, the first-lapper-wins ordering the director depends on, and output clearing (the containers are reused every rebuild). The end-to-end wiring stays pinned by `blueflag_test.cpp`
- `test_battle_groups.cpp` - the battle-group partitioning core (`core/battle_groups.h`) behind the director's battle scoring and the overlay battle panel: adjacency chaining (each consecutive delta within threshold, regardless of total spread), the strictly-positive-delta rule that keeps the pre-first-split field (everyone at gap 0) from fusing into one giant group, lap boundaries never chaining, the maxLeaderPos group filter, and position-sort independence from input order. Eligibility filtering stays in `PluginData::getBattleGroups`
- `test_lap_timer.cpp` - the display rider's live lap-timer state machine (`core/lap_timer.h`): S/F detection strictly via backward wraparound (forward progress and near-line jitter never re-anchor), the grid-start grace in all three exits (opening-lap crossing suppressed with the green-flag anchor kept, lap-1 completion ending it, pit-out abandoning it so the next crossing recovers instead of sticking on the placeholder), official-split anchor resync + sector advancement, invalidateAnchor keeping track monitoring alive, and pause/resume. Transition state is asserted, not wall-clock durations
- `test_live_gap_engine.cpp` - the live leader-relative gap core (`core/live_gap_engine.h`): gap direction per session format (countdown vs count-up clocks - reversed subtraction shows every gap frozen), the freeze-vs-set semantics (stale/finished/unstamped/non-positive all keep the last shown value; leader and lapped riders are explicitly SET), the sub-threshold return that keeps full grids from rebuilding every HUD per 30Hz batch, trackPos quantization incl. the 1.0 clamp, and lap pruning. End-to-end wiring stays pinned by `livegaps_test.cpp`
- `test_marker_label.cpp` - the shared rider-marker label core (`hud/marker_label.h`) that Map/Radar/GapBar all render through: the exact "P%d [#%d]" text per mode, the no-position fallbacks (POSITION renders nothing, BOTH drops to "#%d"), podium gold/silver/bronze applied only in position-showing modes, and the enum's numeric values, which are GapBar's on-disk INI representation. Before the extraction the three HUDs each carried a drifted copy of this logic, agreeing only by review
- `test_settings_serde.cpp` - the enum<->string converter pairs in `core/settings_serde.h` + `core/settings_serde_hud.h`, the entire on-disk representation of every enum setting. They are twin hand-written switch statements, and only their agreement makes a setting survive a restart: a typo on one side saves fine and silently reads back as the default, which no compiler catches because both halves are individually well-formed. Asserts round-trip for every value of every enum, that unknown text returns the CALLER's default (checked against two different defaults, so a hardcoded fallback can't pass), that matching is exact - no trimming or case folding, which is what a hand-edited INI hits - and that every `toString` default arm emits text the load side accepts. Compiles the real header natively via `tests/unit/shim/`. Mutation-tested: a typo'd save string, a swapped pair, and a converter ignoring its default parameter are each caught
- `test_director_scoring.cpp` - the auto-director's story-score formulas (`core/director_scoring.h`). Asserts the RANKING (overtake > battle > drop > lapper > leader baseline) rather than the magic numbers, because the ranking is what makes the director cut to the right thing and the multipliers are meant to stay tunable. Also pins `posWeight`'s monotonicity (call sites compare positions instead of weights on the strength of it), the boost caps, and the battle/lapper crossover at 60% closeness - which is a real consequence of widening the battle-gap setting
- `test_director_detect.cpp` - the overtake/drop edge detectors (`core/director_detect.h`). The two cases worth the file: a rider ahead **pitting or retiring** must not read as a pass (the detector compares relative order, not absolute positions, precisely so it can't), and a drop's position cutoff applies to where the slide **started**, not where it ended - gating on the current position would discard the front-runner-slid-to-the-back story that is the whole point
- `test_camera_resolve.cpp` - spectate camera-name matching and role->index resolution (`handlers/camera_resolve.h`). The director's whole camera choice, extracted from two file-statics plus an inline switch in `spectate_handler.cpp` so the cases that a whole-race test reaches clumsily cost ~1s here: candidate PRIORITY inside a list ("Free-Roam" must beat a lower-indexed "Orbit"), the Auto fallback resolving BY NAME rather than assuming index 0, Free-Roam's deliberate refusal to fall back at all, and the bounded walk over a blob whose `numCameras` overstates its contents. Mutation-tested: a first-match lookup, an index-0 Auto fallback, and a Free-Roam that falls back are each caught. The DLL-level wiring stays pinned by `spectate_cameras_test.cpp`
- `test_standings_context_window.cpp` - the standings pagination (`hud/standings_context_window.h`): which slice(s) of the classification the table draws. Extracted from `rebuildRenderData()` because the interesting cases are exactly the ones a Wine test reaches awkwardly - the window is clamped at BOTH ends and each clamp has to hand its lost rows to the other side, so a rider in P4 (rows lost to the pinned top block) and a rider in last place (rows lost off the end) are the two that silently render short. Beyond the hand-written cases it sweeps field size × rider position × top count × row count asserting the standing contract: a field at least as large as the table always yields exactly `rowCount` rows, always including the rider's own, never duplicated, never out of range
- `test_records_window.cpp` - the records table's context window (`hud/records_window.h`): which slice of the fetched records surrounds the player's own PB. A sibling of `test_standings_context_window.cpp` and deliberately NOT merged with it - this window centres on a PB that is *inserted between* records and may sit past the end of them entirely (slower than everything fetched), which is its own branch, and it always reserves a row for the player's own line. Extracted from `rebuildRenderData()` because reaching these cases under Wine also needs a live records fetch: the player just below the pinned top block, on the last fetched record, past the end, and a list shorter than the table. Sweeps records × rows × player position asserting the window never overlaps the pinned block, never points past the records, and is exactly full whenever enough records exist to fill it. Also carries the panel's **character width**, extracted for the same reason: every enabled column contributes its FULL width, gap included, the last one too. That trailing gap used to be subtracted as waste and is the last column's right clearance - without it the lap time's final digit lands on the same pixel as the player row's highlight band. Invisible unthemed (the highlight spans the whole panel there), so it reached a user; measured at 1px of clearance before the fix and 10 after. Exhaustive over all 64 column combinations, because which column is last changes with the set
- `test_font_metrics.cpp` - the two `LayoutMetrics` numbers that are MEASUREMENTS of the shipped `.fnt` rather than style choices, checked against the atlases themselves. `inkCenterRatio`: every shipped font is normalised by `mxbmrp3_fontgen` with `center = 1`, so its cap/digit band is centred in the glyph cell - the constant that replaced a screenshot-tuned pair (ink height 0.46 + ink top 0.16, which between them put that centre at 0.39) and with it the 0.11-font-size downward bias on every value `inkCenteredY` placed, visible as a widget's number hanging out of the bottom of its own panel once the box model's air terms are set to 0. `charWidthRatio`: a digit's advance through the aspect ratio, asserted flat across every shipped font. It could not be flat before: `RobotoMono-Regular` was left un-normalised so `tools/fontgen/test.sh` had a real PiBoSo-fontgen artefact to compare against, and it is also the DEFAULT for Normal and Digits - so the one un-normalised face was the one most users read, advancing ~3% wider than the layout reserved (a long spotter subtitle overran its card; Normal and Strong did not measure alike). It is normalised now, with that baseline moved to `tools/fontgen/testdata/`
- `test_gamepad_geometry.cpp` - the gamepad widget's unit system (`hud/gamepad_geometry.h`). Reported in-game: raise `[Advanced] uiFontSize` and the controller picture grows while its buttons stay put, walking the sticks and d-pad off their sockets. The frame's width comes from the type, but the ~30 hand-placed per-variant offsets were scaled by the widget's **scale slider alone** - so two of the three things that move the frame (font size, and a theme's content inset) never reached the interior. The fix derives the interior's em by **inverting the frame's own width**, and that inversion is what the cases pin: `kFrameEm` must stay the frame's true em-width (one expression restating another - exactly what rots when someone edits `BACKGROUND_WIDTH_CHARS` or the panel padding), the interior must be similar to itself at any frame width, and a grid retune must not reach it (the 0.0222→0.0235 one that caused the original workaround)
- `test_asset_packs.cpp` - the SHIPPED asset packs (gamepad pads and pit boards)' inis still describe the pads they replaced. The two pads' geometry was ~60 hardcoded assignments in `GamepadWidget::initDefaultLayouts()` before it moved into `gamepads/<name>/gamepad.ini`, and a pure data migration has one failure mode worth guarding: a number that changed on the way across. It does not build, parse or test differently - it just puts a button off its socket. The expectations are the original values, transcribed and written out rather than read from the file under test. Also asserts the two pads are genuinely *different* geometry (the premise of pack-per-pad), and that the key→field table rejects unknown keys while consuming non-finite values for known ones
- `test_plate_geometry.cpp` - the standings race-number plate box (`hud/plate_geometry.h`). One property, and it is what everything about the number's placement rests on: the plate is **centred in its row**. Glyphs are row-centred globally by `BaseHud::rowCenterOffset()`, so a centred plate is what makes the number land centred on it with no plate-local correction. There WAS a plate-local correction - this header used to own the whole story of why digits sit high in a top-aligned text cell - and when glyph centring became global the number received the offset twice and dropped onto the plate's bottom edge. The cases pin centring at any row height, so `kPlateHeightFrac` stays free to change. Where the number actually lands needs the real render, which `standings_layout_test` pinned before the box-model suite came out
- `test_lap_log_plan.cpp` - the Lap Log's row planning (`hud/lap_log_plan.h`): which rows are drawn and in what order. Extracted from `rebuildRenderData()` for one specific trap - the two reserved rows (live current lap, out-of-window best lap) are taken from the recent-lap budget in an ORDER that matters, because the "is the best lap already on screen" scan runs against the budget the current-lap row has already reduced. So turning live timing ON can push the best lap out of the window and cost a *third* slot; swapping those two steps still passes a naive "does it show 5 rows" check. Sweeps maxDisplayLaps × history size × the flag space asserting the constant-height contract (placeholders keep the box the same size as laps come in) and that no lap row points past the history. The `maxDisplayLaps == 1` overflow is pinned as the pre-existing behaviour it is, not silently changed
- `test_icon_resolve.cpp` - icon sprite resolution both ways (`core/icon_resolve.h`): a name or a 1-based shape index to the sprite that gets drawn, and a sprite back to the shape index it stands for. Pins that the two stay inverses once a THEME override is in play - an override sprite is registered past the contiguous base block, so the `sprite - firstIcon + 1` that ten call sites open-coded returns a number off the end of the vocabulary, which makes markers stop rotating (`shouldRotate` reads the filename at that index) and a saved marker resolve to nothing. Also covers the per-name fallback that lets a theme ship three icons and inherit the rest, and the sprites that are not icons at all (a theme's slice sprites live past the block too)
- `test_severity_ramp.cpp` - the shared gauge colour ramp (`hud/severity_ramp.h`): a reading as a fraction of full scale to a colour on the palette's POSITIVE → NEUTRAL → NEGATIVE ramp, used by the G-force ring and the Lean widget's arc and steer bar. Pins the shape rather than any colour - exact anchors at 0 / 0.5 / 1, linear halves, and clamping past full scale, which is what lets two gauges at the same fraction read as the same severity. The alpha case is the one that had already gone wrong once: interpolating RGB and rebuilding the word with a hardcoded 0xFF silently forces a translucent palette slot opaque
- `test_radar_fade.cpp` - the radar's auto-hide fade (`hud/radar_fade.h`): how visible the radar is given who is nearby. Pins the wrap fold at the start/finish line - two riders at trackPos 0.99 and 0.01 are two hundredths apart, not ninety-eight, and without the fold the radar blinks off exactly as a rider crosses the line - and the fact that riders are gated TWICE on different distances (straight-line, then along the racing line), so a rider standing metres away across a barrier but half a lap back does not light it up. Also covers the `trackLength == 0` fallback that applies before session data arrives, and sweeps track position × distance × track length asserting the result never leaves [0,1] (it multiplies the configured opacity)
- `test_peak_marker.cpp` - the shared "max marker" state machine (`hud/peak_marker.h`), which replaced six character-for-character copies across LeanWidget (lean and steer, both sides), BarsWidget and RumbleHud. Pins the branch that reads like a bug and isn't: climbing to a new peak HIDES the marker, because until the reading retreats the marker would sit under the live needle - a "fix" here makes every gauge show a permanently redundant marker. Also the threshold as a deadband on *both* sides (a single trip point flickers at the top of an arc), the linger countdown clearing the held peak on expiry, and why `snapOnImpact` gates on the linger rather than the value: a still-visible marker holds an earlier, higher peak that the impact value must not overwrite, while a *held-but-hidden* peak from a steady input is exactly the case it exists for
- `test_standings_gap_plan.cpp` - the standings gap-column decision (`hud/standings_gap_plan.h`): which value each cell shows, in which style, with which tint. The ~170-line nested if/else that used to sit in `rebuildRenderData()`, now returning a plan the caller formats. Pins the rules a plausible edit breaks silently: a LAPPED or FINISHED rider must fall back to the official gap (a live time difference between riders on different laps reads as seconds when the truth is "+1 lap"); a rider outside the game's ~10-closest track-position batch has a stale `realTimeGap` and must not go live; the leader is exempt from that FRESHNESS requirement but still needs a strictly positive live gap like anyone else (the half that is easy to over-generalise); and a stale PLAYER reference must fall the whole column back rather than skew every row by a zero offset. Plus scope/tint filtering and a totality sweep over the flag space. The end-to-end strings stay pinned by `race_test.cpp` and `livegaps_test.cpp`
- `test_thread_detach_grace.cpp` - the spin-then-detach teardown POLICY (`core/thread_detach_grace.h`), shared by the three singletons that own a worker thread. The fault it guards is a scheduler race and cannot be asserted headlessly (the 150ms is a measurement, 5/24 → 0/24 in `teardown_test` under saturation); what IS a decision, and is pinned here in milliseconds, is that the grace is paid only when the finished flag actually landed, that the spin is bounded so a flag that never lands cannot hang process exit, that the thread is detached and never joined, and that an already-joined thread costs nothing
- `test_text_wrap.cpp` - the settings tooltip box's greedy word wrap (`hud/settings/text_wrap.h`), extracted from a lambda in `rebuildRenderData()` that wrapped and emitted in one loop. Pins what that loop could not state: a word longer than the line is hard-broken, the break-space is consumed rather than re-emitted, the inclusive `rfind` boundary that lets a line use its full width, and - the one that matters for the guard below - that TRUNCATION and an ELLIPSIS are different events, since a line with 3 or fewer characters to give up drops text with no "..." at all. Also sweeps widths 1..30 asserting no line ever exceeds the requested width, and that degenerate widths terminate instead of looping
- `test_spotter_phrase.cpp` - the spotter's phrase composition (`core/spotter_phrase.h`): the racing-style number words ("four seventy six", "two oh six") asserted verbatim against `tests/fixtures/spotter_number_words.txt` - the SAME fixture the pack generator's `spottergen-selftest` gate (`tools/spottergen/generate.py --selftest`) asserts, so C++ wording and every baked `num_*.wav` can only pass together - lap times speaking tenths only with the leading-zero fraction case (".007" is zero tenths - an integer parse of the fraction says seven), "you" vs "rider N" phrasing incl. the raceNum -1 guard that keeps session events from matching a -1 focus, and the empty-string "never spoken" contract (Director cues, your own retirement)
- `spotter_test.cpp` *(integration)* - the cue pipeline through the real callbacks: race events -> `addEventLogEntry` tap -> enable/category gates -> composed text in the cue log (what the subtitle widget shows; audio is not asserted - a Wine prefix has no SAPI voice and the worker degrades by design). Pins that a disabled spotter records nothing, that focus phrasing follows the player, that muting one category leaves the others audible, and the cue-pack resolution order (a pack's override and explicit mute apply while unlisted cues fall back to the built-ins) via the injected-pack hook. Also drives the proximity/hazard detectors through real track-position batches: rider-behind then clear with a crashed rider excluded, and a blue flag announcing exactly once while the flag holds
- `test_spotter_hazard.cpp` - the spotter's proximity/hazard cue state machine (`core/spotter_hazard.h`): edges and restraint over detection that lives in PluginData. Pins the hysteresis hold band (a rival oscillating between behind-on and clear must chatter neither way), "clear" pairing only with a tracked behind and being DROPPED when suppressed by a higher cue (a stale clear with nobody around reads as a glitch), the blue-flag edge retrying after same-tick suppression but consuming silently inside its cooldown, the shared stationary/wrong-way cooldown, and the clock-rewind reset without which a session restart mutes every cooldown-tracked cue
- `test_spotter_milestones.cpp` - the spotter's session-progress milestones (`core/spotter_milestones.h`): "ten minutes to go" / "five minutes left" / "halfway there" as crossing-edged, once-per-session calls. Pins the mid-session-join swallow (the first tick only arms - no machine-gunning already-passed thresholds), the existence rules (a call only exists when the session comfortably clears it; a 20-minute race's halfway IS ten-to-go and collapses into it), the clock-rewind reset, and the lap-race halfway riding the leader's crossings with short sprints excluded
- `test_analytics_spotter.cpp` - the spotter analytics label (`core/analytics_spotter.h`), the panel-theme classifier applied to the spotter: ONE property where off is a value (`"none"`), so adoption is `spotter != "none"` rather than a separate flag. Pins that off outranks a configured pack, that an empty pack is `"tts"` and not `"none"` (the OS voice still speaks - and only on Windows), that a third-party pack collapses to `"custom"` so no folder name off a user's disk ever ships, and that a named-but-absent pack is `"missing"`. Second case walks `mxbmrp3_data/spotters/` so a new shipped pack nobody added to `kShippedPacks` fails the build instead of silently reporting as "custom" forever
- `test_spotter_tts_voice.cpp` - the in-game TTS voice picker's pure half (`core/spotter_tts_voice.h`). Selecting an OS voice is done with SAPI markup, which makes every spoken cue XML - and cue phrases are USER-authored, so the escaping cases are the point: one unescaped `&` in a hand-edited pack would make SAPI reject the utterance and the spotter would go silent on exactly the packs people customise, with a correct subtitle still on screen. Also pins that the voice name is escaped too (it lands in an attribute), stored-by-NAME resolution degrading to the system default when a voice is not installed on this machine, and cycling that treats "system default" as a reachable entry - including the empty-list case every Wine prefix has
- `test_spotter_pace.cpp` - the spotter's pace-report tracker (`core/spotter_pace.h`): gap to the rider ahead/behind at timing points with a gaining/losing trend. Pins the honesty contract - a gap exists only where BOTH riders crossed the same point on the same lap, the behind report stays ARMED until the behind rider reaches a point the focused rider has also crossed (splits resolve it earlier than the full lap) and fires once per arm, trend only compares gaps to the SAME neighbor and stays silent under the threshold, and gaps beyond the cap report nothing
- `test_spotter_mix.cpp` - the spotter's wav chunk mixer (`core/spotter_mix.h`). parseWav is a trust boundary (chunk wavs arrive in SHARED packs): the malformed cases - lying chunk sizes, truncation, stereo/float/8-bit formats, data-before-fmt - must reject whole, and the `unit-asan` flavor is what gives the bounds checks teeth. Also pins the frozen `{rider}`/`{time}` → `rider.wav`/`num_<N>.wav`/`point.wav` chunk-name resolution, that an unresolvable placeholder empties the WHOLE file list (fall back to TTS, never skip words silently), and the assembled RIFF's gap placement and single-sample-rate rule
- `test_install_prefs.cpp` - the installer's analytics opt-out marker (`core/install_prefs.h`), which is how Setup's Privacy-page choice reaches a plugin whose settings file does not exist yet. Every branch is a privacy outcome, and two fail quietly in the direction of sending MORE data: honouring `analyticsOptOut=0` would let an upgrade switch analytics back on for someone who turned it off in-game (Setup cannot read the per-game settings, so it must never overrule them), and re-applying a marker whose stamp was already honoured would pin analytics off forever and beat the in-game toggle. The stamp comparison is done in RECORDED form, which the unstamped case pins - comparing the raw stamp both refused the first application and then repeated it forever, a bug this test caught. What Setup actually writes to disk is covered separately by Case 8 of `run_installer_test.sh`
- `test_pack_ini_path.cpp` - the pack-ini resolution rule (`core/pack_ini_path.h`) with the filesystem replaced by a set of paths. Each of its three branches is a shipped bug if it flips: canonical-wins is what makes an upgrade take effect at all (the user-folder sync copies the new ini in beside the old one and never deletes, so every upgraded install has both); legacy-still-read is what keeps packs published before 1.29.2 alive, and no upgrade step can reach a pack downloaded from a forum post; and resolving to the CANONICAL name when neither exists is what makes a "cannot read" warning name the file an author should create. That the SHIPPED packs actually use the canonical name is the separate census in `test_pack_types.cpp`
- `test_pack_types.cpp` - censuses `AssetManager::PACK_TYPES` (the one table both user-asset copies walk: the startup sync and the RELOAD_CONFIG re-copy) against the shipped `mxbmrp3_data/`. A pack type has to be listed there to be copied out of the user's Documents folder at all, and the failure when it isn't is silent in the worst way - everything works for the developer, whose packs are already in the plugins tree, while the type is unauthorable for everyone else in a folder an uninstall deletes. That is what shipped for spotter voices. The census runs both ways (every row names a real folder; every shipped folder that *looks* like a pack root has a row), recognising a pack root by the FORMAT - a folder of `<name>/<type>.ini` - so a fifth type is caught without being taught here. The media pattern is checked against the real packs too: it is what a pack may carry across the copy, so a wrong one either carries nothing or widens a trust boundary
- `test_spotter_pack_census.cpp` - walks the SHIPPED pack (`mxbmrp3_data/spotters/default/spotter.ini`) against the two published namespaces, in both directions. Neither half of a pack file is checked by anything else: a key nobody emits parses fine and is simply never spoken, and a `{variable}` that isn't a variable parses fine and is printed literally on screen - both look exactly like a working pack until you're on track. Not theoretical: renaming the milestone cues left `ten_minutes_remaining` in the ini while `spotter_milestones.h` still returned `time_10min`, and every gate stayed green. Also checks the keys the plugin can emit - enumerated through `cueKeyFor`, and scanned out of the `spotter_manager*.cpp` TUs' source (globbed, so a new carve-out is covered) for the literals in those Windows-only TUs - against `allCueKeys()`, so the registry can't drift from what actually fires
- `test_fuel_estimate.cpp` - the fuel arithmetic (`core/fuel_estimate.h`), shared by the Fuel widget's readout and the spotter's warning. They have to agree: a voice saying "two laps" while the screen says four reads as one of them being broken, with no way to tell which. The case that earns its place is the first-lap rule - lap 1 includes sitting on the grid, so counting it drags the average up and makes the tank look emptier than it is, exactly when a warning would fire. It is skipped only while still inside the window, a condition expressed through `totalLapsRecorded` rather than a stored flag, and it was untestable while it lived inside a render path
- `test_spotter_vars.cpp` - the `{variable}` namespace (`core/spotter_vars.h`), which is FROZEN once packs are written against it. A variable is a name in somebody else's file: renaming one does not fail to build, does not fail to parse and does not warn - the old name simply stops being a variable and becomes literal text, so a shared pack quietly says "gap {gap_ahead}". Adding a field to `Vars` and forgetting its table row is the same failure from the other side, the variable never resolving. So the cases census the table against a spelled-out list of the published names and against the struct itself (every row reaches a distinct field, no field is unreachable), rather than testing that a lookup works. Also pins that empty is a VALUE and not an absence - that distinction is what lets `expand()` drop an optional group while leaving a typo on screen
- `test_spotter_stretch.cpp` - the pitch-preserving time stretch (`core/spotter_stretch.h`) behind the speed setting, which on the wav paths - the ones most players hear, since SAPI doesn't exist under Wine - has to change the DURATION without changing the pitch. Whether it sounds good is a listening test and was settled by ear (plus a spectral check against a deliberately resampled control); what is pinned here is everything that could rot underneath that judgement: the output duration actually tracks the requested speed, unity is a byte-exact no-op including a dead band so a stepper landing on 0.999 doesn't rebuild the audio, the clamps hold so a hand-edited INI can't ask for 10x, and the overlap-add keeps the signal's energy instead of normalising it into silence. The degenerate cases (empty, shorter than one window, a zero sample rate) are the ones the `unit-asan` flavor gives teeth to
- `test_spotter_cue_pack.cpp` - the cue-pack format (`core/spotter_cue_pack.h`), the contract every shared pack is written against: `cueKeyFor`'s stable key names (a rename orphans every pack's override of that cue), the tolerant parse with empty-value-as-mute distinct from absent-as-fallback, the `_wav` path-escape rejection (a shared pack naming `..\..\x.wav` must never reach PlaySound), and `expand`'s punctuation tidy-up that lets one template serve events with and without a lap time
- `test_spotter_queue.cpp` - the spotter's pending-cue queue (`core/spotter_queue.h`): FIFO order across mixed cue kinds, the drop-OLDEST overflow rule, and the expiry of `perishable` cues at pop. The overflow rule is a pin because a refactor that flips it to reject-newest still "bounds the queue" and passes a size assert, but a final-lap cue would then lose to one from minutes earlier. Expiry is a pin because it is invisible from outside: a queue that never expires anything behaves identically until the pipeline backs up, and then it starts calling a rider alongside who has long gone. Covered: the boundary is inclusive (a cue popped exactly on `kPerishMs` is spoken), a non-perishable cue never expires no matter how late, a run of stale cues is skipped through rather than stalling the fresh cue behind them, and `now < enqueuedMs` cannot underflow into a giant unsigned age that swallows a fresh cue
- `test_cpp_js_parity.cpp` - the C++ side of the cross-renderer mirror vectors (`tests/fixtures/cpp_js_parity.json`): `PluginUtils::isColorDark` and `session_charts_math.h` `formatSecs` against the SAME golden file `tests/web/tests/parity.spec.js` asserts on the overlay JS - a one-sided edit fails one of the two suites
- `asset_path_test.cpp` - `AssetPath::renderName` (`core/asset_path.h`): the display name a discovered asset gets.
- `history_ring_test.cpp` - `HistoryRing<T, N>` (`core/history_ring.h`): a fixed-capacity rolling history, including the wrap.
- `small_vec_test.cpp` - `SmallVec<T, N>` (`core/small_vec.h`): inline storage up to N, heap spill beyond, and the move paths.
- `viewport_test.cpp` - `UiViewport::compute` (`core/ui_viewport.h`): the ONE centered-16:9 UI-rect computation the companion paint loop and InputManager's cursor / window-bounds maps all share - previously three inline copies (integer vs float truncation) that could disagree by a pixel at odd client sizes, so a click landed beside the thing it clicked. Pins orientation, centering, the never-zero guard the inverse map divides by, and forward/inverse round-trips.
- `nine_slice_test.cpp` - the pure geometry of `hud/nine_slice.h`: how a slice grid divides a rect.
- `panel_box_test.cpp` - `core/panel_box.h` against the box model's golden vectors.
- `layout_ini_test.cpp` - the layout/theme ini FORMAT, one line at a time.
- `layout_metrics_test.cpp` - the layout vocabulary as data: the derived values still agree with the terms they come from.
- `grid_snap_test.cpp` - panel ORIGINS land on the snap lattice, not just the rows inside them.
- `gauge_square_test.cpp` - a gauge's dial stays circular while its box lands on the cell lattice.
- `gear_geometry_test.cpp` - the gear digit is sized from the FONT and capped by its box, so a raised `uiLineHeight` gives it air instead of a bigger glyph that runs off the panel.
- `center_stack_test.cpp` - the three centred top panels do not overlap each other or the screen edge.
- `corner_button_test.cpp` - the settings gear and director camera do not overlap and stay on screen.

Exactly one TU defines the doctest impl + `main`
(`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`); every other TU just `#include "doctest.h"`
with no config macro, or the impl is defined twice and the link fails.
The `unit_tests_asan` target rebuilds the same suite under AddressSanitizer +
UBSan - see *Layer 5 - Memory safety* below; a new unit TU gets that coverage
automatically (`MXB_UNIT_SOURCES` is shared by all three targets, no second list).

### Line coverage (this layer only)

`./tests/unit/coverage.sh [floor]` builds the `unit_tests_cov` target with gcov
instrumentation, runs it, and prints a per-file report via
[gcovr](https://gcovr.com/). CI runs it with a floor of `95`, a **ratchet**:
raise the floor when coverage improves, never lower it to turn a red build green.

The number is deliberately **scoped to this layer** - the production code
actually linked into the unit binary - and must not be quoted as a project-wide
figure. Most of the plugin is only reachable across the PiBoSo DLL boundary,
where a line percentage would be expensive to obtain and misleading anyway (a
large share of those lines are render calls with no headless observable). The
honest coverage artifact for that surface is
[`tests/integration/API_COVERAGE.md`](tests/integration/API_COVERAGE.md), which
tracks every export by hand and marks the gaps ⚪/🟠 rather than hiding them in
an average.

## Layer 2 - Integration tests (`tests/integration/tests/`)

These are the heart of the suite. They **cross-compile the whole plugin to a real
Windows DLL** (mingw-w64), load it under Wine, drive the **real PiBoSo callbacks**,
and assert on the plugin's own state snapshot. This is golden-master/
characterization testing: it exercises the entire pipeline (api-export layer →
adapters → `PluginData` change detection → `buildJsonSnapshot`) and catches
*logic* regressions, not just portability breakage.

**Plugin logic is tested in isolation from the serving layer.** A logic test reads
`host.snapshot()`, which calls `buildJsonSnapshot()` **directly** (via a test hook)
- no HTTP server, no socket, no snapshot-rebuild gating. So a plugin-logic test
depends only on the plugin's computation, never on the server machinery. (An
earlier version routed everything through the live HTTP server and one test had to
fire a dummy update just to defeat the rebuild gate - accidental coupling that's
now gone.) The JSON *contract* it reads is still the plugin's own stable public
output, so asserting it isn't coupling to the overlay - the overlay is a separate
consumer with its own layer. `http_test.cpp` owns the serving path: it
starts the real server, fetches over a socket, and checks it serves exactly what
`snapshot()` builds (`http_robust_test.cpp` covers that path's survival against
hostile clients). Internal state that never reaches the snapshot (e.g. the
real-time gap) is read through its own typed hook - see *Test-only hooks* below.

```bash
./tests/integration/run_tests.sh                  # build DLL + run every tests/*.cpp
./tests/integration/run_tests.sh race sessions    # subset by basename
TEST_DEBUG=1 ./tests/integration/run_tests.sh race  # dump the driver trace on failure
MXBMRP3_TEST_TIMEOUT=30 ./tests/integration/run_tests.sh  # tighten the per-test abort cap
```

**Per-test timeout.** Each test binary runs under a wall-clock cap (default **120s**,
printed as `(cap Ns)`) so a *hung* test - a deadlock or infinite loop - is aborted with a
clear `TIMED OUT` line instead of silently burning CI minutes until the job-level cap. A
healthy test finishes in ~1–10s (the runner prints each one's elapsed time), so the default
is pure headroom. Override with `MXBMRP3_TEST_TIMEOUT=<seconds>` to tighten it locally or
loosen it for a genuinely slower run. The specialized runners have the same knob with their
own defaults: `MXBMRP3_PERF_TIMEOUT` (180s), `MXBMRP3_PERSIST_TIMEOUT` (60s),
`MXBMRP3_FUZZ_TIMEOUT` (60s/case), `MXBMRP3_CALLBACK_FUZZ_TIMEOUT` (300s).

Each `tests/*.cpp` is a self-contained doctest binary with its own plugin
lifecycle and HTTP port, run in an isolated Wine process with a clean save dir.
The runner auto-discovers every `tests/integration/tests/*.cpp`, and this table is
a full census of them - `tools/check_docs.py` (CI) fails if a test file exists
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
| `telemetry_companion_test.cpp` | **the producer half of the visibility gate**: telemetry history buffers keep accumulating for a HUD visible only on the OPEN companion window, and stop when the window is closed or the HUD is hidden on both surfaces. Read via `MXBMRP3_Test_TelemetryHistoryDepth` (the buffers never reach `/api/state`). Verified to fail against the pre-fix `isVisible()` gate. The last case covers the **consumer** half - the show edge that clears stale samples - and carries a POSITIVE CONTROL (`REQUIRE(depth > 0)` right before the edge) because the first version of it passed with the bug present: the harness's own first `draw()` flips spectate↔on-track, which calls `clearTelemetryData()` and emptied the buffers for reasons unrelated to visibility |
| `benchmark_companion_test.cpp` | the same gate for the **benchmark profiler's collection switch** (`bm.active`), from a real report: enabled only on the companion window, the profiler rendered its tables and never filled them. Latching `active` from the `setVisible()` override missed `setCompanionVisible()`, which is not virtual. Visibility cannot see this - the widget draws either way - so it reads `active` via `MXBMRP3_Test_BenchmarkMetricsActive`. The last case is the one that constrains the FIX rather than the bug: **closing the companion window** with no toggle touched must stop collection, which a setter-side fix would miss. Verified to fail against the `isVisible()` gate (2 of 4 cases) |
| `spectate_click_test.cpp` | **click-to-spectate is offered on exactly the riders it can reach**: `PluginData::isRiderSpectatable()` is the one gate for Standings / Map / Event Log / Session Charts (DNS/retired/DSQ/pitted/unknown-rider all excluded, and a pit exit re-enables it), plus the Event Log end-to-end - a row is clickable only when the event names a rider AND that rider is still reachable, so a retirement's own row (and the earlier rows about the same rider) go inert; plus **auto-hide drops the click targets with the rows** - the input block runs before the auto-hide check and rebuildRenderData() doesn't run while hidden, so regions left behind kept hit-testing an invisible HUD |
| `chart_sectors_test.cpp` | **Session Charts at sector resolution** (`ELEM_SECTOR_POINTS`, off by default): three completed-lap samples become nine, the LIVE in-progress lap extends the series on each RaceSplit (before any lap completes), practice stays per-lap (off-race ranking is by best lap, which has no sector analogue), one rider's broken splits fall the WHOLE field back to per-lap rather than ranking a sector against a lap, and the snapshot carries the per-sector series (plus the `sectorCount` stride) the web overlay draws from |
| `hazard_reach_test.cpp` | a **wrong-way** hazard is scanned for further ahead (`hazardWrongWayAwarenessDistance`, 250m) than a **stationary** one (`hazardAwarenessDistance`, 100m), because an oncoming rider closes the gap at roughly double the rate and the 1.5s wrong-way confirmation eats most of the warning. Pinned as a matched pair at ONE distance - a wrong-way rider and a crashed rider both 208m ahead, only the first in reach - so it can't pass by simply raising the threshold for everyone |
| `blueflag_test.cpp` | **blue-flag detection semantics**, via the `MXBMRP3_Test_IsRiderBlueFlagged`/`IsRiderLapping`/`RiderLappingTarget` hooks: proximity threshold, the leader/lead-lap cases, the same-lap early-out, and pit exclusion. Written **test-first** against the original O(n²) implementation, so it pins behaviour across the scratch-array refactor rather than describing it |
| `livegaps_test.cpp` | the overlay live-gap data contract: per-rider `liveGapMs`/`liveGapValid` (valid for leader/active, false for dropped-out/lapped) - always emitted; the on/off is a client-side overlay setting |
| `overlay_snapshot_test.cpp` | the **whole** `/api/state` shape, not one field of it: the live snapshot must keep every key path and JSON type of `tests/fixtures/overlay_snapshot.json`. Its twin, `tests/web/tests/overlay_snapshot.spec.js`, feeds that same file through the overlay's real `render()`, so a renamed field fails here, and an overlay still reading the old name fails there. Closes the hole where both suites passed while the live overlay drew nothing - every other web test drives `?demo`, whose snapshot the overlay writes itself. Regenerate deliberately: the test dumps the live JSON to `/tmp/mxbmrp3-tests/overlay_snapshot.new.json` and prints the copy command |
| `session_format_test.cpp` | race-**format** clock: pure-laps/time/time+laps `format` string, and the **finish-before-timer** overtime state machine (`00:00` freeze → N TO GO → FINAL LAP → CHECKERED) |
| `timing_reference_test.cpp` | Timing HUD via the `MXBMRP3_Test_Timing*` hooks: progressive reference selection (S1 → S1+S2 → whole lap, tracking the lap timer's track-position sector from the first flying lap), pit-exit timer reset, INVALID shown for a cut lap but suppressed on a pit out-lap, freeze on the first flying lap after a garage start, grid-start timing from the gate drop + the green-flag grace window, and panel height a whole number of grid bands |
| `spectate_test.cpp` | the camera/spectate chip follows the spectated rider through `SpectateVehicles` |
| `spectate_cameras_test.cpp` | `SpectateCameras` **wiring**: a director camera-role request posted through the real entry point is resolved to an index and written back via `*piSelect` with the "I changed it" return; the request is consumed exactly ONCE (at ~140 calls/s a sticky request would pin the camera every frame); no re-cut when already on the wanted camera; Free-Roam absent leaves the camera alone; manual-camera (Orbit/Free/Free-Roam) detection that pauses the director, including re-resolution on a camera-LIST change so a stale flag can't survive a track change. The per-case resolution rules are unit-tested in `test_camera_resolve.cpp` |
| `vehicle_data_test.cpp` | `RaceVehicleData` - the ONLY telemetry source while spectating/in replay (RunTelemetry is player-only). Only the DISPLAY rider's frames land (others arrive at the same rate and must not overwrite the screen), `active=0` frames are ignored, lean is NEGATED into roll (opposite sign conventions; a dropped negation mirrors the bike), and on track it stands down entirely for RunTelemetry's full frame. Read via `MXBMRP3_Test_BikeTelemetry` - telemetry is not in the snapshot. Mutation-tested: dropping the negation and dropping the display-rider filter are both caught |
| `deinit_test.cpp` | `EventDeinit` / `RaceDeinit` **clear the world**: a populated 3-rider session goes empty, repopulates cleanly afterwards, and nothing survives into a DIFFERENT event that reuses a race number. Also that either callback is safe with nothing to clear (the game fires them without a matching init). The per-rider mirror of this is `racenum_reuse_test.cpp` |
| `run_split_test.cpp` | `RunSplit` is a **deliberate no-op** (RaceSplit owns all split handling): driving it creates no current-lap split state and leaves the splits RaceSplit computed untouched. Asserted through `MXBMRP3_Test_CurrentLapSplits`, not the snapshot - current-lap splits never reach `/api/state`, and a snapshot-only version of this test passed a mutation that made RunSplit start recording |
| `sessionstate_test.cpp` | `RaceSessionState` green snapshots the grid; session started/ended events |
| `benchmark_registry_test.cpp` | the benchmark profiler's **registry survives a session teardown**: `PluginData::clear()` (RaceDeinit / EventDeinit) used to wipe `BenchmarkMetrics`, and since HUDs are registered only once in `HudManager::initialize()` that left `hudCount` at 0 for the rest of the run - every report read "HUDs profiled: 0" with an empty table. Callbacks re-register lazily, which is worse than going empty: a stale cached index lands in whichever slot re-registered first, so timings appear under the **wrong callback name** |
| `position_widget_test.cpp` | **PositionWidget is woken by every change that moves its readout** (Standings for the position, RaceEntries for the "/ 22" denominator) and by nothing else: it used to recompute both every frame and compare against a cached copy, which made it the most expensive widget in the plugin - 1.41us/frame with zero rebuilds - because it was the only per-frame caller of the lazily rebuilt position cache. The failure the subscription replaces it with is invisible to every other test: a widget that stops rebuilding leaves the snapshot, `/api/state` and the standings all correct, and only the pixels stale - so the assertion is on the profiler's own per-HUD rebuild counter (`MXBMRP3_Test_HudRebuildCount`) |
| `asym_border_test.cpp` | a lone value is centred in **the card it is drawn on**, not in the content band inside it. `[content] border` is CSS shorthand and may be asymmetric; the band is inset by `border.t` at the top and `border.b` at the bottom, so it shares a centre with the card ONLY while those are equal. Every shipped theme is symmetric, so the Version widget, the Gap Bar and Notices all centred in the band and looked identical to Timing (which centred in the card) everywhere - until a skinner wrote `border = 2 0 4 6` and reported them sitting high. The synthetic themes the rest of the suite installs take one uniform border and cannot express the case at all, which is why it was reachable only in game: this drives the asymmetry through `MXBMRP3_Test_SetThemeContentBorder` and asserts against the slab's own rect. Mutation-tested - restoring the band-centred placement puts the text 2.5 cells high and fails |
| `card_anchor_sweep_test.cpp` | every HUD's content anchors to **the box that owns it** - the card for body content, the title band for the caption - never to the outer panel. A property test over 16 HUD ids rather than a list of panels caught so far: grow a right-only `[content]` (then `[title]`) margin and every drawn string and quad must keep its offset from its card rect (`MXBMRP3_Test_HudCardRect`); a panel sized from its content widens while the card stays put, so anything tracking the panel's centre or right edge moves and fails, and a pinned-width panel's shrunken card allows only the three legitimate anchors (card left/centre/right). Mutation-tested - re-centring the Speed value on the panel moves it half the widening and fails |
| `crash_widget_test.cpp` | the crash widget's **streaming tally outlives every boundary that scopes the ordinary crash count**. StatsManager's `crashCount` is per track+bike; this number is a streamer's, running across practice, races, server hops and game restarts until they press Reset - so the test crosses a track+bike change and a full plugin unload/reload and asserts the count carries through both. The failure it guards is silent: a tally wired to the wrong lifetime restarts at a track change and looks perfectly correct on screen. Also that the count follows the **rising edge** of `m_iCrashed` (a rider who stays down is one crash, not one per frame), that Reset goes through the widget's own `resetCounter()` - the entry point the button and the hotkey share - and that it reaches **disk immediately** rather than at the next leave-track flush, since a count resurrected by a crash-to-desktop is the one failure a streamer would notice. A second case pins the **shared content box**: the widget was sized to tile with Speed and Gear, and each of the three computes that box from its own dimensions, so the panel rects are compared via `MXBMRP3_Test_HudPanelRect` (with Speed == Gear asserted first as the premise, so it cannot pass vacuously against a moved target) |
| `render_probe_test.cpp` | the **render probe emits the primitive it claims to**: `renderProbeType` 0 adds N *untextured* quads (sprite 0 = "fill with `m_ulColor`"), type 1 adds none, a pinned `renderProbeSprite` is the only sprite in the frame, and an **out-of-range pin falls back to cycling rather than to sprite 0**. That last one is the whole point: the probe's output is only ever read as a frame-time difference between two runs, so a probe that quietly drew flat fill while the report said "textured" would not fail - it would produce a number, and the conclusion drawn from it (that texturing is free) would be the opposite of the truth . Also that the **automatic sweep gives the user's probe settings back** on abort: it drives those settings itself, so failing to restore leaves the plugin permanently emitting up to 2000 synthetic primitives a frame - which does not crash and does not warn, it just looks like the plugin got slow . Plus the **zero-opacity background skip**: a panel at opacity 0 emits no background quads at all, because the probe measured a fully transparent quad at 100% the cost of an opaque one (the engine bills for submission, not pixels) - a themed panel was paying for ~27 invisible quads a frame. The reposition case there is documented as NOT currently discriminating: the count guard it describes is defensive and unreached headlessly, and says so in the test |
| `about_tab_test.cpp` | the **About page is reachable from the footer and absent from the tab list**, and the **Updates tag follows the available version**. The hidden-tab split fails in two directions and both are quiet: listed anyway grows the sidebar and the panel with it, which no other gate calls wrong; hidden AND unselectable leaves a button that does nothing. So the list is asserted not to contain it while a real footer click (through `dispatchRegion`, the path that also runs the easter-egg counter) is asserted to open it. The tag cases pin the rules that make it useful rather than annoying: it clears on CLOSE, not on open, so it is still there while you read the tab it points at; it is keyed on the VERSION string, so a newer release re-arms it with no code noticing; and it is kept apart from the SKIP-version state, so reading about an update never silently skips it. The persistence case wipes the seen-version in memory between save and load - re-announcing the same version cannot do that job, since a version already seen stays seen |
| `whats_new_test.cpp` | the **"New" markers on settings tabs and rows appear, dismiss by their own rule, and stay dismissed**. The rules are ASYMMETRIC on purpose and that is what this pins: opening a tab clears its TAG but leaves the row BANDS ("I know something is here" is not "I found it"), and hovering a row clears only that row. A test treating them as one switch would pass against an implementation that cleared everything on the first click. Drives the tab through `handleTabClick`, the sidebar's own path - deliberately NOT `setActiveTabByName`, which is also the persisted-tab restore and must not dismiss anything before the player has opened the menu. The persistence case wipes the in-memory set between save and load, so a dismissal that never reached the file reads back as a fresh player. Mutation-tested by disabling the load. The marker table's freshness is a separate CI gate, `check_whats_new.sh` |
| `ink_legibility_test.cpp` | **a caption drawn on a coloured slab clears the plugin's own luma threshold**, on the Notices slabs and the Gap Bar's figure over its fill. Both draw text whose colour comes from the same NEGATIVE/POSITIVE rule as the block behind it, so the two land on the same slot whenever they agree - red ink on a red block. It hides at the shipped background opacity, where the slab is a wash and full-strength ink still reads, so the test drives **opacity 1.0**, the state it was reported in. The whole legibility family (`legibleOnFill`, `captionOnSlabColor`, `chipGlyphColor`) had no test before this, which is what let the helper ship applied to Notices and not to the Gap Bar while its own comment claimed both. Compares each string against **every quad it sits on** by point containment, not against the panel's widest quad - the first version did the latter, and passed with the fix reverted, because the Gap Bar's widest quad is its background and the fill reaches at most half the box. Threshold and luma formula are read from the plugin (`MXBMRP3_Test_MinGlyphLumaGap` / `_Luma601`) rather than restated. Mutation-tested: with the correction removed the figure measures **0 luma** from its slab |
| `drop_shadow_test.cpp` | the **settings panel emits no shadow strings**, and the suppression is BEHAVIOUR rather than a stored preference. A shadow is a second string under the first (`collectSurface`), and a string is the priciest primitive we submit (~2.7x a quad); the settings panel always draws its own opaque background, so its 124 shadow strings render under a surface that hides them. Asserted as a difference of differences (panel closed vs open, shadow off vs on) so no tolerance is needed for the stray string another panel legitimately shadows. The third case round-trips settings: `setDropShadowOverride` is sparse-saved and cleared by the authoritative apply, so a panel that set the override on itself would look right on a fresh install and silently shadow again ever after |
| `director_test.cpp` | auto-director **battle detection** splits two close groups at the gap break; director advisory inert by default |
| `director_lock_test.cpp` | auto-director **rider lock (hold)** release rules: the lock survives ordinary standings churn but is released when a new session (session-generation bump) resets the field |
| `director_broadcast_test.cpp` | auto-director **broadcast measurement**: replays a real tape with an injected sim-clock (from tape timestamps) so the wall-clock shot pacing plays out, then parses the director's own cut log to report cut count/rate, shot-length spread, shot-type + camera mix, and per-rider screen time - asserting it lands in a plausible broadcast band and rotates across the field (not glued to the leader). Uses the `MXBMRP3_Test_DirectorSetNowMs` clock hook + `replayTapeTimed()`. Most cases set the full-auto 8 s/25 s pacing explicitly, since that is no longer the shipped default; one case replays the same tape under the **shipped defaults** (Max shot Off) so the out-of-box show is measured too - no `maxshot` cuts, no onboard cameras, still cutting. Tapes record no `SpectateVehicles`, so no home rider is adopted on a replay and that case is the leader-fallback *degrade* path; the return-home behaviour is `director_home_test`'s |
| `director_home_test.cpp` | **"Max shot = Off"**, the forced-rotation switch: with it off the director never cuts on a timer - it holds the broadcaster's own rider through a quiet race, still lets a story take the camera, and returns *home* when the story ends. Covers all four `forcedRotation()` gates (race variety cut, lull round-robin, non-race dip, rider-lock camera cycle), **each against its own rotation-on control** - without those controls "the camera didn't move" would pass on a director that never moves it. Also pins that the camera is on Auto/Trackside whenever it's Off - including the two states that hold a camera without cutting (a rider lock, and a shot already dipped into an onboard when the setting changed), which are corrected back to the TV shot - that a manual pick re-homes the broadcast, and that an out-of-range `maxShotSec` clamps *into* the range rather than reading as Off (a hand-edited `3` must mean "cut fast", not its inverse) |
| `director_events_test.cpp` | director **transparency events**: shot decisions and state changes reach the event log as Director-typed entries, state transitions carry the director button's state colors (cuts keep the per-type default), and they're emitted **unconditionally** - the in-game toggle and the overlay filter at *display* time (raw-data contract). A final case pins that **only one rider is logged as the race winner**: `SHOT_FINISH` covers the whole run-in to the flag, not just the win, so after the winner celebration the finish lock cuts to riders still racing and each of those was announced as the winner too. Counts the winner lines rather than checking one rider, and carries a positive control (`REQUIRE` the winner line exists) because "nobody was called the winner" passes trivially if the finish window never opened |
| `theme_override_test.cpp` | **Per-HUD panel-theme override clears when absent**: the key is captured sparsely, so reset and entering a profile that carries no theme key must UNPIN the HUD -- while a set override still survives a save/load round trip |
| `reset_test.cpp` | **Reset All scope** (#212/#214): per-profile HUD settings revert to factory default, global sections (Rumble/Hotkeys) untouched |
| `reset_profile_test.cpp` | per-profile **operations** on the profile-diff (`[HudName:Profile]`): active-profile / per-HUD reset scope, copy-to-all, and switch-profile persistence |
| `autoswitch_test.cpp` | **auto-by-session profile switch**: with the flag armed, the active profile follows the session type (Practice/Qualify/Race); with it off, a session change no longer overrides a manual pick |
| `pack_texture_variant_test.cpp` | a **pack HUD keeps its artwork across an upgrade**: an INI written before the pit board and gamepad pad became packs still carries `textureVariant=1`, and `applyBaseSettings` walks a section's keys in map order, so `showBackgroundTexture=1` was applied first and the variant turned it straight back off. The panel then drew its flat colour with the loose stick/button sprites still on top - a grey slab wearing half a controller - with nothing thrown and nothing logged. Asserts the background-texture FLAG, not the resolved sprite: the sprite needs pack art installed and the Wine harness stages none, so a sprite check reads 0 for "no packs here" exactly as it reads 0 for the bug (this test was written that way first and failed with the fix in). A no-variant control case pins the fresh-install shape alongside it |
| `stats_test.cpp` | player **personal-best lap** persists to the stats JSON (faster-replaces-only) + top speed and the `finiteOrZero` +Inf write guard |
| `sprite_order_test.cpp` | the **sprite-order self-check** (`HudManager::verifySpriteRegistrationOrder`, run at the end of `setupDefaultResources`): discovery hands out absolute 1-based sprite indices and registration pushes file names in what must be the same order, two walks in different files mirroring each other's block arithmetic - a skew is silent (everything draws, with another asset's art). Green is asserted on the trees where the arithmetic is at risk: a rejected incomplete theme alphabetically BETWEEN accepted ones (the rewind-on-rejection path), a theme skin with own art (registers after all standalone themes, out of directory order), and gamepad packs following the theme block. The must-catch half re-runs the checker with two table entries swapped (`MXBMRP3_Test_SpriteOrderWithSwap`) and requires non-zero - without it a checker that compared nothing would stay green forever |
| `pb_scope_test.cpp` | the **all-time-PB notice follows the active PB scope**, not the per-bike write: under the default `PBScope::CATEGORY` the reference is the fastest lap across the whole **class**, so a first lap on a second bike in that class stores a PB for that bike (asserted on disk - storage stays per-bike) yet must NOT fire the green notice unless it beats the class best (`PersonalBestUpdate::beatsScopedBest`); a first lap in a *different* class still notifies |
| `odometer_test.cpp` | **odometer/distance accumulation**: distance integrates speed over the wall-clock gap between telemetry ticks, so the test injects the odometer clock (`MXBMRP3_Test_StatsSetNowUs`) for exact per-tick dt - accumulation is exact, the **~100m dirty-coalescing** marks dirty once then resets, a +Inf/NaN sample adds nothing (finiteOrZero), a >0.5s gap is discarded, and the total persists finite on the leave-track flush |
| `fmx_test.cpp` | **FMX trick detection + scoring** through the real RunTelemetry path under the injectable FMX clock (`Fmx::clockNow()` / `MXBMRP3_Test_FmxSetNowUs`, 10ms sim steps): a sub-debounce hop banks nothing (airborne debounce); a sustained airborne full-pitch rotation classifies as **BACKFLIP**, survives the 0.75s landing grace, and banks a non-zero score into the session when the 2s chain window expires; a crash during grace fails the trick without touching the session score (state via `MXBMRP3_Test_FmxState`) |
| `records_parse_test.cpp` | records provider (MXB-only): canned CBR / MXB-Ranked responses through the **real parse path** (`MXBMRP3_Test_RecordsParse`) - field mapping incl. seconds→ms + date truncation, malformed/truncated/empty JSON rejected without crashing (zero records), absurd values handled sanely (multi-KB names truncated, negative/wrong-typed times, >MAX_RECORDS capped); plus the **fetch worker** via the stub seam (`MXBMRP3_Test_RecordsSetFetchStub`: sleep + canned response, no network) completing through the real thread, and **shutdown mid-fetch** pinning the join contract - `HudManager::clear()` joins the fetch thread (now owned by `RecordsFetcher`, `core/records_fetcher.{h,cpp}`) *before* nulling the cached HUD pointers the worker touches (TimingHud) |
| `version_test.cpp` | update-checker version ordering (numeric, not lexicographic); plus the load-time **API handshake** - `GetModID`/`GetModDataVersion`/`GetInterfaceVersion` resolve under the exact export names the game looks up and answer what `mxb_api.cpp`'s static_asserts agreed to |
| `updater_test.cpp` | update install pipeline (backup→extract→verify→**rollback**) + **locked-file retry**: aborts intact when the target is held; a transient lock is recovered by the move retry |
| `settings_migration_test.cpp` | a version-mismatched INI (missing / `=4` / `=99` version line) keeps the user's HUD settings instead of silently wiping them |
| `settings_tab_test.cpp` | the settings menu **remembers its open tab**: the focused tab round-trips through save→load (by name, in `[Profiles] activeTab`), and an unknown/unavailable tab name is ignored (no empty tab) |
| `settings_sections_test.cpp` | every section `captureToCache()` produces is actually **serialized** to the INI (via `MXBMRP3_Test_CapturedSections`) - belt-and-suspenders guard on the per-HUD serializer registry (the old capture/apply/`hudOrder` "third hardcoded list" / FriendsHud silent-revert trap, now structurally one list) |
| `settings_idempotency_test.cpp` | **apply-path coverage (defaults)**: `save→load→save` is byte-identical (and a second round too), forcing `applyProfile` to read back every serialized enum/float/int/bitmask at its default and re-capture it - an asymmetric parse/clamp/format bug diverges the files |
| `settings_apply_values_test.cpp` | **apply-path coverage (non-defaults)**: `[Hud:Practice]` overrides carrying non-default enum/float/int values survive a load→save round-trip only if `applyProfile` applied them to the live HUD (re-captured as a sparse diff) - closes the idempotency test's default-only blind spot (`stringToX`/`validateX`/`std::stoi`) |
| `settings_defer_test.cpp` | **deferred auto-save**: `markDirty()` applies a change live but writes *nothing* to disk; `flushIfDirty()` (the leave-track flush) then writes exactly once; a flush with nothing dirty is a no-op - the "no settings write while the player is on track" contract |
| `companion_decouple_test.cpp` | **per-surface companion decoupling** on the live StandingsHud (via the `MXBMRP3_Test_Standings*` hooks): mirror-while-unconfigured → snapshot-on-first-edit (diverge) → clear-reverts-to-mirror; a diverged HUD persists its `companion*` keys through the real serializer while a configured-but-equal HUD writes **none** (upgrade-safe sparse save); per-surface render routing (game-frame suppression, companion filtering + offset, X-close fallback); and a HUD hidden in-game but shown on the companion still updates. The last case is the opposite rule - **HelmetOverlayHud never reaches the companion at all** (`BaseHud::rendersOnCompanion`), because it is a full-screen in-game effect with no position or scale to decouple. It asserts on the FRAME, not a visibility flag, since the flag was never the bug; it needs ON_TRACK (the overlay draws nothing while spectating) and the visor tint (helmet parts need a texture variant no headless run stages); and it sets the helmet visible on BOTH surfaces, because the companion instance is snapshotted from the game flag and setting only the game one leaves the frame flat for the wrong reason - which is how the first version of it passed against a mutant. A sixth case covers the opposite half of the same cluster - the Director tab's **Visible** row must edit the FOCUSED surface, driven through the real click path (hit-test → `dispatchRegion`) because the bug lived in the dispatch, and asserting both that the companion flag changed and that the game flag did not |
| `gamepad_layout_test.cpp` | The gamepad widget is a **picture** of a controller - frame sized from the type, ~30 button/stick offsets hand-placed against the artwork - and two references for one drawing has slid the buttons off the face twice. **Golden**: the content's bottom/right extent as a fraction of the frame, which catches the #256 `LineHeights::NORMAL` regression (0.754/0.878 against 0.7237/0.8562). **Invariant**: that same extent must not move at all across `uiFontSize` 0.014–0.032 (`MXBMRP3_Test_SetUiFontSize`), because it is a fraction - the widget either scales whole or it does not. The invariant is the case that matters: the offsets were once scaled by the widget's scale slider alone while the frame also grew with `uiFontSize`, and a golden taken at one font size is exactly what missed it. Fake controller via `MXBMRP3_Test_FakeGamepad`; the arithmetic underneath is pinned in `test_gamepad_geometry.cpp` |
| `asset_pack_test.cpp` | An asset pack (gamepad pad, pit board) is selected **by name**, and an unknown name degrades **without forgetting**. It used to be selected by texture-variant index, which silently reassigns everyone's pad whenever a pack is added or renamed. The replacement rule has two halves and only the first is obvious: an unresolvable name must still render (the shipped default), *and* must not rewrite what is stored - a user who moves a pack folder out and back gets their pad back. `gamepadPackStored()` vs `gamepadPackActive()` exist to tell those apart; they are equal in every ordinary case, which is why checking one would pass while half the rule was missing. Covers the no-packs-at-all install too (null pack, solid-colour fallbacks, no crash). Both pack types get the same cases deliberately: the rule must hold for each, and a type that half-implements it is the regression worth catching. The board adds one the pad has not - its panel SHAPE comes from the pack, where the aspect used to be a compiled 1920/1080. Packs installed file-lessly via `MXBMRP3_Test_InstallGamepad`/`InstallPitboard`, the same way `MXBMRP3_Test_InstallTheme` installs a theme. Also pins the **pack cycle's Off entry** - a shipped bug: dropping Off from the cycle deleted the only UI control over `showBackgroundTexture` (for every HUD, `setTextureVariant(0)` is what clears it), stranding anyone whose art was already off. The widget state was never wrong, so headless captures showed nothing; these cases drive the settings cycle itself via `MXBMRP3_Test_CyclePack` and assert what a user can get back to, including with a single pack installed |
| `pack_skin_test.cpp` | A pack SKIN (`base = <pack>` in the `[pack]` section) layers over its base: missing sprites and omitted geometry keys resolve from the base pack, own files and keys win - the spotter voice pack rule applied to gamepad and pit board packs, which is what makes a reskin two files instead of a copy of seventeen. Where asset_pack_test deliberately fakes packs to test the by-name SELECTION rule, this one stages a real `plugins/mxbmrp3_data` tree of minimal TGAs in its own temp directory, chdirs into it (discovery scans relative to CWD; the ctor/dtor restore the CWD because integration_main's sentinel file is CWD-relative too), and runs the REAL scan through `Startup` - faking discovery would test the test. Pins per-stem resolution (`MXBMRP3_Test_*StemSource`), geometry inheritance and override (`GamepadGeomWidth`, `PitboardPackArtWidth`), the pack's `[text] color` surviving bit-exact through parseRgbHex with the ABGR byte order pinned (`PitboardTextColor`), a skin whose own art reseeds its own aspect, and the whole-pack rejections: unknown base, a base that is itself a skin (one level only), and a baseless pack still needing the full set. A skin sorting alphabetically before its base is the staged layout, so the two-phase scan is what passes. **Themes** were added last and are the type where this buys most: their art is cut from a master by `themeslice`, so sharing slices is the small win - the big one is that a theme's ini carries the whole palette, fonts and box terms while a theme was only accepted with its full 27-slice set present, so "Carbon Dark but my colours" cost 27 `.tga` copies to change three lines. Theme sprite indices are per FILE rather than a fixed array, so a skin that redraws nothing registers nothing and keeps the base's indices outright - `own=0` in these cases is that property, and it is what keeps HudManager's registration loop correct without it knowing bases exist. Read through one `MXBMRP3_Test_ThemeInfo` descriptor, because own-file count, inherited sprite index and overridden palette are one question: did this skin layer correctly |
| `gauges_migration_test.cpp` | Custom dial art drawn BEFORE gauges packs survives the upgrade. Making the tacho and speedo pack HUDs stops them consulting the flat textures directory, so a user who had redrawn `tacho_widget_1.tga` would have watched the shipped face replace it - the pit board and pad break (grey boxes, reported with screenshots) with a wider blast radius, because a dial is one square picture with no geometry to match and is the easiest thing in the plugin to redraw. `AssetManager::migrateLegacyGaugeArt` turns that file into a real pack in the user's OWN Documents tree, once. Asserts the copied BYTES rather than just the filename (empty placeholders would satisfy every other check and still lose the art), that the source is left alone, that `base = classic` is written so ONE redrawn face still makes a complete pack, and both halves of the one-shot marker: a pack the user deletes stays deleted, while an install with no legacy art writes NO marker so somebody who adds art next month is still migrated. Also asserts the OUTCOME, not just the emitted text: it stages a real `classic` base with a deliberately non-default rev limit, lets discovery run, and reads the resolved pack back through `MXBMRP3_Test_GaugesInfo` - base bound and the base's dial ranges inherited. That case exists because the text-only version passed through a release where the `[pack]` rename had moved under the writer's feet: every `base` the migration generated was being silently dropped, a user who redrew only the tacho got a pack that failed the completeness check and vanished, and the migration is one-shot so a later fix could not repair it. The rule it encodes: when code WRITES config, assert that the reader understood it |
| `map_render_test.cpp` | MapHud **world-ribbon cache is transparent**: a real 2D track emits non-empty, all-finite quads in every view mode, and default-view geometry is bit-for-bit reproducible across a detail round-trip and rotate/zoom visits; the detail **20-200% dial** has real range, **adaptive** mode normalizes quad count across track lengths (fixed mode scales with length), legacy `detail=AUTO\|HIGH\|LOW` INI values migrate to scale/adaptive; a degenerate 1D track never produces a non-finite vertex |
| `theme_panel_padding_test.cpp` | **`[panel] padding-x/-y`** - the gap between a panel's border and its first glyph, which was the last unreachable number in the box model (with every border at 0 a panel still held its content 2 cells in, and no file could change it). Deliberately pins **no number**: the box-model tests were removed because they froze quantities that were in flux, so every case here is relative - unset renders identically to naming the built-in's value (what the `-1` sentinel buys over a copied default), and raising it moves both the content column and every panel's `paddingV`. The third case is the one that taught something: the two axes **die differently** below the frame's border. X is `max(base, frame + inner)` so the key is dead *exactly*; Y is `base + ceilY(frame + inner − base)`, which is neither a max nor monotonic - raising the base re-phases the ceil, and measured, 1 cell of padding comes out *below* the padding-0 value on a panel not at scale 1.0. So Y is pinned as the **clearance invariant** (`paddingV >= ` the panel's own frame border, at any setting), which holds by construction and is what `MXBMRP3_Test_PanelPadY` reports both terms for. Two wrong assertions preceded it - "identical" and "never decreases" - and both are false |
| `title_band_test.cpp` | **The caption row under every `[card]` switch combination** (restored post-port; header records the two adaptations). A bandless caption still clears the content below it - the bug drew the caption and the panel's first reading on one line, on all three centre-stack panels at once, because `emitTitleBand()` returned 0 for both "no caption" and "caption, no band". Plus: switching the band off compacts without moving the caption (the band-vs-bare delta constant across frame sizes - one size cannot tell "tracks the frame" from "agrees here"); the settings panel reserves the height of the band it draws (clearance constant across `[card]` sizes - it shipped merely tight at the sizes the shipped themes use); `[card] title border` scales the band alone (card untouched, panel grows with it, and the plan's per-box columns: rows track the card only, caption clearance tracks the band only); and the settings panel's bandless caption tracks the frame like a HUD's (the one panel not on `addTitleString`) |
| `box_terms_test.cpp` | **Every air term reaches an UNTHEMED panel.** The box model states eight terms and the settings menu documents all eight, but four - `titleMargin`, `titlePadding`, `contentMargin`, `contentPadding` - did nothing unless a theme was selected, which is not the default: `PanelBox::layoutPanel` collapsed the whole title/content BOX on "is there art to draw a border with" when only the border needs art, and `SettingsHud::cardPad*()` gated its content padding the same way, separately. Nothing caught it because every other geometry case installs a theme first or asserts a term against itself - self-consistency is exactly what a dead term satisfies. Each term is set to 0, then 3, then back via `MXBMRP3_Test_SetBoxTerm`; the panel's rect and every string position must MOVE, and must restore. Deliberately no golden: the magnitude is `panel_box_test`'s job against the shared fixture, and a number here would go stale on every retune and bury the one failure this guards. Second case asks the same of the settings panel, which lays out its own geometry rather than going through `layoutPanel` - three terms did not reach it when the case was written (it read the legacy `panelPadding{X,Y}Cells` and never asked for the caption block's terms), and it now spends the box terms for its own chrome, so all eight are asserted alike |
| `center_stack_theme_test.cpp` | **The centre stack's stored-offset contract.** `[card] hud-content` must not move a panel's top edge (the Gap Bar slid when a skinner flipped it - its top was derived by subtracting a flag-gated padding from its stored offset); the Gap Bar's absolute top is one grid cell (the same root cause once computed it to y=0, top frame slice clipped, with both states agreeing); the three boxes do not overlap **unthemed** (the configuration the stored defaults are derived for); and a theme landing moves **no** top - the stored tops follow the `[Advanced]` built-in, not the theme (center_stack.h). The themed no-overlap assertion is deliberately gone: an additive-model theme grows the boxes past the stored tops by design, documented at `contentPaddingY()` as awaiting a persistence migration. Second case: the Notices slab lands flush on the card's outer edge at any card size, and card art with the card OFF adds nothing (the card's border against the raw pad; the `drawnCardBorderY` helper that spelled it went with the pre-plan chain) |
| `settings_layout_test.cpp` | **The settings panel's click-region surface as behaviour**: regions are hit-tested in emission order, so a converted control that swaps its arrow pair or drops its tooltip row is silently broken while rendering perfectly. Structural layer (each control's tooltip row exactly once) survives additions; the **golden** (full region sequence + `typecount` ordinal-shift guard, REQUIREd first so an enum insertion reads as one line instead of a wall of shifted ordinals) is re-blessed deliberately by reading the diff. Passed unchanged through the box-model port - geometry moved, the click surface did not, which is exactly the split its header states (the signature deliberately carries no coordinates) |
| `standings_layout_test.cpp` | **The plate number's two placement paths agree, and both are right.** Standings places row text via full rebuild and via the drag fast path; the plate nudge went into one path first (centred at rest, jumped high mid-drag), and the later glyph-centring move applied the offset **twice** with both paths agreeing about it - so the agreement case carries a correctness case beside it (inset < 2% of plate height, every row, tolerance an order under the ±9% failures it pins) |
| `settings_button_theme_test.cpp` | **Every settings-footer button is a themed button**: a themed button is nine quads, a flat one is one, so "did this go through `addButtonQuad`" has an integer answer. Shipped twice identically (the Reset button, then the update chip - invisible without a theme both times). Measures the **About** button now: the chip it used to measure is gone, and the old probe relied on that chip APPEARING (panel quads with an update pending minus without), which an always-present button cannot offer. Counts the quads inside the button's own click region instead - a better probe than the delta it replaced, since it isolates one named button rather than inferring it from a panel total. Baseline asserted against a slice-less theme, so a "9" is the button's and not the panel's |
| `stripchart_parity_test.cpp` | **Per-primitive golden fingerprints of the four strip-chart HUDs** (Telemetry, Rumble, Performance, Session Charts): counts + position/colour/sprite/text checksums plus order-sensitive rolling hashes (z-order regressions reorder terms without moving a sum), each HUD isolated by rewriting `visible=` in the real saved INI. **It was removed red**, and its header carries the full refresh discipline: four golden generations, each delta explained before re-blessing - the fourth is the box-model re-pin (positions moved, every count/colour/text checksum identical), made after the open `sectionGapY` question was settled as `[panel] gap` with a one-cell unthemed default |
| `settings_fit_test.cpp` | **The settings panel is tall enough for every tab, and the same height on all of them.** Those two pulled against each other for a long time: a fixed row budget (`LayoutMetrics::settingsRows`) met the second and missed the first whenever a tab grew - the reported "buttons and Help & Community overlap on the General tab", found by a player - while sizing the panel to the ACTIVE tab met the first and missed the second, moving Save/Close/Reset on every switch. The panel measures its TALLEST tab now and draws every tab at that height, so both hold by construction; this drives the pair anyway, because "by construction" is a claim about code. Overrun comes from `MXBMRP3_Test_SettingsOverflowRows` and should be zero everywhere - a positive value means the measure pass and the real lay-out disagree about some tab, the one failure mode a measured height still has - and the panel's own edges are compared across tabs, because that is what a user sees move. A second case pins the benefit that a fixed number could never give: `[content] padding` is paid PER SECTION, so a theme with air in it now makes the panel TALLER (asserted as a pair, height grew AND nothing overflowed - either alone is satisfiable by the old clipping). Deliberately NOT clamped to the display |
| `settings_render_test.cpp` | the settings panel **draws content, and fits the screen, in all four corners of (theme on/off) x (developer mode on/off)** - the smoke test it did not have. A change to how it sizes itself passed every other settings suite and rendered in game as one tall black rectangle with no text: the suites assert click-region ordinals and row geometry, and none asked whether any string came out. The panel measures EVERY tab to size itself, so a section that exists only under developer mode is measured only under developer mode - hence that axis. (The fit case is currently red at ~1.01 screens with a thick theme: pre-existing, and the debt behind `settings_fit_test`'s other half.) |
| `standings_row_band_test.cpp` | **A full-row band sits inside its card, at any amount of theme air.** Reported as "the highlight grows outside the content of the card way beyond where the text is": a band's span was `contentRowInsetX()` - frame border + card border - which was the row's true inset until `[panel]` padding started acting on a plan panel's card. The card moved in, the band did not (measured at padding 3: band 61..414 against a card at 81..394), and there were eight copies of that expression across three HUDs. They ask `plan.contentX()`/`contentW()` now. The band's span is REPORTED by the HUD rather than recomputed here - a test that derives it agrees with whichever derivation it copied, which is how the fault survived - and what is asserted is the relationship: inside the card, still a band and not a sliver, and the card-to-band clearance GROWING with `[content]` padding. (First written as "the band narrows", which is the other box model and wrong here; it failed by reporting correct behaviour, which is the useful half of asserting a relationship you had to look up) |
| `theme_geometry_test.cpp` | The settings panel's **theme-geometry contracts**, on the box-model surface (a previous test of this name pinned the pre-port chain; the hooks it drove stayed exported). Both contracts are **relative**, so the model can keep moving under them. (1) *Switching themes never moves the content*: the layout centres the content box and hangs the panel off it by named overhangs, so only the outer edges may move - the historical failure walked the row steppers sideways a cell per theme cycle. (2) *The gutter is the seam*: the air between the sidebar and content cards must measure what the vertical air between two section cards measures, and turning `[panel] gap` must move **both by the same amount**. That equality was measured broken twice (gap-only trough composition; the unpaid row lead-in) and both bugs passed every other gate, because nothing compared the two axes - each would have been one failing CHECK here. Reads the DRAWN card edges (`MXBMRP3_Test_SettingsGutter` records them at the `rewriteThemedCard` sites), not a re-derivation of the chain that places them - a re-derivation would agree with the chain's bugs A third contract lives here too: *the panel fits the screen*, driven over EVERY tab (enumerated from the registry via `MXBMRP3_Test_SettingsTabName`, so a new tab is covered without anyone remembering) at three frame thicknesses. It matters more than it used to: the panel sizes itself to the tab it is showing, so nothing bounds its height from inside any more - the hand-set `settingsRows` floor used to bound it by clipping the tab instead |
| `settings_surface_test.cpp` | **The settings panel measured against the panel beside it**, which is the question every other case here cannot ask: they all measure one panel against itself, and both bugs this pins were reported from a screenshot by someone comparing neighbours. (1) *Every outer surface sits on the line the title band sits on.* The band spans the frame's inner boundary like every panel's body card; the settings panel inset its sidebar and section cards by `[panel]` padding **on top of** that border, so past padding 0 the band overhung them at both ends. Swept over the padding, because at 0 - which every shipped theme used - the two lines are the same number and the fault is invisible. Asserted as an EQUALITY, not containment: pulling the cards in is what the other fix looks like, and it moves the same bare strip outside the band, where it reads as a notch. (2) *One band height across the surface.* `titleBandBoxHeight()`'s comment claimed this before it was true - a band's drawn height has **two** owners, `PanelBox`'s for a plan panel and that function's for the legacy chain and the settings panel, and only the plan stretched its band by the caption block's quantization remainder (`titleSlack`). ~10px at 1080p, on every themed screen with a HUD open beside the menu. Driven across all three caption chains (Standings, Map, settings) so a failure says which one moved, and swept over `[title]` padding because one value is one lattice phase (3) *The panel insets its band by `[panel]` padding, like a HUD.* Clause (1) is satisfied perfectly by all three surfaces sitting on the frame and ignoring the padding, which is exactly what shipped after it was written - reported from a screenshot at `padding = 3`, the HUD beside the menu holding its card three cells inside its frame and the menu holding nothing. So this measures each panel's band against its OWN background edge and requires the two distances to match, with Standings (where PanelBox already puts the band at `panelInnerLeft`) as the reference rather than a second copy of the arithmetic |
| `theme_palette_test.cpp` | The **three-step precedence** for colours and fonts - built-in default → theme → user override - which had no test of any kind: the Appearance tab builds its colour/font click regions BY HAND, so `MXBMRP3_Test_SettingsClickCycle` cannot reach them, and ColorConfig/FontConfig do not link into the unit suite. Pins the regression that shipped: `cycleColor()` wrote the slot array directly instead of going through `setColor()`, so the override flag never got set, the theme's value kept winning, and cycling a colour did visibly nothing - found by a user, not a test. Both halves are asserted (the flag AND the effective value), because either alone still passes with the bug present. **The font half deliberately does not use `cycleFont()`**: it early-returns when no fonts are discovered and the suite stages none (the case reports the count - 0), so cycling there would assert the empty environment rather than the code; `setFont()` reaches the same precedence machinery without assets, and the STEPPING stays covered only by the in-game menu |
| `palette_test.cpp` | The **Appearance palette and its contract with the shipped packs**: eight of the nine pit board / gamepad skin hues are palette entries under the SAME NAMES the packs are shown by, which is what lets a player match text to their board exactly rather than by eye. That promise is a label in `getColorName`'s switch and a `name` in a pack ini - two unrelated files, and nothing about renaming one makes the other fail - so a census walks the shipped skins and requires a colour of each name. Also pins what had NO coverage before: every entry nameable, unique (getColorName is a `switch` on the VALUE, so duplicates cannot both be labelled), and findable by `getColorIndex`; the eight equal to their `BrandColors` source; and that the three renamed generics (Amber / Aqua / Bright Yellow) kept their exact values, since recolouring them instead would have moved the WARNING and NEUTRAL defaults, the yellow flag, the fuel bar and the gear readout. `getColorName`/`getColorIndex` became `inline` for this: they are pure functions over the constants but sat in `color_config.cpp` beside the ColorConfig singleton, which does not link headless |
| `pack_by_name_test.cpp` | **Every pack type names and stores itself the same way.** Two rules, both cross-type, both previously unenforced. (1) The identity section: every pack's ini opens with `[pack]`, where each type used to spell it differently (`[pad]`, `[board]`, `[gauges]`, `[theme]`, `[Pack]` - singular, plural, and one capitalised), so a modder who had written two packs still had to open a shipped example to write the third, on line one of the file. The case walks the shipped tree, so a sixth type that invents its own section fails on the day its first pack ships. (2) The selection key is exempt from inline-comment stripping. `;` is legal in a Windows folder name and the loader's comment strip was unconditional, so a theme in `retro;90s` loaded as `retro`, degraded, and the next save wrote the TRUNCATED name back - permanently destroying the choice, which is the one thing by-name storage exists to prevent. `Settings::isFolderNameValue()` names its keys through the key SYMBOLS so a RENAME cannot break it - but symbols do nothing about an ADDITION, which is how the gauges pack shipped with `gaugesPack` missing from that list. Nothing failed, because every shipped folder name is semicolon-free: the bug only appears in somebody's own folder, months later. This walks the pack types as data, so a sixth fails here on the day its key is added. Also pins that the exemption stays a SHORT list (`tts_voice` is deliberately outside it - its line carries a comment) and that the two gauges share one key name rather than two that could drift |
| `theme_icons_test.cpp` | **Theme icon overrides follow the selected theme.** The resolution arithmetic is pure and lives in `tests/unit/test_icon_resolve.cpp`; what only the real plugin can answer is whether it notices a theme CHANGE. `AssetManager` memoises the active theme against `themeGeneration()` - the same measured reason `BaseHud` does, since one rebuild resolves it dozens of times - and a memoised pointer that outlives its theme is the classic failure: icons keep coming from a theme the user just switched off. Two themes each overriding the same name is the case that catches it; every other assertion here passes with a stale cache. Overrides are **injected** via `MXBMRP3_Test_SetThemeIconOverride` rather than loaded, because the harness stages no `icons/` directory and staging one would renumber every sprite index the parity goldens hash - reading `themes/<name>/icons/` off disk stays a manual check |
| `xinput_thread_test.cpp` | XInput **I/O thread**: the rumble send policy (first-send, idle-silence, transition-to-zero, disabled-guard) and 8-bit quantization survive the move off-thread - asserted on the command `setVibration()` posts, with the I/O thread stopped so it can't drain the post first |
| `settings_click_test.cpp` | the settings-menu **click path**, headless: a click routed through the real `handleClick` → hit-test `m_clickRegions` → `dispatchRegion` → `applySteppedControl` seam (`MXBMRP3_Test_SettingsClickStepped`), pinning the `SteppedControl` descriptors' clamp + hold-repeat acceleration tiers |
| `rumble_effect_test.cpp` | rumble **effect math** (the values users tune): telemetry→channel mapping through the real RunTelemetry path - zero telemetry is silent, slip ramps map correctly, a suspension spike scales by the per-bike profile JSON, airborne suppresses ground effects, malformed profile JSON falls back without crashing |
| `plugin_thread_test.cpp` | the **`[Advanced] pluginThread=1` worker thread**: every game-state callback applied on a separate thread is functionally equivalent to the sync path - the same synthetic race produces the same standings (with a `pluginThreadFlush()` barrier before asserting) |
| `plugin_thread_golden_test.cpp` | threaded twin of `replay_golden_test`: the same real full-race callback capture (the committed `*.tape.gz` fixture) reconstructs the **identical** golden result with the worker on - no event dropped, reordered, or raced across the queue |
| `plugin_thread_latency_test.cpp` | the worker's whole point: a 60 ms stall injected into `produceFrame` (via `MXBMRP3_Test_SetProduceDelayMs`) is paid by the game's Draw in sync mode but **not** in threaded mode; performance metrics stay live off-thread |
| `plugin_thread_abort_test.cpp` | worker killed by an escaping exception (via `MXBMRP3_Test_PluginThreadAbortWorker`): routing falls back inline immediately, the stranded backlog is drained in order, and threaded mode latches off (no respawn loop) |
| `plugin_thread_switch_test.cpp` | **runtime legacy↔threaded switch** (the RELOAD_CONFIG path): flip the `[Advanced] pluginThread` flag and the next Draw's `reconcileEnabled()` starts/stops the worker - standings stay correct in sync, then threaded, then sync again, on one running instance |
| `plugin_thread_teardown_test.cpp` | teardown with the worker **still running** and a callback still queued: `shutdown()` joins the worker first and drains the queue inline - clean return, no hang, no use-after-free |
| `plugin_thread_flush_test.cpp` | `flush()` **terminates** when the worker never drains (via `MXBMRP3_Test_PluginThreadSwallowBatches`, which discards batches already taken off the queue - the window where a dying worker destroys an in-flight sentinel). Both waits were unbounded, so the calling thread parked forever *and* the abort self-heal that runs on it could never fire. Bounding them lets `flush()` return with the sentinel still queued, so the sentinel is heap-owned and captured by value - a later drain running it is safe rather than a use-after-free. A **watchdog test**: unfixed, it hangs to the per-test timeout instead of failing an assertion |
| `analytics_wiring_test.cpp` | analytics **event wiring** via the dry-run capture seam (no network): app_started is the always-sent tier (anon id + feature flags + `isDebug`); a full launch enqueues session_end + custom, a minimal launch drops both, a crash bypasses the gate. Analytics is compiled into the test DLL but never auto-inits; capture mode makes the real senders no-ops |
| `http_test.cpp` | the **serving path**: the real HTTP server answers `/api/state` and it byte-matches the direct `snapshot()` |
| `http_robust_test.cpp` | slow-loris / partial / malformed clients don't wedge the server or stall the game-thread snapshot |
| `http_gating_test.cpp` | `onDataChanged`'s two change classes: frequent types (Standings) don't rebuild the snapshot while nothing is consuming, rare transitions (RaceEntries/SessionData) rebuild anyway, and frequent types still rebuild once a client is active; plus the **build-side coalescing window**: a burst of frequent changes collapses to a couple of rebuilds rather than one each (the SSE loop pushes at most once per throttleMs and discards intermediates, so a rebuild per change was whole-grid serialization on the game thread that no client could receive), the deferred change still lands once the window elapses, and a rare transition is never deferred |
| `replay_test.cpp` | the tape read/dispatch machinery: a `TapeWriter`-synthesized tape round-trips through `replayTape()` (no game needed) |
| `recorder_test.cpp` | the **in-plugin recorder** end-to-end: disabled (default) writes nothing; enabled, a known synthetic stream produces a well-formed `MXBHREC` tape (raw bytes asserted: magic, framing, per-type counts, the compound packings) that replays back to the same standings |
| `replay_golden_test.cpp` | **real-data golden master** (solo): replays a real 1-lap MXB Club capture, asserts the reconstructed result |
| `replay_golden_multi_test.cpp` | **real-data golden master** (24-rider Farm14 race): the whole pipeline at once - winner, time gaps, fastest-lap chip on a non-winner, a real penalty, a lapped rider, DSQ/DNS/retired |
| `teardown_test.cpp` | shutdown/unload **under load**: HTTP/SSE server live + the real 24-rider tape churning standings, then Shutdown → `FreeLibrary` (static destruction) is clean; plus the unload-**without**-Shutdown (auto-save backstop) path - guards the analytics-reported AV-on-teardown class (core + HTTP path only; Discord/Steam/records are compiled out of the test DLL) |

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

Then drop the file in `tests/integration/tests/` - the runner finds it automatically
(no list to edit) and CI picks it up. Conventions that matter:

- **One plugin lifecycle per file.** The plugin is stateful (it re-derives
  standings on each update), so drive successive phases sequentially against one
  running instance and snapshot after each - don't use `SUBCASE` for phases (it
  re-enters the case body, re-running Startup).
- **Save dir** is `Z:\tmp\mxbmrp3-tests\<name>\` (the runner wipes the tree and
  pre-creates each dir). Keep it distinct so tests don't share a `settings.ini`.
- **Add a callback** the harness doesn't expose yet by adding a driving helper to
  `PluginHost` (and its struct to `plugin_api.h`, byte-compatible with
  `vendor/piboso/mxb_api.h`). **Add a JSON field** to assert by extending
  `assertions.h`. Keep the shared shape in the harness, the scenario in the test.

The harness pieces:
- `plugin_api.h` - the `SPlugins*` structs the tests drive (mirror the real ABI).
- `plugin_host.h` - `PluginHost`: `LoadLibrary` + export resolution, the callback
  drivers, `startHttp()` (via the `MXBMRP3_Test_StartHttp` hook - no settings
  seeding needed), and `state()` returning parsed JSON.
- `assertions.h` - `checkStandings()`, `hasEvent()`, `riderByNum()`.
- `integration_main.h` - shared `main()` that takes the DLL path positionally.
- `ini.h` - INI parse/diff helpers for the settings/persistence tests.
- `tape.h` - the callback-tape format (byte-identical twin of the in-plugin
  recorder) + `TapeWriter` for synthesizing tapes.
- `zipwrite.h` - in-memory zip builder (the updater test's download stand-in).
- `doctest.h` - vendored single-header framework.

### Test-only hooks

Some internal actions aren't reachable through a game callback (reset-to-defaults,
copy-profile, force a save, (re)load settings from disk, compare versions). They're
exposed as `MXBMRP3_Test_*` exports in `mxbmrp3/core/test_hooks.cpp`, gated
entirely on `MXBMRP3_TEST_BUILD` - so they **don't exist in the shipping DLL**.
Add a hook there when a test needs to invoke an internal action the game API can't
trigger.

**Hooks also cover internal state that never reaches the JSON.** Test what a
computation *is*, not just what the overlay renders. The real-time gap
(`RaceTrackPosition` → `updateRealTimeGaps`) is in-game-only - read by
`StandingsHud`, not emitted in `/api/state`. Rather than force it into the data
contract (a product decision) or leave it fuzz-only, `trackpos_test.cpp` reads it
directly via `MXBMRP3_Test_GetRealTimeGap` and asserts the algorithm (a follower's
gap is how much later it reaches a point the leader stamped). White-box, in the
plugin's own units - the right seam for internal logic the black-box snapshot
can't see.

Not every integration test asserts on `/api/state` - a settings test asserts on
the re-saved `settings.ini` instead. `reset_test.cpp` is the pattern: start the
plugin, perturb a few anchor keys in the INI on disk, pull them into live state
with the `MXBMRP3_Test_LoadSettings` hook (the "set live state" seam), run the
reset, re-save, and diff the file with `harness/ini.h`. It runs in one process -
no capture-default-in-a-separate-run dance.

> **Known gap:** only Reset *All* is asserted. Per-profile and per-HUD resets clear
> the *profile diff*, not the shared base section, so perturbing a base-section
> value doesn't exercise them (the "`m_hudDefaults` is not a clean factory
> snapshot" property). Covering those cleanly needs perturbing a profile-diff
> section; tracked in `tests/integration/API_COVERAGE.md`.

### Real-data replay (callback tapes)

The integration tests drive **synthetic** callback streams - deterministic and
great for targeted/edge scenarios, but only as faithful as our reading of the
API. The fidelity anchor is a **callback tape**: a recording of the *real*
callbacks the game sends, replayed headlessly and asserted.

Producing and playing tapes:

- **In-plugin recorder** (`mxbmrp3/core/event_recorder.{h,cpp}`, MX Bikes only) -
  the main plugin records every callback to a binary `MXBHREC` file when a
  developer sets the hidden `[Recorder] enabled=1` INI key (no HUD, no hotkey);
  tapes land in `<save>/mxbmrp3/tapes/`. The only way to *capture* a real tape
  (needs the game). This replaces the old standalone `mxbmrp3_record.dlo` plugin,
  which used its own process + console window - closing that console `ExitProcess`ed
  the game without a clean `Shutdown()`, crashing the main plugin's teardown.
- **`tools/replay/`** - replays a tape into the plugin in **real time** (`--speed`),
  e.g. into a live plugin with the web server on so you can preview the overlay
  against real data in a browser. A manual dev/preview tool.

For **automated** testing, `PluginHost::replayTape()` reads that same format and
dispatches each event into the plugin's real exports, then a test asserts the
resulting `snapshot()` - headless, in CI, under Wine. The core users:

- `replay_test.cpp` - a round-trip on a tape synthesized with `harness/tape.h`'s
  `TapeWriter` (proves the read/dispatch machinery without needing a game).
- `recorder_test.cpp` - a full round-trip through the **in-plugin recorder**: drive
  a live race that the recorder captures to a `.tape`, then replay that tape into a
  fresh plugin instance and assert identical standings (proves the record path, not
  just replay).
- `replay_golden_test.cpp` / `replay_golden_multi_test.cpp` - the **real-data
  golden masters**: replay actual in-game captures and assert the plugin
  reconstructs the result, every value cross-checked against the session log.
  One is a solo 1-lap finish (MXB Club); the other is a **full 24-rider race**
  (Farm14) that exercises the whole pipeline at once - winner, time gaps, the
  fastest-lap chip on a non-winner, a real Cutting penalty, a lapped rider, and
  DSQ/DNS/retired states. These are the fidelity anchors for the synthetic tests.

The same tapes are reused by other tests: `plugin_thread_golden_test.cpp` replays
the solo capture with the worker thread on (identical-result equivalence),
`teardown_test.cpp` replays the 24-rider capture to load the shutdown path, and
`director_broadcast_test.cpp` replays it under an injected sim-clock via
`replayTapeTimed()`.

The captured tapes live gzipped under `tests/integration/tests/fixtures/` (recorder
format, slimmed to the state-changing events - telemetry/vehicle/draw/track-
position dropped, verified to yield the identical `/api/state` as the full
multi-megabyte captures); `tests/integration/run_tests.sh` unpacks fixtures before the run. Assert the *final*
classification + key events, not every frame - real timing is noisy.

> **Maintenance:** `harness/tape.h` must stay byte-identical to
> `mxbmrp3/core/event_recorder.{h,cpp}` (EventType values, `FileHeader`/
> `EventHeader` layout, the `RaceClassification`/`RaceTrackPosition` packings).
> **Enforced:** both files `static_assert` the same literal sizes/offsets (the
> "tape contract" blocks), so a one-sided edit fails to compile; a deliberate
> format change updates both, bumps `FileHeader::version`, and re-records.
> A recorded tape is coupled to the `mxb_api.h` struct layout at record time -
> record fresh after an API change.

## Layer 3 - Specialized runners (`tests/integration/`)

Different modalities that don't fit the snapshot-assertion shape, each its own
script:

| Runner | Kind | Asserts |
|---|---|---|
| `run_persist_test.sh` | property | flips every boolean setting, then that all survive a save→load→save round-trip (the per-HUD registry "silently reverts on restart" write-back trap) |
| `run_fuzz.sh` | survival | a corpus of malformed `settings.ini` + the six JSON config files must never crash or abort the load |
| `run_fuzz_callbacks.sh` | survival | every DLL-boundary callback survives adversarial sizes/counts/bytes (found + guards a real `TrackCenterline` OOB read) |
| `run_perf.sh` | baseline | runs two drivers against a full 50-rider grid on a long/complex ~2400m circuit (`perf_scenario.h`, a heavier superset of the real Farm14 tape): `perf_driver` (isolated per-callback cost) + `map_perf_driver` (the interleaved MAP hot loop). Gates the gross-regression avg **and** both the general Draw p99 and the worst map-ON Draw p99 under the 480fps budget. Also runs `bench_driver` + `tools/benchmark_report.py` as a report↔analyzer contract check |
| `run_tape_bench.sh` | real-data | replays a committed `.tape.gz` (default the multiplayer Farm14 24-rider capture) through `tape_bench_driver` (PluginHost), profiles the reconstructed real field with every HUD visible, and runs `tools/benchmark_report.py` - the per-HUD **render footprint** (`default` vs `max` settings). Not gated (FPS is a tight-loop artifact); it's an inspection tool. The slim real fixtures are standings-only; pass `tests/fixtures/synthetic_positions_22riders.tape.gz` to also light up the **map/telemetry** (that fixture is synthetic - real in FORMAT, generated headless by `make_positions_tape.sh` since a real capture needs the game) |
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
`makensis` + `wine` - not the mingw cross-build.

It drives the installer's **`/ELEVATED` command-line path** (the process the
on-demand elevation relaunch spawns), because that child takes the whole game
selection on the command line and runs the *same* install/uninstall Section,
registry and data-wipe code - so the mechanics are exercised without needing to
drive nsDialogs wizard pages headlessly.

> **Known gaps (manual Windows check).** Wine has no UAC and doesn't enforce ACLs
> for a normal user, so three things the runner can't reach stay a manual pass on
> real Windows (P1 matrix, and `packaging/mxbmrp3.nsi`): the writability probe
> actually **triggering** the elevated relaunch (Wine dirs are writable, so it
> never fires); the genuine **UAC prompt** and cross-account (standard user →
> admin credentials) elevation, including that the savepath resolves to the
> *launching* user's Documents; and the per-user **HKCU** hive branch (Wine always
> permits the HKLM write, so `useMachineReg` is always 1 here - it's the same
> `WRITE_UNINSTALL_REG` macro with a different root). The interactive pages
> themselves render correctly (verified once by hand).

## Layer 4 - Web overlay (`tests/web/`)

The browser/OBS overlay (`mxbmrp3_data/web/`) is the one piece the C++ layers
can't reach: they assert the plugin's `/api/state` JSON (what the overlay
*receives*); these assert what the overlay *draws* from it (tower ordering; battle-card
**live gaps**, reached by freeing the shared bottom slot via the localStorage
CONFIG override). They drive the overlay's built-in **`?demo` mode** - a synthetic 22-rider warmup + race that
feeds the same snapshots into `render()` the live SSE stream would - in headless
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
> - does not overflow, fills the width, element is hidden - never an exact pixel,
> because pixels are a property of the browser build.

**One spec deliberately does NOT use `?demo`.** `overlay_snapshot.spec.js` loads
`tests/fixtures/overlay_snapshot.json` - captured from the real plugin by
`tests/integration/tests/overlay_snapshot_test.cpp` - and feeds it to the same
`render()` the SSE stream calls. Everything else here drives the demo, whose
snapshot the overlay writes itself, so a field renamed in `buildJsonSnapshot()`
would leave both suites green while the live overlay drew nothing. The C++ twin
fails on the rename; this one fails if the client still reads the old name.
Note the trap found while writing it: an assertion that mirrors the client's own
`fullName || name` fallback launders the drift it is meant to catch, so the spec
requires the preferred field outright.

**Lint (`tests/web/lint.sh`, the `eslint` gate).** The same Node install also
carries ESLint over every `.js` in the tree - the overlay, `sw.js` and this
suite - in about a second:

```bash
./tests/web/lint.sh             # what CI and the ctest gate run
./tests/web/lint.sh --fix       # apply the fixable ones
```

It is eslint's own `recommended` set, minus three rules the shipped overlay's
design makes unusable (`no-undef` and `no-unused-vars: vars` because the overlay's
scripts share **one global scope** and ESLint sees one file at a time; `no-redeclare`
because ES5 has no block scope). `tests/web/eslint.config.mjs` states each
reason at the rule. The gate exists because the JS had no lint at all: the two
dead-code nits fixed during the 1.28 release prep were found by hand-running
CodeQL's *code-quality* suite, which no CI job runs.

No game, no plugin, no network - just Node.js. See `tests/web/README.md` for the
gotchas (rows are `translateY`-slotted over a stable DOM order, so ranking is read
by on-screen Y; tests live outside `mxbmrp3_data/web/` because that folder ships
to users). Adding a case is one `test(...)` in `tests/web/tests/overlay.spec.js`.

## Layer 5 - Memory safety (`tests/asan/`)

Answers one question: **is the plugin corrupting memory?** Two shipped crashes
were access violations in innocent heap walks - the signature of heap corruption,
where the dump's `module+offset` shows the *victim*, never the *writer*.
AddressSanitizer instead faults **at the corrupting write**, with the writing and
allocating stacks. Two native pieces (no game, no Windows, no Wine - just
g++/clang + libasan) gate CI via the `memory-safety` job in
`.github/workflows/tests.yml`:

- **The whole unit suite under ASan + UBSan** - `ctest -R unit-asan` builds the
  same TUs with `-fsanitize=address,undefined`, so every surface the unit tests
  already exercise is checked for out-of-bounds / use-after-free / UB, not just
  for correct results. A new unit test gets this coverage automatically
  (`MXB_UNIT_SOURCES` is shared - no second list).
- **A targeted harness** (`tests/asan/memory_safety_fuzz.cpp` + `tests/asan/run.sh`)
  aimed at the fixed-buffer / index surface behind the two shipped heap-corruption
  crashes: `RaceEntryData`'s fixed buffers over hostile names/numbers, the
  leader-timing `clamp((int)(trackPos*100), 0, 99)` index over NaN/Inf/huge/random
  bit patterns, and churn of the two crash-site container types.

The **faithful** pass is the separate `memory-safety-msvc` CI job: it builds the
real plugin DLL with MSVC `/fsanitize=address` (`MXBMRP3_ASAN=ON`, `Debug` config -
exempt from the Release analytics-key requirement, so no secrets) and drives it
through the real
DLL-boundary callbacks with `callback_fuzzer.cpp` on a Windows runner, covering
the live `PluginData`/`StatsManager`/HUD/HttpServer pipeline the portable layer
can't compile. It runs automatically in the free public mirror but is **opt-in**
in the metered private repo (the `asan_msvc` checkbox on Run workflow - a Windows
runner burns minutes at 2x). `tests/asan/run_asan_msvc.ps1` reproduces it locally
on Windows.

Where a memory-safety test goes: adversarial cases for a fixed buffer or index
computation belong in `memory_safety_fuzz.cpp`; anything expressible as a normal
unit test is already covered by the `ASAN=1` rerun. Note the honest limit: ASan
catches spatial (out-of-bounds) and temporal (use-after-free / double-free)
errors on the paths actually executed - it does **not** catch pure data races.
`tests/asan/README.md` has the full policy, the MSVC ASan-runtime (`/MDd`) note,
and the no-rebuild PageHeap option for in-the-wild reproduction.

## Layer 6 - Visual (`tools/hud_window/companion_demo.sh`)

Answers the question the other five can't: **does it still look right?** The
companion window renders the plugin's live quads and strings with the in-process
software renderer (`core/hud_sw_renderer`), drawing text from the game's own
pre-rasterized `.fnt` atlases - the same glyph data the game samples. So a capture
is not an approximation of the in-game HUD; it is the same primitives through the
same font metrics, and it can be **pixel-diffed**.

```bash
tools/hud_window/companion_demo.sh out.png                # default scene
tools/hud_window/companion_demo.sh out.png 25 tab Map     # a settings tab
tools/hud_window/companion_demo.sh out.png 25 tab "Lap Log"  # quote multi-word
tools/hud_window/companion_demo.sh out.png 25 gamepad     # a scene mode
tools/hud_window/companion_demo.sh --verify-deterministic 25 tab Map
SHOT_RES=2560x1440 tools/hud_window/companion_demo.sh out.png
compare -metric AE before.png after.png null:                   # 0 == no change
```

**What it is for.** A refactor that claims to preserve rendering can be *shown* to,
instead of argued to: capture the affected scenes before and after and require
`AE == 0`. That is a stronger statement than a click-region golden, which pins
emission order but not a single pixel. It is equally the way to review a change
that is *supposed* to look different - the diff is the review artifact.

**What it is not.** It is not asserted and not CI-gated: there are no committed
baselines, so it proves nothing on its own the way `ctest` does. Treat it as an
instrument (like `run_tape_bench.sh`), and keep the baseline you diff against in
the same session - a capture from a different toolchain or font revision is not a
valid baseline.

**Three ways a diff lies, all of them quiet** - the reason this section is longer
than the tool:

- **A capture rendering WITHOUT assets is the quiet failure.** Icons fall back to
  `[x]`/`[ ]` text - **72,784 px** for one scene - and the result is a plausible,
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
  diff - it is stamped from `git rev-list --count HEAD`, so a branch and its base
  always differ there. `companion_demo.sh` pins it via `MXBMRP3_VER_BUILD`, so
  captures are directly comparable. Drive the window another way and that floor
  comes back, ~185 px wherever the version shows.)

- **An async repaint can make a scene undiffable, and a fixed hold does NOT save
  you.** Something background repaints over the panel title, worth ~10,654 px.
  It is *not* a function of the hold: two runs at the SAME hold differed by that
  amount - at hold 6 on one machine, at hold 12 on another - so "always use the
  same hold" only moves which machine it bites. The demo seeds `updateMode=off`
  to remove one async source; that is an improvement, **not a fix** - the repaint
  has still been observed with it seeded. Screen a scene before trusting it:

  ```bash
  tools/hud_window/companion_demo.sh --verify-deterministic 25 tab Map
  VERIFY_N=5 tools/hud_window/companion_demo.sh --verify-deterministic 25 gear
  ```

  Treat a pass as "no divergence in N runs", never as "cannot race": at N=2 this
  passed on a scene that a later capture showed differing by 10,654 px - both
  samples had landed on the same side of the race. **So when a diff comes back
  non-zero, re-capture both sides before believing it.** A stale capture that
  caught the race is indistinguishable from a rendering regression; that is
  exactly how a 10,839 px "regression" here resolved into 185 px of version
  string plus one flaky frame.
- **Multi-word tab names must stay quoted** through to the exe
  (`... 6 tab "Lap Log"`). Unquoted, the extra word is dropped, the tab silently
  falls back to **General**, and you diff the wrong panel - reporting `AE=0` as a
  pass for a tab you never rendered.
- **A blank capture used to report success.** `import` exits 0 on an all-black
  grab, so a window that never mapped produced `==> wrote out.png` and a
  0-pixel diff against another blank. The script now requires the frame to have
  non-trivial standard deviation, retries the racy mapping, and on failure
  deletes the file and exits non-zero rather than leaving a blank for someone to
  diff against.

Needs the `screenshot` dep group (`./tools/install_deps.sh screenshot` - Xvfb +
ImageMagick) on top of the mingw/wine toolchain. `tools/hud_window/README.md`
documents the window itself, the `.fnt` layout, and the known limitations
(input still targets the game window; the `mxbmrp3_replay --window` path does not
map under Xvfb).

## Coverage

`tests/integration/API_COVERAGE.md` is the coverage manifest - every game callback and
internal action with its status (asserted / driven / survival / untested) and the
test that covers it. It's a behavioral manifest, not a line-coverage number: the
goal is that gaps are **visible**, not that every line is hit. Update it when you
add a test.

## The cross-build itself

`tests/integration/README.md` documents the mingw build engine (incremental + parallel +
ccache) and exactly how the test DLL diverges from the shipping MSVC build (all
gated by `MXBMRP3_TEST_BUILD` / `_MSC_VER`, so the shipping build is byte-for-byte
unchanged). Manual in-game testing on Windows stays the final check for input and
anything the headless build can't exercise - rendering is no longer on that list
(Layer 6 above), though the game's own GPU path is.
